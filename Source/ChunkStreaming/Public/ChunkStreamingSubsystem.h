#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ChunkStreamingSubsystem.generated.h"

class UChunkGraphAsset;
class ULevelStreaming;
struct FChunkInfo;
class AActor;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FChunkEnteredDelegate, FName, ChunkName, FName, PreviousChunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FChunkEventDelegate, FName, ChunkName);

/**
 * 图驱动关卡流送管理器（每个世界一个）。
 * - 按连接图加载/卸载玩法块（相机 + LookAhead 判定流送焦点，玩家坐标判定进入事件）
 * - 背景块 OR 加载（VisibleFromChunks）
 * - 常驻块永远加载
 * - 卸载前自动保存敌人状态，加载完成后自动恢复（UChunkStateStore）
 * - 跨区块瞬移（TeleportToChunk，过渡模式 + 代际号防加载不同步）
 */
UCLASS()
class CHUNKSTREAMING_API UChunkStreamingSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// ---- 查询（蓝图） ----

	/** 玩家当前所在区块（基于玩家 Pawn 坐标）。 */
	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	FName GetPlayerChunk() const { return PlayerChunk; }

	/** 当前流送焦点区块（基于相机 + LookAhead）。 */
	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	FName GetStreamingChunk() const { return StreamingChunk; }

	/** 区块（玩法或背景）当前是否已加载。 */
	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	bool IsChunkLoaded(FName ChunkName) const;

	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	UChunkGraphAsset* GetGraphAsset() const { return GraphAsset; }

	// ---- 事件（蓝图可绑定） ----

	/** 玩家进入新区块（基于玩家坐标，含瞬移）。 */
	UPROPERTY(BlueprintAssignable, Category = "Chunk Streaming")
	FChunkEnteredDelegate OnPlayerEnteredChunk;

	UPROPERTY(BlueprintAssignable, Category = "Chunk Streaming")
	FChunkEnteredDelegate OnPlayerExitedChunk;

	/** 区块开始加载。 */
	UPROPERTY(BlueprintAssignable, Category = "Chunk Streaming")
	FChunkEventDelegate OnChunkLoadStarted;

	/** 区块加载完成（敌人状态已恢复）。 */
	UPROPERTY(BlueprintAssignable, Category = "Chunk Streaming")
	FChunkEventDelegate OnChunkLoadFinished;

	// ---- 跨区块瞬移 ----

	/**
	 * 瞬移到目标区块（静态节点，可在蓝图搜索）：异步预加载目标块及其邻居，
	 * 加载完成后移动玩家并恢复目标块敌人状态。TargetLocation 为 0 时
	 * 使用目标块的 PlayerStart，否则使用区块包围盒中心。
	 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming", meta = (WorldContext = "WorldContextObject"))
	static void TeleportToChunk(const UObject* WorldContextObject, FName TargetChunk, FVector TargetLocation);

	/**
	 * 预加载传送（延迟节点，类似 Delay）：
	 * 先加载目标坐标所在的区块（含邻居），加载完成后才传送角色并触发输出。
	 * - TargetActor 留空 = 玩家 Pawn；目标区块已加载 → 立即传送并立刻完成
	 * - TimeoutSeconds 超时后强制传送（仍会完成）
	 * - 加载期间目标区块不会被卸载
	 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming", meta = (Latent, LatentInfo = "LatentInfo", WorldContext = "WorldContextObject", DisplayName = "Preload Teleport To Location"))
	static void PreloadTeleportToLocation(const UObject* WorldContextObject, AActor* TargetActor, FVector TargetLocation, float TimeoutSeconds, FLatentActionInfo LatentInfo);

	// ---- 调试 ----

	/** 控制台命令 ChunkStream.Debug [0|1] 的同名蓝图版本。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void SetDebugDraw(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Chunk Streaming")
	bool GetDebugDraw() const { return bDebugDraw; }

	/** 立即强制刷新（调试用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void ForceReconcile();

	/** 当前活动的流送子系统（供模块级控制台命令使用）。 */
	static UChunkStreamingSubsystem* GetActive();

	/** 解析坐标所在的区块（不在任何范围内时取最近玩法区块）。 */
	FName ResolveChunkAtLocation(const FVector& Location) const;

	/** 注册/注销"预加载传送等待中的区块"（等待期间其及邻居加入需求集，不会被卸载）。 */
	void RegisterPendingPreload(FName ChunkName);
	void UnregisterPendingPreload(FName ChunkName);

protected:
	void UpdateStreaming();
	void Reconcile(FName InStreamingChunk);
	FName FindChunkAtX(float X) const;
	/** 参考点不在任何区块范围内时的兜底：返回 X 最近的玩法区块。 */
	FName FindNearestChunk(float X) const;
	void StartLoadChunk(FName ChunkName);
	void UnloadChunk(FName ChunkName, bool bBackground);
	void SaveChunkState(FName ChunkName);
	void RestoreChunkState(FName ChunkName);
	void CompleteTeleport();
	void DebugDraw();
	bool IsGameplayChunk(FName ChunkName) const;
	ULevelStreaming* GetStreamingLevelFor(FName PackageName) const;
	float GetCameraAxis() const;
	float GetPawnAxis() const;
	float GetForwardSign() const;
	/** 流送参考轴坐标：按设置取相机或 Pawn（bUsePawnAsStreamingSource）。 */
	float GetStreamingReferenceAxis() const;

	/** 流送参考位置（3D）：按设置取相机或 Pawn 的位置。 */
	FVector GetStreamingReferenceLocation() const;

	/** 3D 位置判定（含 X 回退）。 */
	FName FindChunkAtLocation(const FVector& Location) const;
	FVector2D GetChunkRange(FName ChunkName) const;

	/** 异步加载/卸载完成的延迟回调（LatentAction 回调）。 */
	UFUNCTION()
	void OnChunkLatentLoadFinished();

	UFUNCTION()
	void OnChunkLatentUnloadFinished();

	UPROPERTY()
	TObjectPtr<UChunkGraphAsset> GraphAsset;

	UPROPERTY()
	TSet<FName> LoadedChunks;          // 已加载的玩法块
	UPROPERTY()
	TSet<FName> LoadedBackgrounds;     // 已加载的背景块
	UPROPERTY()
	TSet<FName> LoadingChunks;         // 加载中（玩法+背景）
	UPROPERTY()
	TSet<FName> UnloadingChunks;       // 卸载中
	UPROPERTY()
	TSet<FName> RestoredChunks;        // 本次加载周期已恢复状态的区块
	UPROPERTY()
	TSet<FName> PendingPreloadChunks;  // 预加载传送等待中的区块（保持加载）
	TMap<FName, double> UnloadStartTimes; // 卸载请求时间（看门狗用）

	UPROPERTY()
	TMap<FName, TObjectPtr<ULevelStreaming>> ChunkStreamingMap;

	/** 连通分量编号：相连的区块同号；不同号的区块组永不共载（传送除外）。 */
	TMap<FName, int32> ChunkComponentIds;


	/** 由连接图计算连通分量编号（OnWorldBeginPlay 时调用一次）。 */
	void BuildComponentIds();
	/** 区块的连通分量编号；不在图中返回 -1。 */
	int32 GetComponentId(FName ChunkName) const;

	double LastMissingLevelWarnTime = 0.0;
	double LastOutOfRangeWarnTime = 0.0;
	double TeleportStartTime = 0.0;

	FName PlayerChunk;
	FName StreamingChunk;
	FName HysteresisCandidate;
	float HysteresisTimer = 0.f;

	FName PendingTargetChunk;
	FVector PendingTeleportLocation = FVector::ZeroVector;
	bool bTransitionMode = false;
	TSet<FName> TransitionSet;
	int32 TeleportGeneration = 0;

	FTimerHandle UpdateTimerHandle;
	bool bDebugDraw = false;
};
