#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphSchema.h"
#include "ChunkGraphSchema.generated.h"

class UChunkGraphAsset;
class UChunkGraphNode;

/** 画布右键菜单动作（回调式）。 */
class FChunkGraphSchemaAction : public FEdGraphSchemaAction
{
public:
	FChunkGraphSchemaAction(FText InMenuDesc, FText InToolTip, TFunction<void()> InOnExecute)
		: FEdGraphSchemaAction(FText(), InMenuDesc, InToolTip, 0)
		, OnExecute(InOnExecute)
	{}

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
	{
		if (OnExecute)
		{
			OnExecute();
		}
		return nullptr;
	}

	TFunction<void()> OnExecute;
};

/**
 * 区块连接图 Schema：无向连接（只区分"相连/不相连"，无进/出概念）。
 * 连接方式：按住节点边缘拖到另一个区块（动画状态机式）。
 */
UCLASS()
class UChunkGraphSchema : public UEdGraphSchema
{
	GENERATED_BODY()

public:
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	// 节点边缘连线绘制（无箭头、最近边缘路径；含拖拽预览线）
	virtual class FConnectionDrawingPolicy* CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const override;

	/** 当前正在编辑的资产（由编辑器面板设置，供右键菜单动作使用）。 */
	static void SetActiveAsset(UChunkGraphAsset* InAsset);
	static UChunkGraphAsset* GetActiveAsset();
};
