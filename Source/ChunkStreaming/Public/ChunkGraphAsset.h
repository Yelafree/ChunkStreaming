#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "ChunkTypes.h"
#include "ChunkGraphAsset.generated.h"

#if WITH_EDITORONLY_DATA
class UEdGraph;
#endif

/**
 * 区块连接图资产：编辑器工具（ChunkGraphBuilder）生成与维护，
 * 运行时由 UChunkStreamingSubsystem 读取驱动流送。
 */
UCLASS(BlueprintType)
class CHUNKSTREAMING_API UChunkGraphAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunks")
	TArray<FChunkInfo> Chunks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunks")
	TArray<FChunkConnection> Connections;

	/** 自动建图容差：两块区间间距小于该值视为相邻。 */
	UPROPERTY(EditAnywhere, Category = "Build")
	float ConnectionTolerance = 200.f;

	/** 横板移动轴（X 或 Y），决定 XRange 的投影方向与运行时坐标采样。 */
	UPROPERTY(EditAnywhere, Category = "Build")
	TEnumAsByte<EAxis::Type> MovementAxis = EAxis::X;

	// ---- 运行时查询（蓝图可用） ----

	/** 返回 X 坐标（沿移动轴）所在的 Gameplay 区块；不在任何区块内返回 None。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	FName FindChunkAtX(float X) const;

	/** 3D 位置判定：优先取"坐标真正落在包围盒内"的范围最小的区块；未命中时回退 FindChunkAtX。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	FName FindChunkAtLocation(const FVector& Location) const;

	/**
	 * 智能判定（运行时内部用）：候选集合（3D 包含）中按以下顺序选择：
	 * 1) 当前所在区块（保持稳定，防重叠区抖动）；
	 * 2) 与当前区块相连的候选（沿连接进入，重叠的相邻块按范围最小）；
	 * 3) 范围最小（站中站/传送房等不相连重叠）。
	 * 3D 未命中时按同规则回退纯轴判定。CurrentChunk 为空时退化为范围最小优先。
	 */
	FName FindChunkAtLocationSmart(const FVector& Location, FName CurrentChunk) const;

	/** 查找区块信息；返回 false 表示不存在。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	bool FindChunkInfo(FName LevelName, FChunkInfo& OutInfo) const;

	/** 返回指定区块的邻居（无向：相连即返回）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	void GetNeighbors(FName LevelName, TArray<FName>& OutNeighbors) const;

	/** 两个区块之间是否存在连接（任意方向）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Streaming")
	bool HasConnection(FName LevelA, FName LevelB) const;

	/** 把世界坐标投影到移动轴上的分量。 */
	float GetAxisCoord(const FVector& V) const;

#if WITH_EDITORONLY_DATA
	/** 编辑器节点连接图（仅编辑器使用，打包时自动剔除）。 */
	UPROPERTY()
	TObjectPtr<UEdGraph> EditorGraph;
#endif

	/** 内部查询（非蓝图）：返回区块信息指针，不存在返回 nullptr。 */
	const FChunkInfo* FindChunkInfoPtr(FName LevelName) const;
};
