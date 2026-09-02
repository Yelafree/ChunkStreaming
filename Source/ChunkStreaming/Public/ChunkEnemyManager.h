#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "TimerManager.h"
#include "UObject/WeakObjectPtr.h"
#include "ChunkEnemyManager.generated.h"

/** 单个敌人的"家"记录（占位收编后由管理器接管生命周期）。 */
USTRUCT()
struct FChunkEnemySpawnRecord
{
	GENERATED_BODY()

	/** 家的位置/朝向（初始生成点）。 */
	FVector HomeLocation = FVector::ZeroVector;
	FRotator HomeRotation = FRotator::ZeroRotator;

	/** 最后已知位置/朝向（活体在场时持续更新；回收后重生用此位置，保持无缝不瞬移回家）。 */
	FVector LastKnownLocation = FVector::ZeroVector;
	FRotator LastKnownRotation = FRotator::ZeroRotator;

	/** 敌人类型（软引用：不强制加载资源，生成活体时才加载）。 */
	TSoftClassPtr<AActor> EnemyClass;

	/** 永久死亡（玩家击杀后不再生成）。 */
	bool bDead = false;

	/** 当前在场的活体（持久层）。 */
	TWeakObjectPtr<AActor> ActiveEnemy;

	/** 活体回收时捕获的标记变量数据（重新生成时写回）。 */
	TArray<uint8> SavedState;

	/** Reset 后冻结：保持在场活体现状，不回收/不生成/不误判死亡；关卡重启（活体随世界销毁）后自动解除。 */
	bool bFrozen = false;
};

/**
 * 敌人占位管理器（GameInstance 级）：
 * 编辑器里摆放的敌人（挂 ChunkEnemySpawnerComponent）在游戏开始时收编为"家记录"，
 * 管理器按玩家距离生成/回收"活体"（活体生成到持久层，与区块加载解耦，可跨区块追击）。
 * - 玩家距家 < EnemySpawnDistance    → 生成活体（类型软引用按需加载）
 * - 玩家距活体 > EnemyDespawnDistance → 回收活体（追丢/脱战，重置为待生成，不记死亡）
 * - 活体被销毁（玩家击杀）           → 记永久死亡，永不生成（Clear 可复活）
 */
UCLASS()
class CHUNKSTREAMING_API UChunkEnemyManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ UGameInstanceSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ end

	/** 占位敌人收编：注册家的位置与类型。返回 true = 已永久死亡（调用方应销毁占位且不再生成）。 */
	bool RegisterSpawn(const FString& Key, const FTransform& HomeTransform, TSoftClassPtr<AActor> EnemyClass);

	/** 标记永久死亡（敌人死亡逻辑可调组件 MarkAsDead 触发）。 */
	void MarkDead(const FString& Key);

	/** 复活单个（调试/测试用）。 */
	void ClearDead(const FString& Key);

	/**
	 * 重置所有敌人的状态（死亡、保存的数值/位置全部清除；在场活体回收销毁不记死）。
	 * 在"关卡重启"流程（玩家死亡重生、坐火/休息等）中调用，下次进入时敌人以全新状态重新加载。
	 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Enemy")
	void ClearAllEnemyStates();

	/** 蓝图入口：重置所有敌人状态（无需先获取管理器）。调用时机：玩家死亡/休息导致的关卡重启前。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Enemy", meta = (WorldContext = "WorldContextObject", DisplayName = "Reset Enemy States"))
	static void ResetEnemyStates(const UObject* WorldContextObject);

	/** 检测生成/回收（由定时器调用）。 */
	void TickSpawns();

	static UChunkEnemyManager* Get(const UObject* WorldContext);

private:
	TMap<FString, FChunkEnemySpawnRecord> Records;
	FTimerHandle CheckTimerHandle;

	/** Z 延迟区计时起点（记录 Key -> 进入延迟区时间），仅 bEnableEnemyZCheck 时使用。 */
	TMap<FString, double> ZDelayStart;
};
