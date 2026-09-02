#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "ChunkTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "GraphEditor.h"

class UChunkGraphAsset;
class UChunkGraphEdGraph;
class FChunkViewportVisualizer;
struct FChunkInfo;

/**
 * Chunk Graph 编辑面板：节点连接图画布 + 详情编辑 + 校验。
 * - 无向连接：按住节点右侧圆点拖到另一个区块即建立连接（动画状态机式）
 * - 背景块节点 Ref → 玩法块节点 Ref（OR 引用）
 * - 连线改动自动写回图资产；Refresh Bounds / Auto Graph 自动同步回画布
 */
class SChunkGraphEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChunkGraphEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// ---- 数据 ----
	TWeakObjectPtr<UChunkGraphAsset> Asset;
	FChunkViewportVisualizer* Visualizer = nullptr;

	// ---- 节点图画布 ----
	TSharedPtr<SVerticalBox> GraphAreaBox;
	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TObjectPtr<UChunkGraphEdGraph> EdGraph;
	FDelegateHandle GraphChangedHandle;
	bool bIsSyncingGraph = false;

	// ---- UI 状态 ----
	TArray<TSharedPtr<FChunkInfo>> ChunkItems;                // 排序视图（连接组合框等使用）
	FName SelectedChunkName;
	TSharedPtr<SListView<TSharedPtr<FChunkInfo>>> ChunkListView;
	TSharedPtr<SVerticalBox> DetailsBox;
	TSharedPtr<SVerticalBox> ConnectionBox;
	TSharedPtr<SVerticalBox> ValidationBox;
	TArray<TSharedPtr<FName>> ChunkNameItems;                 // 连接组合框选项
	TSharedPtr<FName> ComboFromSel;
	TSharedPtr<FName> ComboToSel;

	// ---- 操作 ----
	void SetAsset(UChunkGraphAsset* InAsset);
	void RefreshChunkItems();
	void RebuildDetails();
	void RebuildConnections();
	void RebuildValidation();
	void DoNewAsset();
	void DoSave();
	void DoRefreshBounds();
	void DoAutoGraph();
	void DoAutoAssignBackgrounds();
	void DoValidate();
	void DoIsolateSelected();
	void DoShowAll();
	void DoAddConnection();
	void DoRemoveConnection(FName A, FName B);
	void SetCategory(EChunkCategory Category);
	void SetChunkBool(bool (FChunkInfo::*Member), bool bValue);
	void ToggleVisibleFrom(FName OwnerName);

	// ---- 节点图 ----
	/** 创建/重建节点图编辑器控件（资产变化或布局变化时调用）。 */
	void RebuildGraphEditor();
	/** 用资产数据刷新画布（节点/引脚/连线）。 */
	void RefreshGraphFromAsset();
	/** 画布变化回调：写回资产。 */
	void HandleGraphChanged(const FEdGraphEditAction& InAction);
	/** 画布选中变化回调：联动详情面板。 */
	void HandleGraphSelectionChanged(const FGraphPanelSelectionSet& InSelection);

	const FChunkInfo* GetSelectedInfo() const;
	FChunkInfo* GetSelectedInfoMutable() const;
	UWorld* GetEditorWorld() const;
	FText GetCategoryText(EChunkCategory Category) const;
	FLinearColor GetCategoryColor(EChunkCategory Category) const;
	FText GetChunkRowText(const FChunkInfo& Info) const;
	void Notify(const FString& Message) const;

	// ---- Slate 回调 ----
	TSharedRef<ITableRow> OnGenerateChunkRow(TSharedPtr<FChunkInfo> Item, const TSharedRef<STableViewBase>& OwnerTable);
	void OnChunkSelectionChanged(TSharedPtr<FChunkInfo> Item, ESelectInfo::Type SelectInfo);
};
