#pragma once

#include "CoreMinimal.h"
#include "SGraphNode.h"

class UChunkGraphNode;
class SVerticalBox;

/** 拖拽状态：SChunkGraphNode 写入，画线策略（ChunkGraphSchema）读取绘制。 */
struct FChunkNodeDragState
{
	TWeakPtr<class SChunkGraphNode> SourceNode;
	TWeakPtr<class SChunkGraphNode> HoverTarget;
	FVector2D StartScreenPos = FVector2D::ZeroVector; // 按下点（绝对坐标）
	FVector2D MouseScreenPos = FVector2D::ZeroVector; // 当前鼠标（绝对坐标，与节点几何同坐标系）
	FVector2D StartGraphPos = FVector2D::ZeroVector;  // 按下点（图坐标，增量基准）
	FVector2D StartNodePos = FVector2D::ZeroVector;   // 按下时节点位置（图坐标）
	bool bLineDrag = false;                           // true = 拖线模式（边缘按下）；false = 未激活
	bool bActive = false;
};

/** 获取全局拖拽状态（拖线中）。 */
FChunkNodeDragState& GetChunkNodeDragState();

/**
 * 区块节点（动画状态机风格）：无引脚卡片。
 * 交互（任意位置按下）：
 * - 拖到另一个区块上松手 = 建立无向连接（拖拽中节点弹回原位，出现预览线，目标变绿）
 * - 拖到空白处松手 = 移动节点（拖动中节点跟随鼠标）
 * - 原地松手 = 选中节点
 * - Ctrl+点击 = 断开该区块所有连接
 */
class SChunkGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SChunkGraphNode) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UChunkGraphNode* InNode);

	//~ Begin SWidget
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	//~ End SWidget

	//~ Begin SGraphNode
	virtual void UpdateGraphNode() override;
	//~ End SGraphNode

	FSlateColor GetBorderBackgroundColor() const;
	const FSlateBrush* GetNameIcon() const;

	UChunkGraphNode* GetChunkNode() const { return ChunkGraphNode; }

private:
	void CreateConnectionTo(SChunkGraphNode* Target);

	/** 按区块名查找节点 widget（注册表）。 */
	TSharedPtr<SChunkGraphNode> FindChunkNode(FName ChunkName) const;

	/** 把点限制在矩形内。 */
	static FVector2D ClampToRect(const FVector2D& P, const FVector2D& Min, const FVector2D& Max);

	UChunkGraphNode* ChunkGraphNode = nullptr;
	bool bNodeHovered = false;
};
