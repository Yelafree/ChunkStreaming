#include "ChunkStreamingSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerStart.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/LatentActionManager.h"
#include "TimerManager.h"
#include "LatentActions.h"
#include "DrawDebugHelpers.h"

#include "ChunkGraphAsset.h"
#include "ChunkStreamingSettings.h"
#include "ChunkStateStore.h"

// ---- UE 版本兼容：LevelStreaming 状态 API（5.2+ 重构为 ELevelStreamingState/GetLevelStreamingState） ----
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 2
	#define CHUNK_LSTATE_TYPE ULevelStreaming::ECurrentState
	#define CHUNK_LSTATE_LOADING ULevelStreaming::ECurrentState::Loading
	#define CHUNK_LSTATE_UNLOADED ULevelStreaming::ECurrentState::Unloaded
	#define CHUNK_LSTATE_FAILED ULevelStreaming::ECurrentState::FailedToLoad
	#define CHUNK_GET_LSTATE(SL) ((SL)->GetCurrentState())
#else
	#define CHUNK_LSTATE_TYPE ELevelStreamingState
	#define CHUNK_LSTATE_LOADING ELevelStreamingState::Loading
	#define CHUNK_LSTATE_UNLOADED ELevelStreamingState::Unloaded
	#define CHUNK_LSTATE_FAILED ELevelStreamingState::FailedToLoad
	#define CHUNK_GET_LSTATE(SL) ((SL)->GetLevelStreamingState())
#endif

static TWeakObjectPtr<UChunkStreamingSubsystem> GActiveChunkStreamingSubsystem;

UChunkStreamingSubsystem* UChunkStreamingSubsystem::GetActive()
{
	return GActiveChunkStreamingSubsystem.Get();
}

void UChunkStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	GActiveChunkStreamingSubsystem = this;

	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	if (Settings)
	{
		GraphAsset = Settings->GraphAsset.LoadSynchronous();
		bDebugDraw = Settings->bDebugDraw;
	}
}

void UChunkStreamingSubsystem::Deinitialize()
{
	if (GActiveChunkStreamingSubsystem.Get() == this)
	{
		GActiveChunkStreamingSubsystem = nullptr;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
	}
	Super::Deinitialize();
}

void UChunkStreamingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// 收集本世界的流送子关卡（PIE 下包名带 UEDPIE_N_ 前缀，统一剥离后作为键）
	ChunkStreamingMap.Reset();
	for (ULevelStreaming* SL : InWorld.GetStreamingLevels())
	{
		if (SL)
		{
			const FName PkgName = FName(*UWorld::RemovePIEPrefix(SL->GetWorldAssetPackageName()));
			if (!PkgName.IsNone())
			{
				ChunkStreamingMap.Add(PkgName, SL);
			}
		}
	}

	// Always Loaded 检测：此类关卡引擎会强制加载且拒绝卸载
	for (ULevelStreaming* SL : InWorld.GetStreamingLevels())
	{
		if (SL && SL->ShouldBeAlwaysLoaded())
		{
			UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 子关卡 %s 被设为 Always Loaded——流送系统无法卸载它，请在 Levels 面板取消勾选。"), *SL->GetWorldAssetPackageName());
		}
	}

	// 启动诊断日志
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 初始化：图资产=%s，流送子关卡=%d 个，图区块=%d 个"),
		GraphAsset ? *GraphAsset->GetName() : TEXT("(未配置)"), ChunkStreamingMap.Num(), GraphAsset ? GraphAsset->Chunks.Num() : 0);
	if (!GraphAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 未配置图资产：请在 Project Settings -> Chunk Streaming 中指定 Graph Asset。"));
	}
	else if (ChunkStreamingMap.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 当前主关卡没有流送子关卡：请用 Window -> Levels 面板把分块关卡添加进来。"));
	}

	// 初始状态：已在世界中的已加载关卡
	for (const auto& Pair : ChunkStreamingMap)
	{
		if (Pair.Value && Pair.Value->IsLevelLoaded())
		{
			if (IsGameplayChunk(Pair.Key))
			{
				LoadedChunks.Add(Pair.Key);
			}
			else if (GraphAsset)
			{
				LoadedBackgrounds.Add(Pair.Key);
			}
		}
	}
	for (const FName& ChunkName : LoadedChunks)
	{
		RestoreChunkState(ChunkName);
	}
	FString LoadedList;
	for (const FName& C : LoadedChunks)
	{
		LoadedList += C.ToString() + TEXT(", ");
	}
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 初始已加载区块: %s"), LoadedList.IsEmpty() ? TEXT("(无)") : *LoadedList);

	// 初始流送焦点：出生块优先，否则玩家所在块
	StreamingChunk = NAME_None;
	if (GraphAsset)
	{
		for (const FChunkInfo& Info : GraphAsset->Chunks)
		{
			if (Info.bStartChunk && Info.Category == EChunkCategory::Gameplay)
			{
				StreamingChunk = Info.LevelName;
				break;
			}
		}
	}
	if (StreamingChunk.IsNone())
	{
		StreamingChunk = FindChunkAtX(GetPawnAxis());
	}
	PlayerChunk = GraphAsset ? GraphAsset->FindChunkAtLocationSmart(GetStreamingReferenceLocation(), StreamingChunk) : NAME_None;

	// 连通分量编号（不同分量的区块组永不共载）
	BuildComponentIds();

	// 一次性诊断：打印每个玩法块的邻居（确认连接图）
	if (GraphAsset)
	{
		for (const FChunkInfo& Info : GraphAsset->Chunks)
		{
			if (Info.Category != EChunkCategory::Gameplay)
			{
				continue;
			}
			TArray<FName> Neighbors;
			GraphAsset->GetNeighbors(Info.LevelName, Neighbors);
			FString NeighborList;
			for (const FName& N : Neighbors)
			{
				const FString Short = N.ToString();
				const int32 Slash = Short.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				NeighborList += (Slash != INDEX_NONE ? Short.RightChop(Slash + 1) : Short) + TEXT(",");
			}
			const FString ShortName = Info.LevelName.ToString();
			const int32 Slash2 = ShortName.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 邻居诊断 %s → [%s]"),
				*(Slash2 != INDEX_NONE ? ShortName.RightChop(Slash2 + 1) : ShortName), *NeighborList);
		}
	}



	// 初始区块诊断
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 初始 PlayerChunk=%s StreamingChunk=%s（参考轴 X=%.1f）"),
		*PlayerChunk.ToString(), *StreamingChunk.ToString(), GetPawnAxis());
	FString LevelList;
	for (const auto& Pair : ChunkStreamingMap)
	{
		LevelList += Pair.Key.ToString() + TEXT(", ");
	}
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 世界中的流送子关卡: %s"), LevelList.IsEmpty() ? TEXT("(无)") : *LevelList);

	// 启动周期判定
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	const float Interval = Settings ? Settings->UpdateInterval : 0.2f;
	InWorld.GetTimerManager().SetTimer(UpdateTimerHandle, this, &UChunkStreamingSubsystem::UpdateStreaming, FMath::Max(0.05f, Interval), true);
}

// ---------------------------------------------------------------------------------------------

FName UChunkStreamingSubsystem::FindChunkAtX(float X) const
{
	return GraphAsset ? GraphAsset->FindChunkAtX(X) : NAME_None;
}

FName UChunkStreamingSubsystem::FindNearestChunk(float X) const
{
	if (!GraphAsset)
	{
		return NAME_None;
	}
	FName Best = NAME_None;
	float BestDist = TNumericLimits<float>::Max();
	for (const FChunkInfo& Info : GraphAsset->Chunks)
	{
		if (Info.Category != EChunkCategory::Gameplay)
		{
			continue;
		}
		const float Center = (Info.XRange.X + Info.XRange.Y) * 0.5f;
		const float Dist = FMath::Abs(Center - X);
		if (Dist < BestDist)
		{
			BestDist = Dist;
			Best = Info.LevelName;
		}
	}
	return Best;
}

bool UChunkStreamingSubsystem::IsGameplayChunk(FName ChunkName) const
{
	if (!GraphAsset)
	{
		return false;
	}
	const FChunkInfo* Info = GraphAsset->FindChunkInfoPtr(ChunkName);
	return Info && Info->Category == EChunkCategory::Gameplay;
}

ULevelStreaming* UChunkStreamingSubsystem::GetStreamingLevelFor(FName PackageName) const
{
	const TObjectPtr<ULevelStreaming>* Found = ChunkStreamingMap.Find(PackageName);
	return Found ? Found->Get() : nullptr;
}

float UChunkStreamingSubsystem::GetCameraAxis() const
{
	if (!GraphAsset || !GetWorld())
	{
		return 0.f;
	}
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0);
	if (Cam)
	{
		return GraphAsset->GetAxisCoord(Cam->GetCameraLocation());
	}
	return 0.f;
}

float UChunkStreamingSubsystem::GetPawnAxis() const
{
	if (!GraphAsset || !GetWorld())
	{
		return 0.f;
	}
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (Pawn)
	{
		return GraphAsset->GetAxisCoord(Pawn->GetActorLocation());
	}
	return 0.f;
}

float UChunkStreamingSubsystem::GetStreamingReferenceAxis() const
{
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	return (Settings && Settings->bUsePawnAsStreamingSource) ? GetPawnAxis() : GetCameraAxis();
}

FVector UChunkStreamingSubsystem::GetStreamingReferenceLocation() const
{
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	if (Settings && Settings->bUsePawnAsStreamingSource)
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			return Pawn->GetActorLocation();
		}
	}
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(GetWorld(), 0))
	{
		return Cam->GetCameraLocation();
	}
	return FVector::ZeroVector;
}

FName UChunkStreamingSubsystem::FindChunkAtLocation(const FVector& Location) const
{
	return GraphAsset ? GraphAsset->FindChunkAtLocation(Location) : NAME_None;
}

float UChunkStreamingSubsystem::GetForwardSign() const
{
	if (!GraphAsset || !GetWorld())
	{
		return 1.f;
	}
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (Pawn)
	{
		const float Vel = GraphAsset->GetAxisCoord(Pawn->GetVelocity());
		if (FMath::Abs(Vel) > 50.f)
		{
			return FMath::Sign(Vel);
		}
	}
	return 1.f;
}

FVector2D UChunkStreamingSubsystem::GetChunkRange(FName ChunkName) const
{
	if (GraphAsset)
	{
		const FChunkInfo* Info = GraphAsset->FindChunkInfoPtr(ChunkName);
		if (Info)
		{
			return Info->XRange;
		}
	}
	return FVector2D::ZeroVector;
}

bool UChunkStreamingSubsystem::IsChunkLoaded(FName ChunkName) const
{
	return LoadedChunks.Contains(ChunkName) || LoadedBackgrounds.Contains(ChunkName);
}

void UChunkStreamingSubsystem::BuildComponentIds()
{
	ChunkComponentIds.Reset();
	if (!GraphAsset)
	{
		return;
	}
	int32 NextId = 0;
	for (const FChunkInfo& Info : GraphAsset->Chunks)
	{
		if (Info.Category != EChunkCategory::Gameplay)
		{
			continue;
		}
		if (ChunkComponentIds.Contains(Info.LevelName))
		{
			continue;
		}
		TArray<FName> Queue;
		Queue.Add(Info.LevelName);
		ChunkComponentIds.Add(Info.LevelName, NextId);
		while (Queue.Num() > 0)
		{
			const FName Cur = Queue.Pop(false);
			TArray<FName> Neighbors;
			GraphAsset->GetNeighbors(Cur, Neighbors);
			for (const FName& N : Neighbors)
			{
				if (!ChunkComponentIds.Contains(N))
				{
					ChunkComponentIds.Add(N, NextId);
					Queue.Add(N);
				}
			}
		}
		++NextId;
	}
}

int32 UChunkStreamingSubsystem::GetComponentId(FName ChunkName) const
{
	const int32* Id = ChunkComponentIds.Find(ChunkName);
	return Id ? *Id : -1;
}

// ---------------------------------------------------------------------------------------------

void UChunkStreamingSubsystem::UpdateStreaming()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !GraphAsset)
	{
		return;
	}

	// 心跳日志（调试）：前 5 次全量，之后每 10 秒摘要
	{
		static int32 TickCount = 0;
		const double Now = FPlatformTime::Seconds();
		static double LastHeartbeat = 0.0;
		++TickCount;
		if (TickCount <= 5 || Now - LastHeartbeat > 10.0)
		{
			LastHeartbeat = Now;
			FString LoadedNames;
			for (const FName& LC : LoadedChunks)
			{
				const FString Short = LC.ToString();
				const int32 Slash = Short.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				LoadedNames += (Slash != INDEX_NONE ? Short.RightChop(Slash + 1) : Short) + TEXT(",");
			}
			UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 心跳 tick=%d Player=%s Focus=%s Loaded=[%s] Loading=%d Unloading=%d"),
				TickCount, *PlayerChunk.ToString(), *StreamingChunk.ToString(),
				*LoadedNames, LoadingChunks.Num(), UnloadingChunks.Num());
		}
	}

	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	if (Settings && !Settings->bEnableStreaming)
	{
		return;
	}
	const float LookAhead = Settings ? Settings->LookAheadDistance : 0.f;
	const float Hysteresis = Settings ? Settings->HysteresisSeconds : 0.f;

	// 对账：认领引擎侧已加载/正在加载的区块。
	// 场景：Open Level 重新进入时，子关卡由引擎异步加载（bInitiallyLoaded），
	// OnWorldBeginPlay 时尚未加载完、初始化收集不到；加载完成后在此统一认领，
	// 否则这些关卡会游离在流送管理之外、永远不卸载（"全部关卡一起加载出来"）。
	if (GraphAsset)
	{
		for (const FChunkInfo& Info : GraphAsset->Chunks)
		{
			ULevelStreaming* SL = GetStreamingLevelFor(Info.LevelName);
			if (!SL)
			{
				continue;
			}
			const CHUNK_LSTATE_TYPE LState = CHUNK_GET_LSTATE(SL);
			if (SL->IsLevelLoaded())
			{
				if (UnloadingChunks.Contains(Info.LevelName))
				{
					continue; // 正在卸载中，等待完成回调
				}
				const bool bWasLoading = LoadingChunks.Remove(Info.LevelName) > 0;
				if (Info.Category == EChunkCategory::Background)
				{
					if (!LoadedBackgrounds.Contains(Info.LevelName))
					{
						LoadedBackgrounds.Add(Info.LevelName);
						if (!bWasLoading)
						{
							UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 认领引擎加载的背景区块 %s"), *Info.LevelName.ToString());
						}
					}
				}
				else if (!LoadedChunks.Contains(Info.LevelName))
				{
					LoadedChunks.Add(Info.LevelName);
					RestoreChunkState(Info.LevelName);
					UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 认领引擎加载的区块 %s%s"), *Info.LevelName.ToString(), bWasLoading ? TEXT("（加载完成）") : TEXT(""));
				}
			}
			else if (LState == CHUNK_LSTATE_LOADING)
			{
				if (!LoadingChunks.Contains(Info.LevelName) && !UnloadingChunks.Contains(Info.LevelName))
				{
					LoadingChunks.Add(Info.LevelName);
					UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 跟随引擎加载中的区块 %s"), *Info.LevelName.ToString());
				}
			}
			else if (LState == CHUNK_LSTATE_UNLOADED)
			{
				// 反向对账：引擎侧已卸载而我们仍记账（保持状态一致）
				if (UnloadingChunks.Remove(Info.LevelName) > 0)
				{
					UnloadStartTimes.Remove(Info.LevelName);
				}
				LoadingChunks.Remove(Info.LevelName);
				LoadedChunks.Remove(Info.LevelName);
				LoadedBackgrounds.Remove(Info.LevelName);
			}
			else if (LState == CHUNK_LSTATE_FAILED)
			{
				LoadingChunks.Remove(Info.LevelName);
				LoadedChunks.Remove(Info.LevelName);
				LoadedBackgrounds.Remove(Info.LevelName);
				UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 区块 %s 加载失败"), *Info.LevelName.ToString());
			}
		}
	}

	// 瞬移待完成：目标块已加载则执行；超时（15s）则取消过渡，恢复正常卸载
	if (!PendingTargetChunk.IsNone())
	{
		if (IsChunkLoaded(PendingTargetChunk))
		{
			CompleteTeleport();
		}
		else if (FPlatformTime::Seconds() - TeleportStartTime > 15.0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] TeleportToChunk: 目标区块 %s 加载超时，已取消过渡模式。"), *PendingTargetChunk.ToString());
			bTransitionMode = false;
			TransitionSet.Reset();
			PendingTargetChunk = NAME_None;
			PendingTeleportLocation = FVector::ZeroVector;
		}
	}

	// 卸载看门狗：15 秒未完成卸载的区块告警并恢复状态（防止卡死）
	for (const FName& C : UnloadingChunks.Array())
	{
		const double* Start = UnloadStartTimes.Find(C);
		if (Start && FPlatformTime::Seconds() - *Start > 15.0)
		{
			ULevelStreaming* ULS = GetStreamingLevelFor(C);
			UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 区块 %s 卸载超时（可能被设为 Always Loaded 或仍在使用中），已恢复状态。"), *C.ToString());
			UnloadingChunks.Remove(C);
			UnloadStartTimes.Remove(C);
			if (ULS && ULS->IsLevelLoaded())
			{
				LoadedChunks.Add(C); // 实际仍加载，恢复记账
			}
		}
	}

	// 玩家所在区块（事件，3D 包含判定）
	FName NewPlayerChunk = NAME_None;
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		NewPlayerChunk = GraphAsset ? GraphAsset->FindChunkAtLocationSmart(Pawn->GetActorLocation(), PlayerChunk) : NAME_None;
	}
	else
	{
		NewPlayerChunk = FindChunkAtX(GetPawnAxis());
	}
	if (NewPlayerChunk != PlayerChunk)
	{
		const FName Old = PlayerChunk;
		PlayerChunk = NewPlayerChunk;
		OnPlayerExitedChunk.Broadcast(Old, PlayerChunk);
		OnPlayerEnteredChunk.Broadcast(PlayerChunk, Old);
	}

	// 流送焦点（参考位置 + 前向预加载），3D 包含判定
	FVector RefLoc = GetStreamingReferenceLocation();
	FVector FocusLoc = RefLoc;
	if (GraphAsset->MovementAxis == EAxis::Y)
	{
		FocusLoc.Y = RefLoc.Y + GetForwardSign() * LookAhead;
	}
	else
	{
		FocusLoc.X = RefLoc.X + GetForwardSign() * LookAhead;
	}
	FName NewStreaming = GraphAsset ? GraphAsset->FindChunkAtLocationSmart(FocusLoc, StreamingChunk) : NAME_None;
	if (NewStreaming.IsNone() && GraphAsset)
	{
		// 判定点不在任何区块范围内：回退到最近玩法区块（世界尽头/预加载越界属正常情况，静默处理）
		NewStreaming = FindNearestChunk(GetStreamingReferenceAxis());
		// 仅当玩家自身位置也不在任何区块内时才警告（真正的坐标不匹配信号）
		if (PlayerChunk.IsNone())
		{
			const double Now = FPlatformTime::Seconds();
			if (Now - LastOutOfRangeWarnTime > 5.0)
			{
				LastOutOfRangeWarnTime = Now;
				UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 玩家位置 X=%.1f 不在任何区块范围内，已回退到最近区块 %s——请检查图资产 XRange 与玩家/存档坐标是否一致。"),
					GetStreamingReferenceAxis(), *NewStreaming.ToString());
			}
		}
	}
	// 隔离块（无连接）不能通过"预加载点"抢占焦点：仅当玩家真正在其内部时才成为流送焦点
	if (!NewPlayerChunk.IsNone() && NewStreaming != NewPlayerChunk)
	{
		TArray<FName> FocusNeighbors;
		GraphAsset->GetNeighbors(NewStreaming, FocusNeighbors);
		if (FocusNeighbors.Num() == 0)
		{
			NewStreaming = NewPlayerChunk;
		}
	}
	// 连通分量隔离：焦点不能通过预加载点跨到"不相连的区块组"（不同分组永不共载，传送除外）
	if (!NewStreaming.IsNone() && !StreamingChunk.IsNone() && NewStreaming != StreamingChunk && NewStreaming != NewPlayerChunk)
	{
		if (GetComponentId(NewStreaming) != GetComponentId(StreamingChunk))
		{
			NewStreaming = StreamingChunk;
		}
	}

	if (NewStreaming != StreamingChunk)
	{
		// 瞬移/首次判定（目标不是当前块邻居）立即切换，跳过迟滞；边界抖动才走迟滞
		bool bIsNeighbor = false;
		if (!StreamingChunk.IsNone() && GraphAsset)
		{
			TArray<FName> Neighbors;
			GraphAsset->GetNeighbors(StreamingChunk, Neighbors);
			bIsNeighbor = Neighbors.Contains(NewStreaming);
		}
		const bool bSkipHysteresis = StreamingChunk.IsNone() || !bIsNeighbor;
		if (bSkipHysteresis)
		{
			StreamingChunk = NewStreaming;
			HysteresisCandidate = NAME_None;
			HysteresisTimer = 0.f;
		}
		else if (NewStreaming == HysteresisCandidate)
		{
			HysteresisTimer += Settings ? Settings->UpdateInterval : 0.2f;
			if (HysteresisTimer >= Hysteresis)
			{
				StreamingChunk = NewStreaming;
				HysteresisCandidate = NAME_None;
				HysteresisTimer = 0.f;
			}
		}
		else
		{
			HysteresisCandidate = NewStreaming;
			HysteresisTimer = 0.f;
		}
	}
	else
	{
		HysteresisCandidate = NAME_None;
		HysteresisTimer = 0.f;
	}

	Reconcile(StreamingChunk);

	if (bDebugDraw)
	{
		DebugDraw();
	}
}

void UChunkStreamingSubsystem::Reconcile(FName InStreamingChunk)
{
	if (!GraphAsset)
	{
		return;
	}

	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	const int32 KeepBehind = Settings ? Settings->KeepBehindCount : 1;
	const bool bPreloadNextHop = Settings ? Settings->bPreloadNextHop : true;

	TSet<FName> Desired;
	if (!InStreamingChunk.IsNone())
	{
		Desired.Add(InStreamingChunk);
		TArray<FName> Neighbors;
		GraphAsset->GetNeighbors(InStreamingChunk, Neighbors);
		Desired.Append(Neighbors);

		if (bPreloadNextHop && Neighbors.Num() > 0)
		{
			// 沿前进方向取第一个邻居的邻居（二级预加载）
			const float Fwd = GetForwardSign();
			FName BestNext = NAME_None;
			float BestDist = TNumericLimits<float>::Max();
			const FVector2D CurRange = GetChunkRange(InStreamingChunk);
			const float CurCenter = (CurRange.X + CurRange.Y) * 0.5f;
			for (const FName& N : Neighbors)
			{
				const FVector2D NR = GetChunkRange(N);
				const float NC = (NR.X + NR.Y) * 0.5f;
				const float D = (NC - CurCenter) * Fwd;
				if (D > 0.f && D < BestDist)
				{
					BestDist = D;
					BestNext = N;
				}
			}
			if (!BestNext.IsNone())
			{
				TArray<FName> NextNeighbors;
				GraphAsset->GetNeighbors(BestNext, NextNeighbors);
				Desired.Append(NextNeighbors);
			}
		}
	}

	// 常驻块
	for (const FChunkInfo& Info : GraphAsset->Chunks)
	{
		if (Info.Category == EChunkCategory::Persistent)
		{
			Desired.Add(Info.LevelName);
		}
	}

	// 背景块（OR：任一引用它的玩法块在需求集或已加载集中）
	TSet<FName> DesiredBackgrounds;
	for (const FChunkInfo& BG : GraphAsset->Chunks)
	{
		if (BG.Category != EChunkCategory::Background)
		{
			continue;
		}
		if (BG.bVisibleFromAll)
		{
			DesiredBackgrounds.Add(BG.LevelName);
			continue;
		}
		for (const FName& Owner : BG.VisibleFromChunks)
		{
			if (Desired.Contains(Owner) || LoadedChunks.Contains(Owner))
			{
				DesiredBackgrounds.Add(BG.LevelName);
				break;
			}
		}
	}

	// 瞬移过渡集
	if (bTransitionMode)
	{
		Desired.Append(TransitionSet);
	}

	// 预加载传送等待中的区块：需求集中保留其及邻居（保持加载直至传送完成）
	for (const FName& PendingChunk : PendingPreloadChunks)
	{
		Desired.Add(PendingChunk);
		TArray<FName> PendingNeighbors;
		GraphAsset->GetNeighbors(PendingChunk, PendingNeighbors);
		Desired.Append(PendingNeighbors);
	}

	// 调试：打印需求集
	{
		static int32 DesiredLogCounter = 0;
		if ((++DesiredLogCounter % 20) == 1)
		{
			FString DesiredNames;
			for (const FName& DC : Desired)
			{
				const FString Short = DC.ToString();
				const int32 Slash = Short.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				DesiredNames += (Slash != INDEX_NONE ? Short.RightChop(Slash + 1) : Short) + TEXT(",");
			}
			UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] Desired=[%s] (焦点=%s)"), *DesiredNames, *InStreamingChunk.ToString());
		}
	}

	// 加载（Z 屏蔽的块不加载）
	for (const FName& C : Desired)
	{
		if (!LoadedChunks.Contains(C) && !LoadingChunks.Contains(C) && !UnloadingChunks.Contains(C))
		{
			StartLoadChunk(C);
		}
	}
	for (const FName& C : DesiredBackgrounds)
	{
		if (!LoadedBackgrounds.Contains(C) && !LoadingChunks.Contains(C) && !UnloadingChunks.Contains(C))
		{
			StartLoadChunk(C);
		}
	}

	// 卸载（过渡模式期间不卸载）
	if (bTransitionMode)
	{
		return;
	}

	// 玩法块：未需求的卸载。
	// 仅保留"可步行回头"的块：有连接（非隔离）且距参考点不超过 KeepBehindDistance，
	// 最多保留 KeepBehindCount 个；隔离区块与远距传送离开的块一律卸载。
	TArray<FName> Undesired;
	for (const FName& C : LoadedChunks)
	{
		if (!Desired.Contains(C))
		{
			Undesired.Add(C);
		}
	}
	if (Undesired.Num() > 0)
	{
		const float RefX = GetStreamingReferenceAxis();
		const float KeepDist = Settings ? Settings->KeepBehindDistance : 5000.f;
		Undesired.Sort([this, RefX](const FName& A, const FName& B)
		{
			const FVector2D AR = GetChunkRange(A);
			const FVector2D BR = GetChunkRange(B);
			const float DA = FMath::Abs((AR.X + AR.Y) * 0.5f - RefX);
			const float DB = FMath::Abs((BR.X + BR.Y) * 0.5f - RefX);
			return DA < DB;
		});
		int32 Kept = 0;
		for (int32 i = 0; i < Undesired.Num(); ++i)
		{
			const FName& C = Undesired[i];
			if (ULevelStreaming* ULS = GetStreamingLevelFor(C))
			{
				if (ULS->ShouldBeAlwaysLoaded())
				{
					continue; // Always Loaded：引擎不会卸载它，跳过
				}
			}
			TArray<FName> Neighbors;
			GraphAsset->GetNeighbors(C, Neighbors);
			const bool bWalkable = Neighbors.Num() > 0; // 有连接 = 可步行回头
			const FVector2D R = GetChunkRange(C);
			const float Dist = FMath::Abs((R.X + R.Y) * 0.5f - RefX);
			// 仅保留与当前焦点同属一个连通分量的块（不同分量的区块组永不共载）
			const bool bSameComponent = InStreamingChunk.IsNone() || GetComponentId(C) == GetComponentId(InStreamingChunk);
			if (bWalkable && bSameComponent && Kept < KeepBehind && Dist <= KeepDist)
			{
				++Kept;
				static int32 KeepLogCounter = 0;
				if ((++KeepLogCounter % 15) == 1)
				{
					UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] KeepBehind 保留未需求区块 %s（距参考轴 %.0f ≤ %.0f，已保留 %d/%d）"),
						*C.ToString(), Dist, KeepDist, Kept, KeepBehind);
				}
				continue; // 保留
			}
			UnloadChunk(C, false);
		}
	}

	// 背景块：不再被引用则卸载
	for (const FName& C : LoadedBackgrounds)
	{
		if (!DesiredBackgrounds.Contains(C))
		{
			UnloadChunk(C, true);
		}
	}
}

// ---------------------------------------------------------------------------------------------

void UChunkStreamingSubsystem::StartLoadChunk(FName ChunkName)
{
	if (ChunkName.IsNone() || LoadingChunks.Contains(ChunkName))
	{
		return;
	}

	ULevelStreaming* SL = GetStreamingLevelFor(ChunkName);
	if (!SL)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - LastMissingLevelWarnTime > 2.0)
		{
			LastMissingLevelWarnTime = Now;
			UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] 区块 '%s' 不是当前世界的流送子关卡：请在主关卡 Levels 面板中添加它（或重新 Refresh Bounds 同步图资产）。"), *ChunkName.ToString());
		}
		return;
	}
	if (SL->IsLevelLoaded())
	{
		if (IsGameplayChunk(ChunkName))
		{
			LoadedChunks.Add(ChunkName);
		}
		else
		{
			LoadedBackgrounds.Add(ChunkName);
		}
		return;
	}
	if (CHUNK_GET_LSTATE(SL) == CHUNK_LSTATE_LOADING)
	{
		// 引擎已在加载（如 Open Level 后的初始异步加载）：跟随，不重复请求
		LoadingChunks.Add(ChunkName);
		return;
	}

	LoadingChunks.Add(ChunkName);
	OnChunkLoadStarted.Broadcast(ChunkName);

	const FName LoadName = FName(*SL->GetWorldAssetPackageName());
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 请求加载区块 %s (包名 %s)"), *ChunkName.ToString(), *LoadName.ToString());
	const int32 UUID = (int32)(GetTypeHash(ChunkName) & 0x7fffffff);
	UGameplayStatics::LoadStreamLevel(GetWorld(), LoadName, /*bMakeVisibleAfterLoad*/ true, /*bShouldBlockOnLoad*/ false,
		FLatentActionInfo(0, UUID, TEXT("OnChunkLatentLoadFinished"), this));
}

void UChunkStreamingSubsystem::UnloadChunk(FName ChunkName, bool bBackground)
{
	if (ChunkName.IsNone() || UnloadingChunks.Contains(ChunkName))
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 请求卸载区块 %s (背景=%d)"), *ChunkName.ToString(), (int32)bBackground);
	UnloadStartTimes.Add(ChunkName, FPlatformTime::Seconds());
	if (!bBackground)
	{
		SaveChunkState(ChunkName);
		LoadedChunks.Remove(ChunkName);
		RestoredChunks.Remove(ChunkName);
	}
	else
	{
		LoadedBackgrounds.Remove(ChunkName);
	}
	UnloadingChunks.Add(ChunkName);

	ULevelStreaming* SL = GetStreamingLevelFor(ChunkName);
	const FName LoadName = SL ? FName(*SL->GetWorldAssetPackageName()) : ChunkName;
	const int32 UUID = (int32)((GetTypeHash(ChunkName) & 0x7fffffff) ^ 0x1f2e3d4c);
	UGameplayStatics::UnloadStreamLevel(GetWorld(), LoadName,
		FLatentActionInfo(0, UUID, TEXT("OnChunkLatentUnloadFinished"), this), /*bShouldBlockOnUnload*/ false);
}

void UChunkStreamingSubsystem::OnChunkLatentLoadFinished()
{
	for (const FName& C : LoadingChunks.Array())
	{
		ULevelStreaming* SL = GetStreamingLevelFor(C);
		if (SL && SL->IsLevelLoaded())
		{
			LoadingChunks.Remove(C);
			if (IsGameplayChunk(C))
			{
				LoadedChunks.Add(C);
				RestoreChunkState(C);
				UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] 区块 %s 加载完成"), *C.ToString());
				OnChunkLoadFinished.Broadcast(C);
			}
			else
			{
				LoadedBackgrounds.Add(C);
			}
		}
	}
}

void UChunkStreamingSubsystem::OnChunkLatentUnloadFinished()
{
	for (const FName& C : UnloadingChunks.Array())
	{
		ULevelStreaming* SL = GetStreamingLevelFor(C);
		if (!SL || !SL->IsLevelLoaded())
		{
			UnloadingChunks.Remove(C);
		}
	}
}

// ---------------------------------------------------------------------------------------------

void UChunkStreamingSubsystem::SaveChunkState(FName ChunkName)
{
	UWorld* World = GetWorld();
	if (!World || !IsGameplayChunk(ChunkName))
	{
		return;
	}
	ULevelStreaming* SL = GetStreamingLevelFor(ChunkName);
	if (!SL || !SL->GetLoadedLevel())
	{
		return;
	}
	if (UChunkStateStore* Store = World->GetGameInstance()->GetSubsystem<UChunkStateStore>())
	{
		Store->SaveChunkFromLevel(ChunkName, SL->GetLoadedLevel());
	}
}

void UChunkStreamingSubsystem::RestoreChunkState(FName ChunkName)
{
	if (RestoredChunks.Contains(ChunkName))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !IsGameplayChunk(ChunkName))
	{
		return;
	}
	ULevelStreaming* SL = GetStreamingLevelFor(ChunkName);
	if (!SL || !SL->GetLoadedLevel())
	{
		return;
	}
	if (UChunkStateStore* Store = World->GetGameInstance()->GetSubsystem<UChunkStateStore>())
	{
		Store->RestoreChunkToLevel(ChunkName, SL->GetLoadedLevel());
	}
	RestoredChunks.Add(ChunkName);
}

// ---------------------------------------------------------------------------------------------

/** 预加载传送的延迟动作（引擎 Latent 机制，类似 Delay）。 */
class FChunkPreloadTeleportAction : public FPendingLatentAction
{
public:
	FChunkPreloadTeleportAction(UChunkStreamingSubsystem* InSub, AActor* InActor, const FVector& InLocation, FName InChunk, float InTimeout, const FLatentActionInfo& InLatentInfo)
		: Sub(InSub)
		, Actor(InActor)
		, Location(InLocation)
		, Chunk(InChunk)
		, Timeout(FMath::Max(1.f, InTimeout))
		, LatentInfo(InLatentInfo)
	{}

	virtual void UpdateOperation(FLatentResponse& Response) override
	{
		if (bFinished)
		{
			Response.DoneIf(true);
			return;
		}
		AActor* A = Actor.Get();
		UChunkStreamingSubsystem* S = Sub.Get();
		if (!A || !S)
		{
			Finish(Response);
			return;
		}
		const bool bLoaded = S->IsChunkLoaded(Chunk);
		const bool bTimeout = FPlatformTime::Seconds() - StartTime > Timeout;
		if (bLoaded || bTimeout)
		{
			if (bTimeout && !bLoaded)
			{
				UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] PreloadTeleport 超时（%.1fs），区块 %s 未加载完成，已强制传送。"),
					Timeout, *Chunk.ToString());
			}
			A->SetActorLocation(Location, /*bSweep*/ false, /*OutHit*/ nullptr, ETeleportType::TeleportPhysics);
			Finish(Response);
		}
	}

	virtual void NotifyObjectDestroyed() override { Cleanup(); }
	virtual void NotifyActionAborted() override { Cleanup(); }

private:
	void Finish(FLatentResponse& Response)
	{
		if (bFinished)
		{
			return;
		}
		bFinished = true;
		Cleanup();
		Response.FinishAndTriggerIf(true, LatentInfo.ExecutionFunction, LatentInfo.Linkage, LatentInfo.CallbackTarget);
	}

	void Cleanup()
	{
		if (UChunkStreamingSubsystem* S = Sub.Get())
		{
			S->UnregisterPendingPreload(Chunk);
		}
	}

	TWeakObjectPtr<UChunkStreamingSubsystem> Sub;
	TWeakObjectPtr<AActor> Actor;
	FVector Location;
	FName Chunk;
	float Timeout;
	FLatentActionInfo LatentInfo;
	double StartTime = FPlatformTime::Seconds();
	bool bFinished = false;
};

void UChunkStreamingSubsystem::PreloadTeleportToLocation(const UObject* WorldContextObject, AActor* TargetActor, FVector TargetLocation, float TimeoutSeconds, FLatentActionInfo LatentInfo)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return;
	}
	UChunkStreamingSubsystem* Sub = World->GetSubsystem<UChunkStreamingSubsystem>();
	if (!Sub)
	{
		return;
	}

	AActor* Actor = TargetActor ? TargetActor : UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Actor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] PreloadTeleportToLocation: 没有可传送的角色。"));
		return;
	}

	const FName TargetChunk = Sub->ResolveChunkAtLocation(TargetLocation);
	if (TargetChunk.IsNone() || Sub->IsChunkLoaded(TargetChunk))
	{
		// 没有区块或目标区块已加载：立即传送并立即完成延迟节点
		Actor->SetActorLocation(TargetLocation, /*bSweep*/ false, /*OutHit*/ nullptr, ETeleportType::TeleportPhysics);
		if (LatentInfo.CallbackTarget)
		{
			if (UFunction* ExecFn = LatentInfo.CallbackTarget->FindFunction(LatentInfo.ExecutionFunction))
			{
				LatentInfo.CallbackTarget->ProcessEvent(ExecFn, &LatentInfo.Linkage);
			}
		}
		return;
	}

	Sub->RegisterPendingPreload(TargetChunk);
	Sub->StartLoadChunk(TargetChunk);
	World->GetLatentActionManager().AddNewAction(LatentInfo.CallbackTarget, LatentInfo.UUID,
		new FChunkPreloadTeleportAction(Sub, Actor, TargetLocation, TargetChunk, TimeoutSeconds, LatentInfo));
}

FName UChunkStreamingSubsystem::ResolveChunkAtLocation(const FVector& Location) const
{
	if (!GraphAsset)
	{
		return NAME_None;
	}
	FName Chunk = GraphAsset->FindChunkAtLocationSmart(Location, StreamingChunk);
	if (Chunk.IsNone())
	{
		Chunk = FindNearestChunk(GraphAsset->GetAxisCoord(Location));
	}
	return Chunk;
}

void UChunkStreamingSubsystem::RegisterPendingPreload(FName ChunkName)
{
	PendingPreloadChunks.Add(ChunkName);
}

void UChunkStreamingSubsystem::UnregisterPendingPreload(FName ChunkName)
{
	PendingPreloadChunks.Remove(ChunkName);
}

void UChunkStreamingSubsystem::TeleportToChunk(const UObject* WorldContextObject, FName TargetChunk, FVector TargetLocation)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	UChunkStreamingSubsystem* Sub = World ? World->GetSubsystem<UChunkStreamingSubsystem>() : nullptr;
	if (!Sub)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] TeleportToChunk: 无法获取流送子系统。"));
		return;
	}
	if (!Sub->GraphAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] TeleportToChunk: no graph asset configured."));
		return;
	}
	const FChunkInfo* Info = Sub->GraphAsset->FindChunkInfoPtr(TargetChunk);
	if (!Info)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] TeleportToChunk: chunk '%s' not found in graph."), *TargetChunk.ToString());
		return;
	}

	// 代际号：作废在途的旧加载回调
	++Sub->TeleportGeneration;

	Sub->bTransitionMode = true;
	Sub->TransitionSet.Reset();
	Sub->TransitionSet.Add(TargetChunk);
	TArray<FName> Neighbors;
	Sub->GraphAsset->GetNeighbors(TargetChunk, Neighbors);
	Sub->TransitionSet.Append(Neighbors);

	Sub->PendingTargetChunk = TargetChunk;
	Sub->PendingTeleportLocation = TargetLocation;
	Sub->TeleportStartTime = FPlatformTime::Seconds();

	if (Sub->IsChunkLoaded(TargetChunk))
	{
		Sub->CompleteTeleport();
	}
	else
	{
		Sub->StartLoadChunk(TargetChunk);
		// 目标块不是流送子关卡（加载请求未发出）→ 立即取消过渡，避免卸载被永久禁用
		if (!Sub->LoadingChunks.Contains(TargetChunk) && !Sub->IsChunkLoaded(TargetChunk))
		{
			UE_LOG(LogTemp, Warning, TEXT("[ChunkStreaming] TeleportToChunk: 目标区块 %s 无法加载（不是当前世界的流送子关卡），已取消过渡。"), *TargetChunk.ToString());
			Sub->bTransitionMode = false;
			Sub->TransitionSet.Reset();
			Sub->PendingTargetChunk = NAME_None;
			Sub->PendingTeleportLocation = FVector::ZeroVector;
		}
	}
}

void UChunkStreamingSubsystem::CompleteTeleport()
{
	const FName Target = PendingTargetChunk;
	// 落点：指定位置 > 目标块 PlayerStart > 区块包围盒中心
	FVector Loc = PendingTeleportLocation;
	PendingTargetChunk = NAME_None;
	PendingTeleportLocation = FVector::ZeroVector;
	bTransitionMode = false;
	TransitionSet.Reset();

	if (Loc.IsNearlyZero())
	{
		ULevelStreaming* SL = GetStreamingLevelFor(Target);
		if (SL && SL->GetLoadedLevel())
		{
			for (AActor* Actor : SL->GetLoadedLevel()->Actors)
			{
				if (Actor && Actor->IsA(APlayerStart::StaticClass()))
				{
					Loc = Actor->GetActorLocation();
					break;
				}
			}
		}
	}
	if (Loc.IsNearlyZero() && GraphAsset)
	{
		const FChunkInfo* Info = GraphAsset->FindChunkInfoPtr(Target);
		if (Info && Info->WorldBounds.IsValid)
		{
			Loc = Info->WorldBounds.GetCenter();
			Loc.Z += 200.f;
		}
	}

	if (!Loc.IsNearlyZero())
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
		{
			Pawn->SetActorLocation(Loc, /*bSweep*/ false, /*OutHit*/ nullptr, ETeleportType::TeleportPhysics);
		}
	}

	// 同步区块状态与事件
	if (PlayerChunk != Target)
	{
		const FName Old = PlayerChunk;
		PlayerChunk = Target;
		OnPlayerExitedChunk.Broadcast(Old, PlayerChunk);
		OnPlayerEnteredChunk.Broadcast(PlayerChunk, Old);
	}
	StreamingChunk = Target;
	HysteresisCandidate = NAME_None;
	HysteresisTimer = 0.f;

	// 目标块状态恢复（若本周期尚未恢复）
	RestoreChunkState(Target);
}

// ---------------------------------------------------------------------------------------------

void UChunkStreamingSubsystem::ForceReconcile()
{
	Reconcile(StreamingChunk);
}

void UChunkStreamingSubsystem::SetDebugDraw(bool bEnabled)
{
	bDebugDraw = bEnabled;
	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] Debug draw %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void UChunkStreamingSubsystem::DebugDraw()
{
	UWorld* World = GetWorld();
	if (!World || !GraphAsset)
	{
		return;
	}

	for (const FChunkInfo& C : GraphAsset->Chunks)
	{
		if (!C.WorldBounds.IsValid)
		{
			continue;
		}
		FColor Col = FColor::Blue;
		if (C.Category == EChunkCategory::Background)
		{
			Col = FColor(130, 130, 130);
		}
		else if (C.Category == EChunkCategory::Persistent)
		{
			Col = FColor::Green;
		}
		if (C.LevelName == StreamingChunk)
		{
			Col = FColor::Yellow;
		}
		if (C.LevelName == PlayerChunk)
		{
			Col = FColor::Red;
		}
		const bool bLoaded = LoadedChunks.Contains(C.LevelName) || LoadedBackgrounds.Contains(C.LevelName);
		if (bLoaded)
		{
			// 已加载：实心
			DrawDebugSolidBox(World, C.WorldBounds.GetCenter(), C.WorldBounds.GetExtent(), Col, /*bPersistent*/ false, 0.35f, 0);
		}
		else
		{
			// 未加载：线框
			DrawDebugBox(World, C.WorldBounds.GetCenter(), C.WorldBounds.GetExtent(), Col, /*bPersistentLines*/ false, 0.35f, 0, 2.f);
		}
	}

	// 当前流送焦点块的连接线
	if (!StreamingChunk.IsNone())
	{
		TArray<FName> Neighbors;
		GraphAsset->GetNeighbors(StreamingChunk, Neighbors);
		const FChunkInfo* Cur = GraphAsset->FindChunkInfoPtr(StreamingChunk);
		if (Cur && Cur->WorldBounds.IsValid)
		{
			const FVector CurCenter = Cur->WorldBounds.GetCenter();
			for (const FName& N : Neighbors)
			{
				const FChunkInfo* NInfo = GraphAsset->FindChunkInfoPtr(N);
				if (NInfo && NInfo->WorldBounds.IsValid)
				{
					DrawDebugLine(World, CurCenter, NInfo->WorldBounds.GetCenter(), FColor::Cyan, false, 0.35f, 0, 1.5f);
				}
			}
		}
	}

	// 流送参考点（青球=相机 / 绿球=Pawn）与预加载方向（品红线）
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	const bool bPawnRef = Settings && Settings->bUsePawnAsStreamingSource;
	const float RefX = GetStreamingReferenceAxis();
	const float Fwd = GetForwardSign();
	const float LookAhead = Settings ? Settings->LookAheadDistance : 0.f;
	FVector RefLoc = FVector(RefX, 0.f, 0.f);
	APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);
	if (bPawnRef && Pawn)
	{
		RefLoc = Pawn->GetActorLocation();
	}
	else if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0))
	{
		RefLoc = Cam->GetCameraLocation();
	}
	DrawDebugSphere(World, RefLoc, 80.f, 12, bPawnRef ? FColor::Green : FColor::Cyan, false, 0.35f, 0, 2.f);
	FVector TargetLoc = RefLoc;
	if (GraphAsset->MovementAxis == EAxis::Y)
	{
		TargetLoc.Y = RefX + Fwd * LookAhead;
	}
	else
	{
		TargetLoc.X = RefX + Fwd * LookAhead;
	}
	DrawDebugLine(World, RefLoc, TargetLoc, FColor::Magenta, false, 0.35f, 0, 2.f);

	if (GEngine)
	{
		FString Status = FString::Printf(TEXT("[ChunkStreaming] PlayerChunk=%s StreamingChunk=%s Loaded=%d Loading=%d BG=%d"),
			*PlayerChunk.ToString(), *StreamingChunk.ToString(), LoadedChunks.Num(), LoadingChunks.Num(), LoadedBackgrounds.Num());
		if (ChunkStreamingMap.Num() == 0)
		{
			Status += TEXT(" | 主关卡没有流送子关卡！请在 Levels 面板添加");
		}
		else if (StreamingChunk.IsNone() && PlayerChunk.IsNone())
		{
			Status += TEXT(" | 参考点不在任何区块范围内");
		}
		GEngine->AddOnScreenDebugMessage(0xCC01, 0.4f, FColor::Cyan, Status);
	}
}
