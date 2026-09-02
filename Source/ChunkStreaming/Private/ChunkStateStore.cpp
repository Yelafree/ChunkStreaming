#include "ChunkStateStore.h"

#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "ChunkSaveable.h"

FString UChunkStateStore::MakeEnemyId(FName ChunkName, const FString& LocalId)
{
	return ChunkName.ToString() + TEXT("_") + LocalId;
}

void UChunkStateStore::SaveChunkFromLevel(FName ChunkName, ULevel* Level)
{
	if (!Level)
	{
		return;
	}
	TArray<FChunkEnemySaveData> Saved;
	for (AActor* Actor : Level->Actors)
	{
		if (!Actor)
		{
			continue;
		}
		// 接口可在 Actor 自身，也可在 Actor 挂载的组件上（ChunkEnemyStateComponent 等）
		IChunkSaveable* Saveable = Cast<IChunkSaveable>(Actor);
		if (!Saveable)
		{
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				Saveable = Cast<IChunkSaveable>(Comp);
				if (Saveable)
				{
					break;
				}
			}
		}
		if (!Saveable)
		{
			continue;
		}
		FChunkEnemySaveData Data;
		Saveable->SaveChunkState(Data);
		if (Data.EnemyId.IsEmpty())
		{
			Data.EnemyId = Actor->GetName();
		}
		Data.EnemyId = MakeEnemyId(ChunkName, Data.EnemyId);
		Saved.Add(Data);
	}
	FChunkEnemySaveDataList List;
	List.Items = Saved;
	ChunkStates.Add(ChunkName, List);
}

void UChunkStateStore::RestoreChunkToLevel(FName ChunkName, ULevel* Level)
{
	if (!Level)
	{
		return;
	}
	const FChunkEnemySaveDataList* List = ChunkStates.Find(ChunkName);
	if (!List)
	{
		return;
	}
	const FString ChunkPrefix = ChunkName.ToString() + TEXT("_");
	for (const FChunkEnemySaveData& Data : List->Items)
	{
		FString LocalId = Data.EnemyId;
		if (LocalId.StartsWith(ChunkPrefix))
		{
			LocalId = LocalId.RightChop(ChunkPrefix.Len());
		}
		// 按本地 ID 找到对应 Actor（优先 IChunkSaveable 的 GetEnemyId，其次 Actor 名字）
		AActor* FoundActor = nullptr;
		for (AActor* Actor : Level->Actors)
		{
			if (!Actor)
			{
				continue;
			}
			IChunkSaveable* Saveable = Cast<IChunkSaveable>(Actor);
			if (!Saveable)
			{
				for (UActorComponent* Comp : Actor->GetComponents())
				{
					Saveable = Cast<IChunkSaveable>(Comp);
					if (Saveable)
					{
						break;
					}
				}
			}
			if (Saveable)
			{
				if (Saveable->GetEnemyId() == LocalId)
				{
					FoundActor = Actor;
					break;
				}
			}
			else if (Actor->GetName() == LocalId)
			{
				FoundActor = Actor;
				break;
			}
		}
		if (FoundActor)
		{
			if (IChunkSaveable* Saveable = Cast<IChunkSaveable>(FoundActor))
			{
				Saveable->RestoreChunkState(Data);
			}
		}
	}
}

bool UChunkStateStore::HasSavedState(FName ChunkName) const
{
	return ChunkStates.Contains(ChunkName);
}

void UChunkStateStore::ClearChunkState(FName ChunkName)
{
	ChunkStates.Remove(ChunkName);
}

TArray<FChunkEnemySaveData> UChunkStateStore::GetChunkState(FName ChunkName) const
{
	const FChunkEnemySaveDataList* List = ChunkStates.Find(ChunkName);
	return List ? List->Items : TArray<FChunkEnemySaveData>();
}

void UChunkStateStore::GetAllStates(TMap<FName, FChunkEnemySaveDataList>& OutStates) const
{
	OutStates = ChunkStates;
}

void UChunkStateStore::SetAllStates(const TMap<FName, FChunkEnemySaveDataList>& InStates)
{
	ChunkStates = InStates;
}
