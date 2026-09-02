#include "ChunkViewportVisualizer.h"

#include "Editor.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

#include "ChunkGraphAsset.h"
#include "ChunkTypes.h"

UWorld* FChunkViewportVisualizer::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

FColor FChunkViewportVisualizer::GetChunkColor(UChunkGraphAsset* InAsset, const FChunkInfo& Info) const
{
	if (Info.LevelName == SelectedChunk)
	{
		return FColor::Yellow;
	}
	switch (Info.Category)
	{
	case EChunkCategory::Background:
		return FColor(120, 120, 130);
	case EChunkCategory::Persistent:
		return FColor::Green;
	default:
		return FColor(30, 90, 220);
	}
}

void FChunkViewportVisualizer::SetAsset(UChunkGraphAsset* InAsset)
{
	Asset = InAsset;
	if (bEnabled)
	{
		Redraw();
	}
}

void FChunkViewportVisualizer::SetSelectedChunk(FName InSelectedChunk)
{
	SelectedChunk = InSelectedChunk;
	if (bEnabled)
	{
		Redraw();
	}
}

void FChunkViewportVisualizer::SetEnabled(bool bInEnabled)
{
	if (bEnabled == bInEnabled)
	{
		return;
	}
	bEnabled = bInEnabled;
	if (bEnabled)
	{
		Redraw();
	}
	else
	{
		Flush();
	}
}

void FChunkViewportVisualizer::Flush()
{
	UWorld* World = GetEditorWorld();
	if (World)
	{
		FlushPersistentDebugLines(World);
	}
}

void FChunkViewportVisualizer::Redraw()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return;
	}
	Flush();

	UChunkGraphAsset* Graph = Asset.Get();
	if (!Graph)
	{
		return;
	}

	for (const FChunkInfo& Info : Graph->Chunks)
	{
		if (!Info.WorldBounds.IsValid)
		{
			continue;
		}
		DrawDebugBox(World, Info.WorldBounds.GetCenter(), Info.WorldBounds.GetExtent(),
			GetChunkColor(Graph, Info), /*bPersistentLines*/ true, /*LifeTime*/ -1.f, /*DepthPriority*/ 0, /*Thickness*/ 2.f);
	}

	// 连接线：选中块只画它的边；未选中不画（100 块全画会乱）
	if (!SelectedChunk.IsNone())
	{
		const FChunkInfo* Cur = Graph->FindChunkInfoPtr(SelectedChunk);
		if (Cur && Cur->WorldBounds.IsValid)
		{
			const FVector CurCenter = Cur->WorldBounds.GetCenter();
			for (const FChunkConnection& Conn : Graph->Connections)
			{
				FName Other = NAME_None;
				if (Conn.FromLevel == SelectedChunk)
				{
					Other = Conn.ToLevel;
				}
				else if (Conn.ToLevel == SelectedChunk)
				{
					Other = Conn.FromLevel;
				}
				if (Other.IsNone())
				{
					continue;
				}
				const FChunkInfo* OtherInfo = Graph->FindChunkInfoPtr(Other);
				if (OtherInfo && OtherInfo->WorldBounds.IsValid)
				{
					DrawDebugLine(World, CurCenter, OtherInfo->WorldBounds.GetCenter(),
						FColor::Cyan, /*bPersistentLines*/ true, /*LifeTime*/ -1.f, /*DepthPriority*/ 0, /*Thickness*/ 1.5f);
				}
			}
		}
	}

	if (GEditor)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}
