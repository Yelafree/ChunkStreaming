#include "ChunkEnemyManager.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#include "ChunkEnemySpawnerComponent.h"
#include "ChunkStreamingSettings.h"

UChunkEnemyManager* UChunkEnemyManager::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UChunkEnemyManager>() : nullptr;
}

void UChunkEnemyManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UChunkEnemyManager::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CheckTimerHandle);
	}
	Super::Deinitialize();
}

bool UChunkEnemyManager::RegisterSpawn(const FString& Key, const FTransform& HomeTransform, TSoftClassPtr<AActor> EnemyClass)
{
	if (Key.IsEmpty() || EnemyClass.IsNull())
	{
		return true; // 无效注册：按"已死"处理（调用方销毁占位）
	}
	const bool bNewRecord = !Records.Contains(Key);
	FChunkEnemySpawnRecord& Rec = Records.FindOrAdd(Key);
	// 已有记录（如 Open Level 后占位重新收编）：保留死亡状态与最后位置
	Rec.HomeLocation = HomeTransform.GetLocation();
	Rec.HomeRotation = HomeTransform.Rotator();
	Rec.EnemyClass = EnemyClass;
	if (bNewRecord)
	{
		Rec.LastKnownLocation = Rec.HomeLocation;
		Rec.LastKnownRotation = Rec.HomeRotation;
	}

	// 确保检测定时器运行（Open Level 后新世界由新占位收编重新启动）
	UWorld* World = GetWorld();
	if (World && !World->GetTimerManager().IsTimerActive(CheckTimerHandle))
	{
		const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
		const float Interval = FMath::Max(0.1f, Settings ? Settings->EnemyCheckInterval : 0.5f);
		World->GetTimerManager().SetTimer(CheckTimerHandle, this, &UChunkEnemyManager::TickSpawns, Interval, true);
	}
	return Rec.bDead;
}

void UChunkEnemyManager::MarkDead(const FString& Key)
{
	if (FChunkEnemySpawnRecord* Rec = Records.Find(Key))
	{
		Rec->bDead = true;
		Rec->ActiveEnemy.Reset();
	}
}

void UChunkEnemyManager::ClearDead(const FString& Key)
{
	if (FChunkEnemySpawnRecord* Rec = Records.Find(Key))
	{
		Rec->bDead = false;
	}
}

void UChunkEnemyManager::ClearAll()
{
	for (auto& Pair : Records)
	{
		Pair.Value.bDead = false;
		Pair.Value.ActiveEnemy.Reset();
	}
}

void UChunkEnemyManager::TickSpawns()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		return;
	}
	APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	if (!Player)
	{
		return;
	}
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	if (!Settings || !Settings->bEnableEnemySpawning)
	{
		return;
	}
	const float SpawnDist = Settings->EnemySpawnDistance;
	const float DespawnDist = Settings->EnemyDespawnDistance;
	const bool bZCheck = Settings->bEnableEnemyZCheck;
	const float ZDelayDist = Settings->EnemyZDelayDistance;
	const float ZDelaySeconds = Settings->EnemyZDelaySeconds;
	const float ZImmediateDist = Settings->EnemyZImmediateDistance;

	const FVector PlayerLoc = Player->GetActorLocation();

	for (auto& Pair : Records)
	{
		FChunkEnemySpawnRecord& Rec = Pair.Value;
		if (Rec.bDead)
		{
			// 永久死亡：什么都不做（占位不会再生成）
			continue;
		}

		// 有在场活体：先检查是否已被销毁（= 玩家击杀或其他销毁 → 记永久死亡）
		if (Rec.ActiveEnemy.IsValid())
		{
			AActor* Active = Rec.ActiveEnemy.Get();
			if (!IsValid(Active))
			{
				// 活体已被外部销毁：视为玩家击杀，记永久死亡
				Rec.bDead = true;
				Rec.ActiveEnemy.Reset();
				UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 的活体已被销毁 → 记永久死亡"), *Pair.Key);
				continue;
			}
			// 持续记录活体最后位置（回收后重生用，保持位置连续）
			Rec.LastKnownLocation = Active->GetActorLocation();
			Rec.LastKnownRotation = Active->GetActorRotation();

			// Z 轴检测：玩家与活体高度差过大 → 延迟/立即回收（防跳跃误触发）
			if (bZCheck)
			{
				const float ZDiff = FMath::Abs(PlayerLoc.Z - Active->GetActorLocation().Z);
				if (ZDiff <= ZDelayDist)
				{
					ZDelayStart.Remove(Pair.Key); // 回到同一高度层：清除计时
				}
				else
				{
					bool bRecycleByZ = false;
					if (ZDiff >= ZImmediateDist)
					{
						bRecycleByZ = true; // 大幅高度差：立即回收
					}
					else
					{
						const double NowZ = FPlatformTime::Seconds();
						double* T = ZDelayStart.Find(Pair.Key);
						if (!T)
						{
							ZDelayStart.Add(Pair.Key, NowZ);
						}
						else if (NowZ - *T >= ZDelaySeconds)
						{
							bRecycleByZ = true; // 延迟区持续超时：回收
							ZDelayStart.Remove(Pair.Key);
						}
					}
					if (bRecycleByZ)
					{
						if (UChunkEnemySpawnerComponent* SC = Active->FindComponentByClass<UChunkEnemySpawnerComponent>())
						{
							SC->CaptureState(Rec.SavedState);
						}
						Rec.ActiveEnemy.Reset();
						Active->Tags.Remove(FName(TEXT("ChunkEnemyActive")));
						Active->Destroy();
						UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 的活体因 Z 差 %.0f 被回收"), *Pair.Key, ZDiff);
						continue;
					}
				}
			}
			else
			{
				ZDelayStart.Remove(Pair.Key);
			}

			// 玩家远离活体（追丢/脱战）：回收（重置为待生成，不记死亡）
			const float DistToActive = FVector::Distance(PlayerLoc, Active->GetActorLocation());
			if (DistToActive > DespawnDist)
			{
				// 捕获标记变量（重新生成时写回，保持数值连续）
				if (UChunkEnemySpawnerComponent* SC = Active->FindComponentByClass<UChunkEnemySpawnerComponent>())
				{
					SC->CaptureState(Rec.SavedState);
				}
				// 先清引用 + 移除活体标记（回收销毁不记为死亡），再销毁
				Rec.ActiveEnemy.Reset();
				Active->Tags.Remove(FName(TEXT("ChunkEnemyActive")));
				Active->Destroy();
				UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 的活体已回收（玩家距 %.0f）"), *Pair.Key, DistToActive);
			}
			continue;
		}

		// 无活体：玩家靠近"最后位置"且处于同一高度层则生成
		if (bZCheck)
		{
			const float ZDiff = FMath::Abs(PlayerLoc.Z - Rec.LastKnownLocation.Z);
			if (ZDiff > ZDelayDist)
			{
				continue; // 玩家不在该敌人的高度层：不生成
			}
		}
		const float DistToLast = FVector::Distance(PlayerLoc, Rec.LastKnownLocation);
		if (DistToLast < SpawnDist)
		{
			if (Rec.EnemyClass.IsNull())
			{
				continue;
			}
			UClass* Class = Rec.EnemyClass.LoadSynchronous();
			if (!Class)
			{
				continue;
			}
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* Spawned = World->SpawnActor<AActor>(Class, Rec.LastKnownLocation, Rec.LastKnownRotation, Params);
			if (Spawned)
			{
				// 打标记：组件识别自己是"活体"（不重复收编）
				Spawned->Tags.AddUnique(FName(TEXT("ChunkEnemyActive")));
				// 写回上次保存的标记变量（位置连续之外，数值也连续），并广播"再次加载"事件
				if (Rec.SavedState.Num() > 0)
				{
					if (UChunkEnemySpawnerComponent* SC = Spawned->FindComponentByClass<UChunkEnemySpawnerComponent>())
					{
						SC->ApplyState(Rec.SavedState);
						SC->NotifyRespawned();
					}
				}
				Rec.ActiveEnemy = Spawned;
				UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] 生成活体 %s（距最后位置 %.0f）"), *Pair.Key, DistToLast);
			}
		}
	}
}
