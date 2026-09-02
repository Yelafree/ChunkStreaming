#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

class FChunkViewportVisualizer;
class FExtender;

/**
 * ChunkStreamingEditor：区块连接图编辑工具（Window -> Tools -> Chunk Graph Editor / 工具栏按钮）。
 */
class FChunkStreamingEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** 视口可视化器（编辑器世界绘制区块包围盒与连接线）。 */
	FChunkViewportVisualizer& GetVisualizer() { return *Visualizer; }

	static FChunkStreamingEditorModule& Get();

	static const FName TabName;

private:
	static TSharedRef<class SDockTab> SpawnGraphEditorTab(const class FSpawnTabArgs& SpawnTabArgs);
	void AddToolbarButton();

	TSharedPtr<FChunkViewportVisualizer> Visualizer;
	TArray<TSharedPtr<FExtender>> ToolbarExtenders;
};
