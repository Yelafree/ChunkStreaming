#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class UChunkGraphAsset;

/**
 * 在关卡编辑器视口中绘制区块包围盒与连接线（持久线，数据变化时重绘）。
 */
class FChunkViewportVisualizer
{
public:
	void SetAsset(UChunkGraphAsset* InAsset);
	void SetSelectedChunk(FName InSelectedChunk);
	void SetEnabled(bool bInEnabled);
	bool IsEnabled() const { return bEnabled; }

	/** 清空并重绘。 */
	void Redraw();

	/** 清空所有绘制。 */
	void Flush();

private:
	UWorld* GetEditorWorld() const;
	FColor GetChunkColor(class UChunkGraphAsset* Asset, const struct FChunkInfo& Info) const;

	TWeakObjectPtr<UChunkGraphAsset> Asset;
	FName SelectedChunk;
	bool bEnabled = false;
};
