#include "SChunkGraphNode.h"

#include "EdGraph/EdGraph.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "SGraphPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "ChunkGraphEdGraph.h"
#include "ChunkGraphNode.h"

#define LOCTEXT_NAMESPACE "SChunkGraphNode"

static FChunkNodeDragState GChunkNodeDragState;
static TArray<TWeakPtr<SChunkGraphNode>> GAllChunkNodes;

/** 当前鼠标的"窗口系坐标"：节点几何 FGeometry::AbsolutePosition 是窗口坐标系（不含窗口在屏幕的位置），
 *  而鼠标事件的 GetScreenSpacePosition() 是屏幕坐标系（含窗口位置）——两者差一个窗口位置常量。
 *  这里减去窗口位置，得到与节点几何/画线完全同一坐标系的坐标。 */
static FVector2D GetMouseWinPos(const TSharedPtr<SGraphPanel>& Panel, const FPointerEvent& MouseEvent)
{
	FVector2D MouseWin = FVector2D(MouseEvent.GetScreenSpacePosition());
	if (Panel.IsValid())
	{
		const TSharedPtr<SWindow> Win = FSlateApplication::Get().FindWidgetWindow(Panel->AsShared());
		if (Win.IsValid())
		{
			MouseWin -= FVector2D(Win->GetPositionInScreen());
		}
	}
	return MouseWin;
}

FChunkNodeDragState& GetChunkNodeDragState()
{
	return GChunkNodeDragState;
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphNode::Construct(const FArguments& InArgs, UChunkGraphNode* InNode)
{
	ChunkGraphNode = InNode;
	GraphNode = InNode;
	SetCursor(EMouseCursor::CardinalCross);
	SetToolTipText(LOCTEXT("NodeTip", "按住节点边缘拖到另一个区块 = 建立连接（无向）\n按住节点中间拖动 = 移动节点；点击 = 选中\nCtrl+点击 = 断开本区块所有连接；Alt+点击连线 = 断开单条线"));
	GAllChunkNodes.Add(SharedThis(this));
	UpdateGraphNode();
}

void SChunkGraphNode::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	const FSlateBrush* NodeTypeIcon = GetNameIcon();
	const FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);

	const FString TitleText = ChunkGraphNode
		? (ChunkGraphNode->DisplayName.IsEmpty() ? ChunkGraphNode->ChunkName.ToString() : ChunkGraphNode->DisplayName)
		: FString();
	const FString RangeText = ChunkGraphNode
		? FString::Printf(TEXT("[%.0f ~ %.0f]"), ChunkGraphNode->XRange.X, ChunkGraphNode->XRange.Y)
		: FString();

	ContentScale.Bind(this, &SGraphNode::GetContentScale);
	GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Graph.StateNode.Body"))
			.Padding(0)
			.BorderBackgroundColor(this, &SChunkGraphNode::GetBorderBackgroundColor)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.StateNode.ColorSpill"))
				.BorderBackgroundColor(TitleShadowColor)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(18.f, 10.f))
				.Visibility(EVisibility::SelfHitTestInvisible)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage).Image(NodeTypeIcon)
					]
					+ SHorizontalBox::Slot().Padding(FMargin(6, 0, 6, 0))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TitleText))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
							.ColorAndOpacity(FLinearColor::White)
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(RangeText))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FLinearColor(0.75f, 0.75f, 0.75f))
						]
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// 交互：边缘拖线

FReply SChunkGraphNode::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !ChunkGraphNode)
	{
		return SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	// Ctrl+点击：断开该区块所有连接；Alt+点击：交给面板（引擎的"Alt+点击连线=断线"）
	if (MouseEvent.IsControlDown())
	{
		if (UChunkGraphEdGraph* G = Cast<UChunkGraphEdGraph>(ChunkGraphNode->GetGraph()))
		{
			const FName Name = ChunkGraphNode->ChunkName;
			G->GraphConnections.RemoveAll([Name](const FChunkConnection& C)
			{
				return C.FromLevel == Name || C.ToLevel == Name;
			});
			G->GraphBackgroundRefs.RemoveAll([Name](const FChunkConnection& C)
			{
				return C.FromLevel == Name || C.ToLevel == Name;
			});
			G->NotifyGraphChanged();
		}
		return FReply::Handled();
	}
	if (MouseEvent.IsAltDown())
	{
		// Alt+点击：断开"离鼠标最近"的线（自定义命中，容差 20px；不依赖引擎）
		if (UChunkGraphEdGraph* G = Cast<UChunkGraphEdGraph>(ChunkGraphNode->GetGraph()))
		{
			const FVector2D Mouse = GetMouseWinPos(GetOwnerPanel(), MouseEvent);
			FName BestA = NAME_None;
			FName BestB = NAME_None;
			float BestDist = 20.f;
			for (const FChunkConnection& C : G->GraphConnections)
			{
				TSharedPtr<SChunkGraphNode> NA = FindChunkNode(C.FromLevel);
				TSharedPtr<SChunkGraphNode> NB = FindChunkNode(C.ToLevel);
				if (!NA.IsValid() || !NB.IsValid())
				{
					continue;
				}
				const FVector2D MinA = NA->GetTickSpaceGeometry().GetAbsolutePosition();
				const FVector2D MaxA = MinA + NA->GetTickSpaceGeometry().GetLocalSize();
				const FVector2D MinB = NB->GetTickSpaceGeometry().GetAbsolutePosition();
				const FVector2D MaxB = MinB + NB->GetTickSpaceGeometry().GetLocalSize();
				FVector2D PA = ClampToRect((MinB + MaxB) * 0.5f, MinA, MaxA);
				FVector2D PB = ClampToRect((MinA + MaxA) * 0.5f, MinB, MaxB);
				const FVector2D AB = PB - PA;
				const float Len2 = AB.SizeSquared();
				float T = 0.f;
				if (Len2 > 0.f)
				{
					T = FMath::Clamp(FVector2D::DotProduct(Mouse - PA, AB) / Len2, 0.f, 1.f);
				}
				const float Dist = FVector2D::Distance(Mouse, PA + AB * T);
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestA = C.FromLevel;
					BestB = C.ToLevel;
				}
			}
			if (!BestA.IsNone())
			{
				G->GraphConnections.RemoveAll([&](const FChunkConnection& C)
				{
					return (C.FromLevel == BestA && C.ToLevel == BestB) || (C.FromLevel == BestB && C.ToLevel == BestA);
				});
				G->NotifyGraphChanged();
				UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] AltBreak %s -- %s"), *BestA.ToString(), *BestB.ToString());
				return FReply::Handled();
			}
		}
		// 未命中：交给面板（引擎的 SplineOverlap 路径）
		return SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	// 边缘带（8px）按下 = 拖线模式；节点中间按下 = 交给引擎（选择/拖动移动）
	const FVector2D Local = MyGeometry.AbsoluteToLocal(GetMouseWinPos(GetOwnerPanel(), MouseEvent));
	const FVector2D Size = GetTickSpaceGeometry().GetLocalSize();
	const float Band = 8.f;
	const bool bOnEdge = Local.X < Band || Local.X > Size.X - Band ||
		Local.Y < Band || Local.Y > Size.Y - Band;
	if (!bOnEdge)
	{
		return SGraphNode::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

	UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] EdgeDown %s (Ctrl=%d Alt=%d)"), *ChunkGraphNode->ChunkName.ToString(), MouseEvent.IsControlDown() ? 1 : 0, MouseEvent.IsAltDown() ? 1 : 0);
	FChunkNodeDragState& D = GetChunkNodeDragState();
	D.bLineDrag = true;
	D.SourceNode = SharedThis(this);
	D.HoverTarget.Reset();
	// 统一使用"窗口系坐标"（与节点几何 AbsolutePosition 同一坐标系）
	D.StartScreenPos = GetMouseWinPos(GetOwnerPanel(), MouseEvent);
	D.MouseScreenPos = D.StartScreenPos;
	D.StartNodePos = FVector2D(ChunkGraphNode->NodePosX, ChunkGraphNode->NodePosY);
	D.bActive = true;
	ChunkGraphNode->Modify();
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SChunkGraphNode::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FChunkNodeDragState& D = GetChunkNodeDragState();
	if (D.bActive && D.SourceNode.Pin().Get() == this)
	{
		static int32 MoveCounter = 0;
		if ((++MoveCounter % 10) == 1)
		{
			UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] DragMove %s mouse=(%.0f,%.0f) hover=%s"),
				*ChunkGraphNode->ChunkName.ToString(), D.MouseScreenPos.X, D.MouseScreenPos.Y,
				D.HoverTarget.IsValid() ? *D.HoverTarget.Pin()->GetChunkNode()->ChunkName.ToString() : TEXT("(none)"));
		}
		TSharedPtr<SGraphPanel> Panel = GetOwnerPanel();
		D.MouseScreenPos = GetMouseWinPos(Panel, MouseEvent);
		if (Panel.IsValid())
		{
			// 命中检测（直接用每个节点的屏幕矩形）
			static int32 HitCheckCounter = 0;
			TWeakPtr<SChunkGraphNode> NewTarget;
			for (int32 i = GAllChunkNodes.Num() - 1; i >= 0; --i)
			{
				TSharedPtr<SChunkGraphNode> Other = GAllChunkNodes[i].Pin();
				if (!Other.IsValid())
				{
					GAllChunkNodes.RemoveAtSwap(i); // 清理已销毁的实例
					continue;
				}
				if (Other.Get() == this || !Other->ChunkGraphNode)
				{
					continue;
				}
				const FGeometry& OG = Other->GetTickSpaceGeometry();
				const FVector2D Min = OG.GetAbsolutePosition();
				const FVector2D Max = Min + OG.GetLocalSize();
				if ((++HitCheckCounter % 20) == 1)
				{
					UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] HitCheck mouse=(%.0f,%.0f) node=%s rect=(%.0f,%.0f)-(%.0f,%.0f)"),
						D.MouseScreenPos.X, D.MouseScreenPos.Y, *Other->ChunkGraphNode->ChunkName.ToString(),
						Min.X, Min.Y, Max.X, Max.Y);
				}
				if (D.MouseScreenPos.X >= Min.X && D.MouseScreenPos.X <= Max.X &&
					D.MouseScreenPos.Y >= Min.Y && D.MouseScreenPos.Y <= Max.Y)
				{
					NewTarget = Other;
					break;
				}
			}
			D.HoverTarget = NewTarget;
			Panel->Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
		}
		return FReply::Handled();
	}
	return SGraphNode::OnMouseMove(MyGeometry, MouseEvent);
}

FReply SChunkGraphNode::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FChunkNodeDragState& D = GetChunkNodeDragState();
	if (D.bActive && D.SourceNode.Pin().Get() == this)
	{
		UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] MouseUp %s dist=%.1f hover=%s"),
			*ChunkGraphNode->ChunkName.ToString(), FVector2D::Distance(D.MouseScreenPos, D.StartScreenPos),
			D.HoverTarget.IsValid() ? *D.HoverTarget.Pin()->GetChunkNode()->ChunkName.ToString() : TEXT("(none)"));
		if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			TSharedPtr<SGraphPanel> Panel = GetOwnerPanel();
			if (!D.HoverTarget.IsValid() && FVector2D::Distance(D.MouseScreenPos, D.StartScreenPos) >= 4.f)
			{
				// 最后再检测一次（用松手位置）
				for (int32 i = GAllChunkNodes.Num() - 1; i >= 0; --i)
				{
					TSharedPtr<SChunkGraphNode> Other = GAllChunkNodes[i].Pin();
					if (!Other.IsValid() || Other.Get() == this || !Other->ChunkGraphNode)
					{
						continue;
					}
					const FGeometry& OG = Other->GetTickSpaceGeometry();
					const FVector2D Min = OG.GetAbsolutePosition();
					const FVector2D Max = Min + OG.GetLocalSize();
					if (D.MouseScreenPos.X >= Min.X && D.MouseScreenPos.X <= Max.X &&
						D.MouseScreenPos.Y >= Min.Y && D.MouseScreenPos.Y <= Max.Y)
					{
						D.HoverTarget = Other;
						break;
					}
				}
			}
			if (TSharedPtr<SChunkGraphNode> Target = D.HoverTarget.Pin())
			{
				// 拖到目标节点：建立连接
				CreateConnectionTo(Target.Get());
			}
			else if (FVector2D::Distance(D.MouseScreenPos, D.StartScreenPos) < 4.f)
			{
				// 原地点击：选中节点
				if (Panel.IsValid())
				{
					Panel->SelectionManager.SelectSingleNode(GraphNode);
				}
			}
			// 拖到空白：取消拖线（节点不动）
		}
		D.bActive = false;
		D.SourceNode.Reset();
		D.HoverTarget.Reset();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return SGraphNode::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SChunkGraphNode::CreateConnectionTo(SChunkGraphNode* Target)
{
	if (!ChunkGraphNode || !Target || !Target->ChunkGraphNode || Target == this)
	{
		return;
	}
	UChunkGraphEdGraph* G = Cast<UChunkGraphEdGraph>(ChunkGraphNode->GetGraph());
	if (!G)
	{
		return;
	}
	const FName A = ChunkGraphNode->ChunkName;
	const FName B = Target->ChunkGraphNode->ChunkName;
	if (A == B)
	{
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[ChunkGraph] ConnectAttempt %s -> %s (srcCat=%d dstCat=%d)"),
		*A.ToString(), *B.ToString(), (int32)ChunkGraphNode->Category, (int32)Target->ChunkGraphNode->Category);

	auto AddRef = [&G](FName BgName, FName OwnerName)
	{
		const bool bExists = G->GraphBackgroundRefs.ContainsByPredicate([&](const FChunkConnection& R)
		{
			return R.FromLevel == BgName && R.ToLevel == OwnerName;
		});
		if (!bExists)
		{
			FChunkConnection Ref;
			Ref.FromLevel = BgName;
			Ref.ToLevel = OwnerName;
			G->GraphBackgroundRefs.Add(Ref);
		}
	};

	if (ChunkGraphNode->Category == EChunkCategory::Background)
	{
		// 背景块 -> 玩法块：背景引用
		if (Target->ChunkGraphNode->Category == EChunkCategory::Gameplay)
		{
			AddRef(A, B);
			G->NotifyGraphChanged();
		}
		return;
	}

	// 玩法块源
	if (Target->ChunkGraphNode->Category == EChunkCategory::Gameplay)
	{
		// 玩法连接（无向）
		const bool bExists = G->GraphConnections.ContainsByPredicate([&](const FChunkConnection& C)
		{
			return (C.FromLevel == A && C.ToLevel == B) || (C.FromLevel == B && C.ToLevel == A);
		});
		if (!bExists)
		{
			FChunkConnection Conn;
			Conn.FromLevel = A;
			Conn.ToLevel = B;
			G->GraphConnections.Add(Conn);
			G->NotifyGraphChanged();
		}
	}
	else if (Target->ChunkGraphNode->Category == EChunkCategory::Background)
	{
		// 玩法块 -> 背景块：反向引用（等价于背景块 -> 玩法块）
		AddRef(B, A);
		G->NotifyGraphChanged();
	}
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphNode::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SGraphNode::OnMouseEnter(MyGeometry, MouseEvent);
	bNodeHovered = true;
}

void SChunkGraphNode::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SGraphNode::OnMouseLeave(MouseEvent);
	bNodeHovered = false;
}

FSlateColor SChunkGraphNode::GetBorderBackgroundColor() const
{
	FLinearColor Inactive = ChunkGraphNode ? ChunkGraphNode->GetNodeTitleColor() : FLinearColor(0.2f, 0.2f, 0.2f);
	if (bNodeHovered)
	{
		Inactive = Inactive * 1.25f; // hover 提亮：提示"这里可交互"
	}

	FChunkNodeDragState& D = GetChunkNodeDragState();
	if (D.bActive)
	{
		// 拖线目标：绿色高亮（可连接）；拖线源：提亮
		if (D.HoverTarget.Pin().Get() == this)
		{
			return FLinearColor(0.15f, 0.9f, 0.35f);
		}
		if (D.SourceNode.Pin().Get() == this)
		{
			return Inactive * 1.4f;
		}
	}

	TSharedPtr<SGraphPanel> Panel = GetOwnerPanel();
	const bool bSelected = Panel.IsValid() && GraphNode && Panel->SelectionManager.IsNodeSelected(GraphNode);
	return bSelected ? Inactive * 1.5f : Inactive;
}

const FSlateBrush* SChunkGraphNode::GetNameIcon() const
{
	if (ChunkGraphNode && ChunkGraphNode->Category == EChunkCategory::Background)
	{
		return FAppStyle::GetBrush("Graph.ConduitNode.Icon");
	}
	return FAppStyle::GetBrush("Graph.StateNode.Icon");
}

TSharedPtr<SChunkGraphNode> SChunkGraphNode::FindChunkNode(FName ChunkName) const
{
	for (int32 i = GAllChunkNodes.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<SChunkGraphNode> Other = GAllChunkNodes[i].Pin();
		if (!Other.IsValid())
		{
			GAllChunkNodes.RemoveAtSwap(i);
			continue;
		}
		if (Other->ChunkGraphNode && Other->ChunkGraphNode->ChunkName == ChunkName)
		{
			return Other;
		}
	}
	return nullptr;
}

FVector2D SChunkGraphNode::ClampToRect(const FVector2D& P, const FVector2D& Min, const FVector2D& Max)
{
	return FVector2D(FMath::Clamp(P.X, Min.X, Max.X), FMath::Clamp(P.Y, Min.Y, Max.Y));
}

#undef LOCTEXT_NAMESPACE
