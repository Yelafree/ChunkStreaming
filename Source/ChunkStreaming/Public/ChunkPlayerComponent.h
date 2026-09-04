#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkStreamingSubsystem.h"
#include "ChunkPlayerComponent.generated.h"

/**
 * 玩家区块辅助组件：挂到玩家 Pawn 上。
 * - On Player Entered Chunk：玩家进入新区块事件（可直接 Add Event 或绑定），
 *   与子系统 OnPlayerEnteredChunk 同步触发（基于玩家坐标，含瞬移）。
 * - Get Current Chunk Name：查询玩家当前所在区块（组件引脚版；也可用
 *   子系统静态节点 "Get Current Chunk Name"，无需挂组件）。
 */
UCLASS(ClassGroup = (ChunkStreaming), meta = (BlueprintSpawnableComponent))
class CHUNKSTREAMING_API UChunkPlayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChunkPlayerComponent();

	/** 玩家进入新区块（ChunkName = 新区块，PreviousChunk = 上一区块）。 */
	UPROPERTY(BlueprintAssignable, Category = "Chunk Streaming")
	FChunkEnteredDelegate OnPlayerEnteredChunk;

	/** 玩家当前所在区块的名字（不在任何区块内返回 None）。 */
	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	FName GetCurrentChunkName() const { return CurrentChunk; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleSubsystemEntered(FName ChunkName, FName PreviousChunk);

	FName CurrentChunk;
};
