#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChunkStreamingSettings.generated.h"

class UChunkGraphAsset;

/**
 * Chunk Streaming 项目设置（Project Settings -> Chunk Streaming）。
 * 在这里指定关卡使用的连接图资产与运行时参数。
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Chunk Streaming"))
class CHUNKSTREAMING_API UChunkStreamingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 本关卡使用的区块连接图资产。 */
	UPROPERTY(EditAnywhere, config, Category = "Chunk Streaming", meta = (AllowedClasses = "/Script/ChunkStreaming.ChunkGraphAsset"))
	TSoftObjectPtr<UChunkGraphAsset> GraphAsset;

	/** 总开关。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming")
	bool bEnableStreaming = true;

	/** 流送判定周期（秒）。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming", meta = (ClampMin = "0.05"))
	float UpdateInterval = 0.2f;

	/** 预加载余量：沿移动方向在参考点之外额外提前加载的距离。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming", meta = (ClampMin = "0.0"))
	float LookAheadDistance = 2500.f;

	/** 流送参考点：true = 以玩家 Pawn 位置为中心；false = 以摄像机位置为中心（默认）。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming")
	bool bUsePawnAsStreamingSource = false;

	/** 迟滞窗口：相机在边界来回抖动时，切换区块等待的秒数。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming", meta = (ClampMin = "0.0"))
	float HysteresisSeconds = 1.5f;

	/** 身后保留的未需求区块数（允许回头且免重载；仅对"可步行回头"的块生效）。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming", meta = (ClampMin = "0"))
	int32 KeepBehindCount = 1;

	/** 保留距离：距参考点超过该距离的未需求区块直接卸载（远距传送离开的块不留）。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming", meta = (ClampMin = "0.0"))
	float KeepBehindDistance = 5000.f;

	/** 是否开启二级预加载（沿前进方向再预加载下一跳）。 */
	UPROPERTY(EditAnywhere, config, Category = "Streaming")
	bool bPreloadNextHop = true;


	// ---- 敌人占位收编（ChunkEnemySpawnerComponent / ChunkEnemyManager） ----

	/** 敌人占位收编总开关。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning")
	bool bEnableEnemySpawning = true;

	/** 活体生成距离：玩家距敌人"家"小于该值时生成活体（cm）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "100.0"))
	float EnemySpawnDistance = 2500.f;

	/** 活体回收距离：玩家距活体超过该值时回收（追丢/脱战，不记死亡）（cm）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "100.0"))
	float EnemyDespawnDistance = 3000.f;

	/** 生成/回收检测周期（秒）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "0.1"))
	float EnemyCheckInterval = 0.5f;

	// ---- 敌人 Z 轴（高度层）检测 ----

	/** 是否开启敌人 Z 检测：玩家与敌人（或其"家"）Z 高度差异过大时不生成/回收（多层关卡用）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning")
	bool bEnableEnemyZCheck = false;

	/** 小阈值：玩家与敌人 Z 差超过该值（cm）后开始计时；同时超过该值时不生成活体。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "0.0"))
	float EnemyZDelayDistance = 300.f;

	/** 小阈值超时（秒）：Z 差持续超过小阈值达到该时长后回收活体（防跳跃误触发）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "0.1"))
	float EnemyZDelaySeconds = 3.f;

	/** 大阈值：玩家与敌人 Z 差超过该值（cm）立即回收（跳崖/电梯/跨层传送）。 */
	UPROPERTY(EditAnywhere, config, Category = "Enemy Spawning", meta = (ClampMin = "0.0"))
	float EnemyZImmediateDistance = 1500.f;

	/** 运行时调试可视化（等价于控制台 ChunkStream.Debug 1）。 */
	UPROPERTY(EditAnywhere, config, Category = "Debug")
	bool bDebugDraw = false;
};
