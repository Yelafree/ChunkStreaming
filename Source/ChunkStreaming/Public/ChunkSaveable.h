#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ChunkTypes.h"
#include "ChunkSaveable.generated.h"

/**
 * 可保存状态接口：挂在该接口的 Actor（敌人/可交互物）在区块卸载时保存状态，
 * 重新加载区块时按 EnemyId 恢复。蓝图实现即可（接口蓝图）。
 */
UINTERFACE(BlueprintType, MinimalAPI)
class UChunkSaveable : public UInterface
{
	GENERATED_BODY()
};

class CHUNKSTREAMING_API IChunkSaveable
{
	GENERATED_BODY()

public:
	/** 把当前状态写入 OutData（卸载区块前调用）。 */
	UFUNCTION(BlueprintNativeEvent, Category = "Chunk Streaming")
	void SaveChunkState(FChunkEnemySaveData& OutData);

	/** 用 InData 恢复状态（区块加载完成后调用）。 */
	UFUNCTION(BlueprintNativeEvent, Category = "Chunk Streaming")
	void RestoreChunkState(const FChunkEnemySaveData& InData);

	/** 返回该 Actor 在区块内的稳定唯一 ID（默认用 Actor 名字）。 */
	UFUNCTION(BlueprintNativeEvent, Category = "Chunk Streaming")
	FString GetEnemyId();
};
