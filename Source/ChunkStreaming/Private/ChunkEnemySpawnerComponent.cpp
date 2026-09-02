#include "ChunkEnemySpawnerComponent.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

#include "ChunkEnemyManager.h"
#include "ChunkStateReflection.h"
#include "ChunkStreamingSettings.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

UChunkEnemySpawnerComponent::UChunkEnemySpawnerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UChunkEnemyManager* UChunkEnemySpawnerComponent::GetManager() const
{
	UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UChunkEnemyManager>() : nullptr;
}

FString UChunkEnemySpawnerComponent::GetResolvedKey() const
{
	if (!EnemyKey.IsEmpty())
	{
		return EnemyKey;
	}
	if (AActor* Owner = GetOwner())
	{
		return Owner->GetName();
	}
	return FString();
}

namespace
{
	bool IsPlayerOwned(AActor* Actor)
	{
		APawn* Pawn = Cast<APawn>(Actor);
		if (!Pawn)
		{
			return false;
		}
		if (Pawn->IsPlayerControlled())
		{
			return true;
		}
		if (AController* C = Pawn->GetController())
		{
			return Cast<APlayerController>(C) != nullptr;
		}
		return false;
	}
}

void UChunkEnemySpawnerComponent::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const UChunkStreamingSettings* Settings = GetDefault<UChunkStreamingSettings>();
	if (!Settings || !Settings->bEnableEnemySpawning)
	{
		return;
	}
	// 延迟一帧再处理：等玩家 Possess 完成（玩家与敌人可能共享父类，需先识别玩家）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(this, &UChunkEnemySpawnerComponent::DeferredBeginPlay);
	}
}

void UChunkEnemySpawnerComponent::DeferredBeginPlay()
{
	AActor* Owner = GetOwner();
	if (!Owner || !IsValid(this))
	{
		return;
	}

	// 玩家豁免：玩家 Pawn 不参与占位收编
	if (IsPlayerOwned(Owner))
	{
		return;
	}

	// 活体模式：管理器生成的实例带 Tag，不重复收编
	if (Owner->ActorHasTag(TEXT("ChunkEnemyActive")))
	{
		bActiveEnemy = true;
		CachedKey = GetResolvedKey();
		// 活体被销毁时上报（击杀 → 永久死亡）
		Owner->OnDestroyed.AddDynamic(this, &UChunkEnemySpawnerComponent::OnOwnerDestroyed);
		return;
	}

	// 占位模式：收编后自我销毁（管理器按距离生成活体）
	UChunkEnemyManager* Manager = GetManager();
	if (!Manager)
	{
		return;
	}
	CachedKey = GetResolvedKey();
	const FTransform Home = Owner->GetActorTransform();
	const TSoftClassPtr<AActor> EnemyClass(Owner->GetClass());
	const bool bAlreadyDead = Manager->RegisterSpawn(CachedKey, Home, EnemyClass);
	UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] 占位收编 %s（家=%s，已死=%d）"),
		*CachedKey, *Home.GetLocation().ToString(), bAlreadyDead ? 1 : 0);
	// 占位使命完成：销毁（无论死活；已死的由管理器跳过，不会再生）
	Owner->Destroy();
}

void UChunkEnemySpawnerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 记录销毁原因：只有"被 Destroy"（玩家击杀）才上报死亡；
	// 世界/关卡切换销毁（Open Level、退出）不算击杀，不记死
	bWorldTearDown = (EndPlayReason != EEndPlayReason::Destroyed);
	if (AActor* Owner = GetOwner())
	{
		Owner->OnDestroyed.RemoveDynamic(this, &UChunkEnemySpawnerComponent::OnOwnerDestroyed);
	}
	Super::EndPlay(EndPlayReason);
}

void UChunkEnemySpawnerComponent::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// 只有活体被销毁时才上报（占位销毁是收编流程，不在这里处理）
	if (!bActiveEnemy)
	{
		return;
	}
	// 世界/关卡切换销毁：不上报
	if (bWorldTearDown)
	{
		return;
	}
	// 管理器回收销毁（标记已被移除）：不上报死亡
	if (DestroyedActor && !DestroyedActor->ActorHasTag(TEXT("ChunkEnemyActive")))
	{
		return;
	}
	if (UChunkEnemyManager* Manager = GetManager())
	{
		if (!CachedKey.IsEmpty())
		{
			Manager->MarkDead(CachedKey);
			UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] 活体 %s 被销毁 → 记永久死亡"), *CachedKey);
		}
	}
}

void UChunkEnemySpawnerComponent::MarkAsDead()
{
	if (UChunkEnemyManager* Manager = GetManager())
	{
		if (!CachedKey.IsEmpty())
		{
			Manager->MarkDead(CachedKey);
			UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] MarkAsDead: %s"), *CachedKey);
		}
	}
}

void UChunkEnemySpawnerComponent::NotifyRespawned()
{
	OnEnemyRespawned.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 重生事件已广播"), *CachedKey);
}

namespace
{
	/** 组件名匹配：支持实例名精确匹配，或"去末尾 _N 数字后缀"匹配（如 BPC_Attribution 匹配 BPC_Attribution_0）。 */
	bool ComponentNameMatches(UActorComponent* Comp, const FString& Wanted)
	{
		if (!Comp)
		{
			return false;
		}
		const FString Inst = Comp->GetName();
		if (Inst == Wanted)
		{
			return true;
		}
		const int32 Under = Inst.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Under != INDEX_NONE)
		{
			bool bAllDigits = true;
			for (int32 i = Under + 1; i < Inst.Len(); ++i)
			{
				if (!FChar::IsDigit(Inst[i]))
				{
					bAllDigits = false;
					break;
				}
			}
			if (bAllDigits && Inst.Left(Under) == Wanted)
			{
				return true;
			}
		}
		return false;
	}
}

void UChunkEnemySpawnerComponent::CaptureState(TArray<uint8>& OutBytes)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	FBufferArchive Bytes;
	uint32 Count = 0;
	const int64 CountPos = Bytes.Tell();
	Bytes << Count;

	// 解析 VariablesToSave：短名（Actor 变量白名单）与 "组件名.变量名"（组件变量白名单）
	TArray<FName> ActorFilter;
	TArray<FString> CompVarEntries;
	const bool bHasFilter = VariablesToSave.Num() > 0;
	for (const FString& S : VariablesToSave)
	{
		const FString T = S.TrimStartAndEnd();
		if (T.IsEmpty())
		{
			continue;
		}
		if (T.Contains(TEXT(".")))
		{
			CompVarEntries.Add(T);
		}
		else
		{
			ActorFilter.AddUnique(FName(*T));
		}
	}

	// 1) 敌人 Actor 自身
	if (!bHasFilter)
	{
		Count += ChunkStateReflection::CaptureObjectProps(Bytes, Owner, TEXT(""), nullptr);
	}
	else if (ActorFilter.Num() > 0)
	{
		Count += ChunkStateReflection::CaptureObjectProps(Bytes, Owner, TEXT(""), &ActorFilter);
	}

	// 2) 指定组件：实例引用 / 组件类 / Tag / 变量白名单前缀条目 —— 任一命中即纳入
	for (UActorComponent* Comp : Owner->GetComponents())
	{
		if (!Comp || Comp == this)
		{
			continue;
		}
		// a) 实例引用
		bool bIncluded = false;
		for (const FComponentReference& Ref : ComponentsToSave)
		{
			if (UActorComponent* RefComp = Ref.GetComponent(Owner))
			{
				if (RefComp == Comp)
				{
					bIncluded = true;
					break;
				}
			}
		}
		// b) 组件类
		if (!bIncluded)
		{
			for (const TSubclassOf<UActorComponent>& Cls : ComponentClassesToSave)
			{
				if (Cls && Comp->IsA(Cls))
				{
					bIncluded = true;
					break;
				}
			}
		}
		// c) Tag
		if (!bIncluded)
		{
			for (const FName& Tag : ComponentTagsToSave)
			{
				if (Comp->ComponentHasTag(Tag))
				{
					bIncluded = true;
					break;
				}
			}
		}
		// d) 变量白名单前缀条目（"组件名.变量名"）
		TArray<FName> EntryVars;
		for (const FString& Entry : CompVarEntries)
		{
			const int32 Dot = Entry.Find(TEXT("."));
			if (Dot == INDEX_NONE)
			{
				continue;
			}
			if (ComponentNameMatches(Comp, Entry.Left(Dot)))
			{
				EntryVars.AddUnique(FName(*Entry.RightChop(Dot + 1)));
				bIncluded = true;
			}
		}

		if (!bIncluded)
		{
			continue;
		}
		// 收集范围：引用/类/Tag 指定 = 全量；仅白名单条目 = 只收集条目变量
		const bool bFullCollect = !bHasFilter || ComponentsToSave.Num() > 0 || ComponentClassesToSave.Num() > 0 || ComponentTagsToSave.Num() > 0;
		const int32 Before = Count;
		if (bFullCollect)
		{
			Count += ChunkStateReflection::CaptureObjectProps(Bytes, Comp, Comp->GetName() + TEXT("."), nullptr);
		}
		else if (EntryVars.Num() > 0)
		{
			Count += ChunkStateReflection::CaptureObjectProps(Bytes, Comp, Comp->GetName() + TEXT("."), &EntryVars);
		}
		{
			FString VarList;
			for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				if (Prop && ChunkStateReflection::IsSaveableProperty(Prop))
				{
					if (!VarList.IsEmpty()) { VarList += TEXT(", "); }
					VarList += Prop->GetName();
				}
			}
			UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] 组件 %s 已纳入保存（收集 %d 个变量，可保存属性: %s）"), *Comp->GetName(), Count - Before, *VarList);
		}
	}

	// 引用解析失败提示
	if (ComponentsToSave.Num() > 0)
	{
		for (const FComponentReference& Ref : ComponentsToSave)
		{
			if (!Ref.GetComponent(Owner))
			{
				UE_LOG(LogTemp, Warning, TEXT("[ChunkEnemy] ComponentsToSave 引用解析失败：ComponentProperty=%s（检查是否选中的本敌人身上的组件）"), *Ref.ComponentProperty.ToString());
			}
		}
	}

	Bytes.Seek(CountPos);
	Bytes << Count;
	OutBytes = Bytes;
	UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 状态已捕获（共 %u 条）"), *GetResolvedKey(), Count);
}
void UChunkEnemySpawnerComponent::ApplyState(const TArray<uint8>& InBytes)
{
	AActor* Owner = GetOwner();
	if (!Owner || InBytes.Num() == 0)
	{
		return;
	}
	FMemoryReader R(InBytes);
	uint32 Count = 0;
	R << Count;
	const int32 Restored = ChunkStateReflection::RestoreObjectProps(R, Owner, (int32)Count);
	UE_LOG(LogTemp, Log, TEXT("[ChunkEnemy] %s 状态已恢复（%d/%u 条）"), *GetResolvedKey(), Restored, Count);
}
