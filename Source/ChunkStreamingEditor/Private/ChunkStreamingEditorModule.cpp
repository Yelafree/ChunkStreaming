#include "ChunkStreamingEditorModule.h"

#include "Modules/ModuleManager.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "LevelEditor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "ChunkViewportVisualizer.h"
#include "SChunkGraphEditor.h"

#define LOCTEXT_NAMESPACE "FChunkStreamingEditorModule"

const FName FChunkStreamingEditorModule::TabName(TEXT("ChunkGraphEditor"));

void FChunkStreamingEditorModule::StartupModule()
{
	Visualizer = MakeShared<FChunkViewportVisualizer>();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateStatic(&FChunkStreamingEditorModule::SpawnGraphEditorTab))
		.SetDisplayName(LOCTEXT("ChunkGraphTabTitle", "Chunk Graph Editor"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory());

	AddToolbarButton();

	UE_LOG(LogTemp, Log, TEXT("[ChunkStreaming] Editor module loaded. Open the tool via: Window menu -> Tools -> Chunk Graph Editor, or the 'Chunk Graph' toolbar button."));
}

void FChunkStreamingEditorModule::ShutdownModule()
{
	if (Visualizer.IsValid())
	{
		Visualizer->SetEnabled(false);
		Visualizer.Reset();
	}
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
	ToolbarExtenders.Reset();
}

void FChunkStreamingEditorModule::AddToolbarButton()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension(
		TEXT("Play"),
		EExtensionHook::After,
		nullptr,
		FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& Builder)
		{
			Builder.AddToolBarButton(
				FUIAction(FExecuteAction::CreateLambda([]()
				{
					FGlobalTabmanager::Get()->TryInvokeTab(FChunkStreamingEditorModule::TabName);
				})),
				NAME_None,
				LOCTEXT("ChunkGraphButton", "Chunk Graph"),
				LOCTEXT("ChunkGraphButtonTip", "Open the Chunk Streaming graph editor"),
				FSlateIcon());
		}));
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
	ToolbarExtenders.Add(ToolbarExtender);
}

FChunkStreamingEditorModule& FChunkStreamingEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked<FChunkStreamingEditorModule>(TEXT("ChunkStreamingEditor"));
}

TSharedRef<SDockTab> FChunkStreamingEditorModule::SpawnGraphEditorTab(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			SNew(SChunkGraphEditor)
		];
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FChunkStreamingEditorModule, ChunkStreamingEditor)
