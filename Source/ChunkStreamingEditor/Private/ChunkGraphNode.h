#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "ChunkTypes.h"
#include "ChunkGraphNode.generated.h"

class SGraphNode;

/**
 * 区块节点：一个节点对应图资产里的一个区块（无引脚）。
 * 连接方式：按住节点边缘拖到另一个区块 = 建立无向连接（动画状态机式）。
 */
UCLASS()
class UChunkGraphNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName ChunkName;

	UPROPERTY()
	EChunkCategory Category = EChunkCategory::Gameplay;

	UPROPERTY()
	bool bStartChunk = false;

	UPROPERTY()
	bool bVisibleFromAll = false;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	FVector2D XRange = FVector2D::ZeroVector;

	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual TSharedPtr<SGraphNode> CreateVisualWidget() override;

	/** 隐藏连接标识引脚（仅用于 Alt+点击断线命中，不显示不绘制）。 */
	UEdGraphPin* GetInPin() const;
	UEdGraphPin* GetOutPin() const;

	void SyncFromChunkInfo(const FChunkInfo& Info);
	void SyncToChunkInfo(FChunkInfo& Info) const;
};
