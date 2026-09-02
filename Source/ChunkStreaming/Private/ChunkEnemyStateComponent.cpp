#include "ChunkEnemyStateComponent.h"

#include "GameFramework/Actor.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

#include "ChunkStateReflection.h"

void UChunkEnemyStateComponent::SaveNow()
{
	FChunkEnemySaveData Data;
	SaveChunkState_Implementation(Data);
}

void UChunkEnemyStateComponent::RestoreNow(const FChunkEnemySaveData& InData)
{
	RestoreChunkState_Implementation(InData);
}

void UChunkEnemyStateComponent::SaveChunkState_Implementation(FChunkEnemySaveData& OutData)
{
	OutData.EnemyId = GetEnemyId_Implementation();
	OutData.Health = Health;
	OutData.bDead = bDead;
	if (AActor* OwnerActor = GetOwner())
	{
		SavedTransform = OwnerActor->GetActorTransform();
	}
	OutData.SavedTransform = SavedTransform;

	// 反射自动收集：敌人 Actor 自身 + Tag 白名单组件的数值型变量
	FBufferArchive Bytes;
	uint32 Count = 0;
	const int64 CountPos = Bytes.Tell();
	Bytes << Count;

	Count += ChunkStateReflection::CaptureObjectProps(Bytes, GetOwner(), TEXT(""), nullptr);
	if (AActor* OwnerActor = GetOwner())
	{
		for (UActorComponent* Comp : OwnerActor->GetComponents())
		{
			if (!Comp || Comp == this)
			{
				continue;
			}
			for (const FName& Tag : ComponentTagsToSave)
			{
				if (Comp->ComponentHasTag(Tag))
				{
					Count += ChunkStateReflection::CaptureObjectProps(Bytes, Comp, Comp->GetName() + TEXT("."), nullptr);
					break;
				}
			}
		}
	}

	Bytes.Seek(CountPos);
	Bytes << Count;
	OutData.CustomData = Bytes;
}

void UChunkEnemyStateComponent::RestoreChunkState_Implementation(const FChunkEnemySaveData& InData)
{
	Health = InData.Health;
	bDead = InData.bDead;
	SavedTransform = InData.SavedTransform;
	if (bDead)
	{
		if (AActor* OwnerActor = GetOwner())
		{
			OwnerActor->Destroy();
			return;
		}
	}
	if (AActor* OwnerActor = GetOwner())
	{
		OwnerActor->SetActorTransform(SavedTransform);
	}

	// 反射恢复：按名字写回属性（找不到的属性跳过其字节）
	if (InData.CustomData.Num() > 0)
	{
		FMemoryReader R(InData.CustomData);
		uint32 Count = 0;
		R << Count;
		ChunkStateReflection::RestoreObjectProps(R, GetOwner(), (int32)Count);
	}
}

FString UChunkEnemyStateComponent::GetEnemyId_Implementation()
{
	if (!EnemyId.IsEmpty())
	{
		return EnemyId;
	}
	if (AActor* OwnerActor = GetOwner())
	{
		return OwnerActor->GetName();
	}
	return FString();
}