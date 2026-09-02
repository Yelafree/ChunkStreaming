#include "ChunkGraphNode.h"

#include "EdGraph/EdGraphPin.h"
#include "SGraphNode.h"

#include "SChunkGraphNode.h"

void UChunkGraphNode::AllocateDefaultPins()
{
	// 隐藏连接标识引脚：不显示、不参与绘制，仅用于画线策略的关联（Alt+点击断线命中）
	UEdGraphPin* InPin = CreatePin(EGPD_Input, TEXT("ChunkFlow"), TEXT("In"));
	InPin->bHidden = true;
	UEdGraphPin* OutPin = CreatePin(EGPD_Output, TEXT("ChunkFlow"), TEXT("Out"));
	OutPin->bHidden = true;
}

UEdGraphPin* UChunkGraphNode::GetInPin() const
{
	return FindPin(TEXT("In"), EGPD_Input);
}

UEdGraphPin* UChunkGraphNode::GetOutPin() const
{
	return FindPin(TEXT("Out"), EGPD_Output);
}

FText UChunkGraphNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	const FString Name = DisplayName.IsEmpty() ? ChunkName.ToString() : DisplayName;
	const FString TypeName = (Category == EChunkCategory::Background) ? TEXT("BG") : (Category == EChunkCategory::Persistent) ? TEXT("Const") : TEXT("Chunk");
	return FText::FromString(FString::Printf(TEXT("%s  %s\n[%.0f ~ %.0f]"), *Name, *TypeName, XRange.X, XRange.Y));
}

FLinearColor UChunkGraphNode::GetNodeTitleColor() const
{
	if (bStartChunk)
	{
		return FLinearColor(1.f, 0.75f, 0.15f); // 出生块：黄
	}
	switch (Category)
	{
	case EChunkCategory::Background:
		return FLinearColor(0.55f, 0.55f, 0.6f); // 灰
	case EChunkCategory::Persistent:
		return FLinearColor(0.3f, 0.85f, 0.4f);  // 绿
	default:
		return FLinearColor(0.25f, 0.55f, 0.95f); // 蓝
	}
}

TSharedPtr<SGraphNode> UChunkGraphNode::CreateVisualWidget()
{
	return SNew(SChunkGraphNode, this);
}

void UChunkGraphNode::SyncFromChunkInfo(const FChunkInfo& Info)
{
	ChunkName = Info.LevelName;
	Category = Info.Category;
	bStartChunk = Info.bStartChunk;
	bVisibleFromAll = Info.bVisibleFromAll;
	DisplayName = Info.DisplayName;
	XRange = Info.XRange;
}

void UChunkGraphNode::SyncToChunkInfo(FChunkInfo& Info) const
{
	Info.Category = Category;
	Info.bStartChunk = bStartChunk;
	Info.bVisibleFromAll = bVisibleFromAll;
	Info.DisplayName = DisplayName;
}
