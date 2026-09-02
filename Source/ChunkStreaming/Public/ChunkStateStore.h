#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ChunkTypes.h"
#include "ChunkStateStore.generated.h"

class ULevel;

/**
 * 区块状态库（GameInstance 级）：存放各区块卸载时的敌人/可保存 Actor 状态。
 * 跨关卡、跨 PIE 存活；可并入存档系统（EasyMultiSave 等）实现持久化存档。
 */
UCLASS()
class CHUNKSTREAMING_API UChunkStateStore : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** 把区块内所有 IChunkSaveable Actor 的状态存入状态库（卸载前调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void SaveChunkFromLevel(FName ChunkName, ULevel* Level);

	/** 把状态库中该区块的状态恢复到区块内对应 Actor（加载完成后调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void RestoreChunkToLevel(FName ChunkName, ULevel* Level);

	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	bool HasSavedState(FName ChunkName) const;

	/** 清除某区块的状态（例如：让已死亡敌人重新出现）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void ClearChunkState(FName ChunkName);

	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	TArray<FChunkEnemySaveData> GetChunkState(FName ChunkName) const;

	/** 导出全部状态（供存档系统合并）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void GetAllStates(TMap<FName, FChunkEnemySaveDataList>& OutStates) const;

	/** 导入全部状态（读档时调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void SetAllStates(const TMap<FName, FChunkEnemySaveDataList>& InStates);

	/** 生成带区块前缀的稳定 ID。 */
	static FString MakeEnemyId(FName ChunkName, const FString& LocalId);

	UPROPERTY()
	TMap<FName, FChunkEnemySaveDataList> ChunkStates;
};
