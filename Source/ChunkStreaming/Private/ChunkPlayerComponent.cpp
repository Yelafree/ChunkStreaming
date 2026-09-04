#include "ChunkPlayerComponent.h"

UChunkPlayerComponent::UChunkPlayerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UChunkPlayerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (UWorld* World = GetWorld())
	{
		if (UChunkStreamingSubsystem* Sub = World->GetSubsystem<UChunkStreamingSubsystem>())
		{
			Sub->OnPlayerEnteredChunk.AddDynamic(this, &UChunkPlayerComponent::HandleSubsystemEntered);
			// 同步一次初始区块，避免挂上组件后第一帧查询为空
			CurrentChunk = Sub->GetPlayerChunk();
		}
	}
}

void UChunkPlayerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		if (UChunkStreamingSubsystem* Sub = World->GetSubsystem<UChunkStreamingSubsystem>())
		{
			Sub->OnPlayerEnteredChunk.RemoveDynamic(this, &UChunkPlayerComponent::HandleSubsystemEntered);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UChunkPlayerComponent::HandleSubsystemEntered(FName ChunkName, FName PreviousChunk)
{
	CurrentChunk = ChunkName;
	OnPlayerEnteredChunk.Broadcast(ChunkName, PreviousChunk);
}
