#include "ChunkGraphSchema.h"

#include "ConnectionDrawingPolicy.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "ToolMenus.h"
#include "Framework/Commands/UIAction.h"
#include "Internationalization/Text.h"

#include "ChunkGraphAsset.h"
#include "ChunkGraphEdGraph.h"
#include "ChunkGraphNode.h"
#include "SChunkGraphNode.h"

static TWeakObjectPtr<UChunkGraphAsset> GChunkActiveAsset;

void UChunkGraphSchema::SetActiveAsset(UChunkGraphAsset* InAsset)
{
	GChunkActiveAsset = InAsset;
}

UChunkGraphAsset* UChunkGraphSchema::GetActiveAsset()
{
	return GChunkActiveAsset.Get();
}

// ---------------------------------------------------------------------------------------------
// 节点边缘连线：两个节点矩形之间取"最近边缘点对"画线（无箭头）；拖拽时画预览线

namespace ChunkEdgeDraw
{
	FVector2D ClampToRect(const FVector2D& P, const FVector2D& Min, const FVector2D& Max)
	{
		return FVector2D(FMath::Clamp(P.X, Min.X, Max.X), FMath::Clamp(P.Y, Min.Y, Max.Y));
	}
	bool IsInside(const FVector2D& P, const FVector2D& Min, const FVector2D& Max)
	{
		return P.X >= Min.X && P.X <= Max.X && P.Y >= Min.Y && P.Y <= Max.Y;
	}

	/** 节点几何条目（Draw 期间有效）。 */
	struct FNodeGeom
	{
		UChunkGraphNode* Node = nullptr;
		const FArrangedWidget* AW = nullptr;
	};
}

class FChunkGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
public:
	FChunkGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
		: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements)
		, GraphObj(InGraphObj)
	{}

	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes) override
	{
		// 收集节点几何（区块名 -> 节点指针 + 屏幕几何，Draw 期间有效）
		TMap<FName, ChunkEdgeDraw::FNodeGeom> NodeGeoms;
		for (int32 i = 0; i < ArrangedNodes.Num(); ++i)
		{
			const FArrangedWidget& AW = ArrangedNodes[i];
			TSharedPtr<SChunkGraphNode> N = StaticCastSharedRef<SChunkGraphNode>(AW.Widget);
			if (N.IsValid() && N->GetChunkNode())
			{
				ChunkEdgeDraw::FNodeGeom Geom;
				Geom.Node = N->GetChunkNode();
				Geom.AW = &AW;
				NodeGeoms.Add(Geom.Node->ChunkName, Geom);
			}
		}

		if (UChunkGraphEdGraph* CG = Cast<UChunkGraphEdGraph>(GraphObj))
		{
			// 玩法连接（无向粗线；关联隐藏引脚供 Alt+点击断线命中）
			FConnectionParams P;
			P.WireThickness = 2.5f;
			P.WireColor = FLinearColor(0.85f, 0.9f, 1.f);
			for (const FChunkConnection& C : CG->GraphConnections)
			{
				const ChunkEdgeDraw::FNodeGeom* GA = NodeGeoms.Find(C.FromLevel);
				const ChunkEdgeDraw::FNodeGeom* GB = NodeGeoms.Find(C.ToLevel);
				if (GA && GB)
				{
					DrawClosestEdge(*GA, *GB, P);
				}
			}

			// 背景引用（浅色细线）
			FConnectionParams BP;
			BP.WireThickness = 1.5f;
			BP.WireColor = FLinearColor(0.6f, 0.6f, 0.65f, 0.9f);
			for (const FChunkConnection& C : CG->GraphBackgroundRefs)
			{
				const ChunkEdgeDraw::FNodeGeom* GA = NodeGeoms.Find(C.FromLevel);
				const ChunkEdgeDraw::FNodeGeom* GB = NodeGeoms.Find(C.ToLevel);
				if (GA && GB)
				{
					DrawClosestEdge(*GA, *GB, BP);
				}
			}

			// 拖拽预览线：从边缘按下点画到鼠标（直线、末端贴鼠标）；悬停在目标上时变绿
			FChunkNodeDragState& D = GetChunkNodeDragState();
			if (D.bActive && D.bLineDrag)
			{
				TSharedPtr<SChunkGraphNode> Src = D.SourceNode.Pin();
				if (Src.IsValid() && Src->GetChunkNode())
				{
					const ChunkEdgeDraw::FNodeGeom* GS = NodeGeoms.Find(Src->GetChunkNode()->ChunkName);
					if (GS)
					{
						FConnectionParams DP;
						DP.WireThickness = 3.0f;
						DP.WireColor = D.HoverTarget.IsValid()
							? FLinearColor(0.3f, 1.f, 0.5f)          // 可连接：绿
							: FLinearColor(1.f, 0.85f, 0.3f, 0.9f); // 拖拽中：亮黄
						// 起点：鼠标投影到源节点边缘（鼠标在源节点内时用按下点）——起点永远贴在节点边缘
						// 终点：鼠标当前位置（绝对坐标，与节点几何同系）
						const FVector2D SrcMin(GS->AW->Geometry.AbsolutePosition);
						const FVector2D SrcMax(GS->AW->Geometry.AbsolutePosition + GS->AW->Geometry.GetLocalSize());
						FVector2D Start = ChunkEdgeDraw::ClampToRect(D.MouseScreenPos, SrcMin, SrcMax);
						if (ChunkEdgeDraw::IsInside(D.MouseScreenPos, SrcMin, SrcMax))
						{
							Start = D.StartScreenPos;
						}
						const FVector2D End = D.MouseScreenPos;
						const FVector2D Dir = End - Start;
						DP.StartTangent = Dir;
						DP.EndTangent = Dir;
						static int32 PreviewCounter = 0;
						if ((++PreviewCounter % 5) == 1)
						{
							UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] DrawPreview Start=(%.0f,%.0f) End=(%.0f,%.0f) SrcRect=(%.0f,%.0f)-(%.0f,%.0f) MouseAbs=(%.0f,%.0f) StartPressed=(%.0f,%.0f)"),
								Start.X, Start.Y, End.X, End.Y,
								SrcMin.X, SrcMin.Y, SrcMax.X, SrcMax.Y,
								D.MouseScreenPos.X, D.MouseScreenPos.Y,
								D.StartScreenPos.X, D.StartScreenPos.Y);
						}
						DrawConnection(WireLayerID, Start, End, DP);
					}
				}
			}
		}
	}

private:
	void DrawClosestEdge(const ChunkEdgeDraw::FNodeGeom& GA, const ChunkEdgeDraw::FNodeGeom& GB, FConnectionParams& Params)
	{
		// 关联隐藏引脚：Alt+点击连线命中后由 Schema::BreakSinglePinLink 断开
		if (GA.Node && GB.Node)
		{
			Params.AssociatedPin1 = GA.Node->GetOutPin();
			Params.AssociatedPin2 = GB.Node->GetInPin();
		}
		const FVector2D MinA(GA.AW->Geometry.AbsolutePosition);
		const FVector2D MaxA(GA.AW->Geometry.AbsolutePosition + GA.AW->Geometry.GetLocalSize());
		const FVector2D MinB(GB.AW->Geometry.AbsolutePosition);
		const FVector2D MaxB(GB.AW->Geometry.AbsolutePosition + GB.AW->Geometry.GetLocalSize());
		// 最近边缘点对：各自中心投影到对方矩形
		FVector2D PA = ChunkEdgeDraw::ClampToRect((MinB + MaxB) * 0.5f, MinA, MaxA);
		FVector2D PB = ChunkEdgeDraw::ClampToRect((MinA + MaxA) * 0.5f, MinB, MaxB);
		if (ChunkEdgeDraw::IsInside(PB, MinA, MaxA) && ChunkEdgeDraw::IsInside(PA, MinB, MaxB))
		{
			// 矩形重叠：退化为中心连线
			PA = (MinA + MaxA) * 0.5f;
			PB = (MinB + MaxB) * 0.5f;
		}
		DrawConnection(WireLayerID, PA, PB, Params);
	}

	UEdGraph* GraphObj = nullptr;
};

FConnectionDrawingPolicy* UChunkGraphSchema::CreateConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float InZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj) const
{
	return new FChunkGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, InZoomFactor, InClippingRect, InDrawElements, InGraphObj);
}

// ---------------------------------------------------------------------------------------------

void UChunkGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	UEdGraph* Graph = const_cast<UEdGraph*>(ContextMenuBuilder.CurrentGraph);

	ContextMenuBuilder.AddAction(MakeShared<FChunkGraphSchemaAction>(
		FText::FromString(TEXT("Sync nodes from asset")),
		FText::FromString(TEXT("Rebuild nodes and links from the graph asset (keeps node positions)")),
		[Graph]()
		{
			if (Graph && GChunkActiveAsset.IsValid())
			{
				FChunkGraphConverter::BuildNodesFromAsset(GChunkActiveAsset.Get(), Cast<UChunkGraphEdGraph>(Graph));
			}
		}));

	ContextMenuBuilder.AddAction(MakeShared<FChunkGraphSchemaAction>(
		FText::FromString(TEXT("Auto layout")),
		FText::FromString(TEXT("Arrange all nodes by X range")),
		[Graph]()
		{
			if (Graph)
			{
				FChunkGraphConverter::AutoLayout(Cast<UChunkGraphEdGraph>(Graph));
			}
		}));

	ContextMenuBuilder.AddAction(MakeShared<FChunkGraphSchemaAction>(
		FText::FromString(TEXT("Sync graph to asset")),
		FText::FromString(TEXT("Write current links and node properties back to the graph asset")),
		[Graph]()
		{
			if (Graph && GChunkActiveAsset.IsValid())
			{
				FChunkGraphConverter::SyncAssetFromGraph(GChunkActiveAsset.Get(), Cast<UChunkGraphEdGraph>(Graph));
			}
		}));
}

void UChunkGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	if (!Menu || !Context || !Context->Node)
	{
		return;
	}
	UChunkGraphNode* Node = const_cast<UChunkGraphNode*>(Cast<UChunkGraphNode>(Context->Node));
	if (!Node)
	{
		return;
	}

	FToolMenuSection& Section = Menu->AddSection(TEXT("ChunkNode"), FText::FromString(TEXT("Chunk Actions")));

	Section.AddMenuEntry(
		TEXT("SetStartChunk"),
		FText::FromString(TEXT("Set as Start Chunk")),
		FText::FromString(TEXT("Loaded initially at runtime")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			Node->bStartChunk = !Node->bStartChunk;
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		})));

	Section.AddMenuEntry(
		TEXT("SetGameplay"),
		FText::FromString(TEXT("Set as Gameplay")),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			Node->Category = EChunkCategory::Gameplay;
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		})));

	Section.AddMenuEntry(
		TEXT("SetBackground"),
		FText::FromString(TEXT("Set as Background")),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			Node->Category = EChunkCategory::Background;
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		})));

	Section.AddMenuEntry(
		TEXT("SetPersistent"),
		FText::FromString(TEXT("Set as Persistent")),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			Node->Category = EChunkCategory::Persistent;
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		})));

	Section.AddMenuEntry(
		TEXT("ToggleVisibleFromAll"),
		FText::FromString(TEXT("Toggle: Visible From All")),
		FText::FromString(TEXT("Background chunks only: always visible")),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			Node->bVisibleFromAll = !Node->bVisibleFromAll;
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->NotifyGraphChanged();
			}
		})));

	Section.AddMenuEntry(
		TEXT("BreakLinks"),
		FText::FromString(TEXT("Delete all connections of this chunk")),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateLambda([Node]()
		{
			if (UEdGraph* Graph = Node->GetGraph())
			{
				Graph->GetSchema()->BreakNodeLinks(*Node);
				Graph->NotifyGraphChanged();
			}
		})));
}

void UChunkGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	// Alt+点击连线：断开对应的无向连接
	UChunkGraphNode* A = SourcePin ? Cast<UChunkGraphNode>(SourcePin->GetOwningNode()) : nullptr;
	UChunkGraphNode* B = TargetPin ? Cast<UChunkGraphNode>(TargetPin->GetOwningNode()) : nullptr;
	UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] BreakSinglePinLink %s %s"),
		A ? *A->ChunkName.ToString() : TEXT("(null)"),
		B ? *B->ChunkName.ToString() : TEXT("(null)"));
	UChunkGraphEdGraph* G = A ? Cast<UChunkGraphEdGraph>(A->GetGraph()) : nullptr;
	if (G && A && B)
	{
		const FName NA = A->ChunkName;
		const FName NB = B->ChunkName;
		const int32 Removed = G->GraphConnections.RemoveAll([&](const FChunkConnection& C)
		{
			return (C.FromLevel == NA && C.ToLevel == NB) || (C.FromLevel == NB && C.ToLevel == NA);
		});
		if (Removed > 0)
		{
			G->NotifyGraphChanged();
		}
	}
}

void UChunkGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	UChunkGraphNode* Node = Cast<UChunkGraphNode>(&TargetNode);
	UChunkGraphEdGraph* G = Node ? Cast<UChunkGraphEdGraph>(TargetNode.GetGraph()) : nullptr;
	if (G && Node)
	{
		const FName Name = Node->ChunkName;
		G->GraphConnections.RemoveAll([Name](const FChunkConnection& C)
		{
			return C.FromLevel == Name || C.ToLevel == Name;
		});
		G->GraphBackgroundRefs.RemoveAll([Name](const FChunkConnection& C)
		{
			return C.FromLevel == Name || C.ToLevel == Name;
		});
		if (UEdGraph* Graph = TargetNode.GetGraph())
		{
			Graph->NotifyGraphChanged();
		}
	}
}
