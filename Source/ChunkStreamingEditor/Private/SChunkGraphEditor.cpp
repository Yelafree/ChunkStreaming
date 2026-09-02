#include "SChunkGraphEditor.h"

#include "Editor.h"
#include "EditorLevelUtils.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "PackageTools.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Views/STableRow.h"
#include "PropertyCustomizationHelpers.h"

#include "ChunkGraphAsset.h"
#include "ChunkGraphBuilder.h"
#include "ChunkGraphEdGraph.h"
#include "ChunkGraphNode.h"
#include "ChunkGraphSchema.h"
#include "ChunkStreamingEditorModule.h"
#include "ChunkViewportVisualizer.h"
#include "EdGraph/EdGraph.h"
#include "GraphEditAction.h"

#define LOCTEXT_NAMESPACE "SChunkGraphEditor"

void SChunkGraphEditor::Construct(const FArguments& InArgs)
{
	Visualizer = &FChunkStreamingEditorModule::Get().GetVisualizer();

	ChildSlot
	[
		SNew(SVerticalBox)

		// 资产选择与文件操作
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("NewAsset", "New Asset"))
				.OnClicked_Lambda([this]() { DoNewAsset(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("SaveAsset", "Save"))
				.OnClicked_Lambda([this]() { DoSave(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UChunkGraphAsset::StaticClass())
				.ObjectPath_Lambda([this]() { return Asset.IsValid() ? Asset->GetPathName() : FString(); })
				.OnObjectChanged_Lambda([this](const FAssetData& InData) { SetAsset(Cast<UChunkGraphAsset>(InData.GetAsset())); })
			]
		]

		// 构建工具
		+ SVerticalBox::Slot().AutoHeight().Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshBounds", "Refresh Bounds"))
				.OnClicked_Lambda([this]() { DoRefreshBounds(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AutoGraph", "Auto Graph"))
				.OnClicked_Lambda([this]() { DoAutoGraph(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("AutoAssignBG", "Auto-Assign BG"))
				.OnClicked_Lambda([this]() { DoAutoAssignBackgrounds(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Validate", "Validate"))
				.OnClicked_Lambda([this]() { DoValidate(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("Isolate", "Isolate Chunk"))
				.OnClicked_Lambda([this]() { DoIsolateSelected(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SButton)
				.Text(LOCTEXT("ShowAll", "Show All"))
				.OnClicked_Lambda([this]() { DoShowAll(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(2)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("VizTip", "Draw chunk bounds and connections in the viewport"))
				.IsChecked_Lambda([this]() { return Visualizer && Visualizer->IsEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
				{
					if (Visualizer)
					{
						Visualizer->SetEnabled(State == ECheckBoxState::Checked);
						if (Visualizer->IsEnabled())
						{
							Visualizer->SetAsset(Asset.Get());
							Visualizer->SetSelectedChunk(SelectedChunkName);
						}
					}
				})
				[
					SNew(STextBlock).Text(LOCTEXT("Viz", "Viewport Viz"))
				]
			]
		]

		+ SVerticalBox::Slot().FillHeight(1.f).Padding(4)
		[
			SNew(SSplitter).Orientation(Orient_Horizontal)
			+ SSplitter::Slot().Value(0.55f)
			[
				SNew(SBorder).Padding(4)
				[
					SAssignNew(GraphAreaBox, SVerticalBox)
				]
			]
			+ SSplitter::Slot().Value(0.35f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(DetailsBox, SVerticalBox)
				]
				+ SScrollBox::Slot()
				[
					SNew(SSeparator)
				]
				+ SScrollBox::Slot()
				[
					SAssignNew(ConnectionBox, SVerticalBox)
				]
			]
			+ SSplitter::Slot().Value(0.30f)
			[
				SNew(SBorder).Padding(4)
				[
					SAssignNew(ValidationBox, SVerticalBox)
				]
			]
		]
	];

	RebuildGraphEditor();
	RebuildDetails();
	RebuildConnections();
	RebuildValidation();
}

// ---------------------------------------------------------------------------------------------

UWorld* SChunkGraphEditor::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

FText SChunkGraphEditor::GetCategoryText(EChunkCategory Category) const
{
	switch (Category)
	{
	case EChunkCategory::Background: return LOCTEXT("BG", "Background");
	case EChunkCategory::Persistent: return LOCTEXT("Persistent", "Persistent");
	default: return LOCTEXT("Gameplay", "Gameplay");
	}
}

FLinearColor SChunkGraphEditor::GetCategoryColor(EChunkCategory Category) const
{
	switch (Category)
	{
	case EChunkCategory::Background: return FLinearColor(0.5f, 0.5f, 0.55f);
	case EChunkCategory::Persistent: return FLinearColor(0.2f, 0.8f, 0.2f);
	default: return FLinearColor(0.15f, 0.4f, 0.9f);
	}
}

FText SChunkGraphEditor::GetChunkRowText(const FChunkInfo& Info) const
{
	return FText::Format(
		LOCTEXT("ChunkRow", "{0}  [{1} ~ {2}]  {3}"),
		FText::FromString(Info.DisplayName.IsEmpty() ? Info.LevelName.ToString() : Info.DisplayName),
		FText::AsNumber(Info.XRange.X),
		FText::AsNumber(Info.XRange.Y),
		GetCategoryText(Info.Category));
}

void SChunkGraphEditor::Notify(const FString& Message) const
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.bFireAndForget = true;
	Info.ExpireDuration = 3.f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphEditor::SetAsset(UChunkGraphAsset* InAsset)
{
	Asset = InAsset;
	SelectedChunkName = NAME_None;
	UChunkGraphSchema::SetActiveAsset(InAsset);
	if (Visualizer)
	{
		Visualizer->SetAsset(InAsset);
		Visualizer->SetSelectedChunk(NAME_None);
	}
	RefreshChunkItems();
	RebuildGraphEditor();
	RebuildDetails();
	RebuildConnections();
	RebuildValidation();
}

void SChunkGraphEditor::RefreshChunkItems()
{
	ChunkItems.Reset();
	if (Asset.IsValid())
	{
		for (FChunkInfo& Info : Asset->Chunks)
		{
			ChunkItems.Add(MakeShared<FChunkInfo>(Info));
		}
		ChunkItems.Sort([](const TSharedPtr<FChunkInfo>& A, const TSharedPtr<FChunkInfo>& B)
		{
			return A->XRange.X < B->XRange.X;
		});
		ChunkNameItems.Reset();
		for (FChunkInfo& Info : Asset->Chunks)
		{
			ChunkNameItems.Add(MakeShared<FName>(Info.LevelName));
		}
	}
	if (ChunkListView.IsValid())
	{
		ChunkListView->RequestListRefresh();
	}
}

const FChunkInfo* SChunkGraphEditor::GetSelectedInfo() const
{
	if (!Asset.IsValid() || SelectedChunkName.IsNone())
	{
		return nullptr;
	}
	return Asset->FindChunkInfoPtr(SelectedChunkName);
}

FChunkInfo* SChunkGraphEditor::GetSelectedInfoMutable() const
{
	if (!Asset.IsValid() || SelectedChunkName.IsNone())
	{
		return nullptr;
	}
	for (FChunkInfo& Info : Asset->Chunks)
	{
		if (Info.LevelName == SelectedChunkName)
		{
			return &Info;
		}
	}
	return nullptr;
}

TSharedRef<ITableRow> SChunkGraphEditor::OnGenerateChunkRow(TSharedPtr<FChunkInfo> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FChunkInfo>>, OwnerTable)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SColorBlock).Color(GetCategoryColor(Item->Category)).Size(FVector2D(10, 10))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(GetChunkRowText(*Item))
			]
		];
}

void SChunkGraphEditor::OnChunkSelectionChanged(TSharedPtr<FChunkInfo> Item, ESelectInfo::Type SelectInfo)
{
	SelectedChunkName = Item.IsValid() ? Item->LevelName : NAME_None;
	if (Visualizer)
	{
		Visualizer->SetSelectedChunk(SelectedChunkName);
	}
	RebuildDetails();
	RebuildConnections();
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphEditor::SetCategory(EChunkCategory Category)
{
	if (FChunkInfo* Info = GetSelectedInfoMutable())
	{
		Info->Category = Category;
		Asset->MarkPackageDirty();
		RefreshChunkItems();
		RebuildDetails();
		RefreshGraphFromAsset();
		if (Visualizer)
		{
			Visualizer->SetAsset(Asset.Get());
		}
	}
}

void SChunkGraphEditor::SetChunkBool(bool (FChunkInfo::*Member), bool bValue)
{
	if (FChunkInfo* Info = GetSelectedInfoMutable())
	{
		Info->*Member = bValue;
		Asset->MarkPackageDirty();
		RefreshChunkItems();
		RebuildDetails();
		RefreshGraphFromAsset();
	}
}

void SChunkGraphEditor::ToggleVisibleFrom(FName OwnerName)
{
	if (FChunkInfo* Info = GetSelectedInfoMutable())
	{
		if (Info->VisibleFromChunks.Contains(OwnerName))
		{
			Info->VisibleFromChunks.Remove(OwnerName);
		}
		else
		{
			Info->VisibleFromChunks.AddUnique(OwnerName);
		}
		Asset->MarkPackageDirty();
		RebuildDetails();
		RefreshGraphFromAsset();
	}
}

void SChunkGraphEditor::RebuildDetails()
{
	DetailsBox->ClearChildren();
	const FChunkInfo* Info = GetSelectedInfo();
	if (!Info)
	{
		DetailsBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(STextBlock).Text(LOCTEXT("NoSelection", "Select a chunk."))
		];
		return;
	}

	DetailsBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(STextBlock).Text(FText::Format(LOCTEXT("DetailName", "Chunk: {0}"), FText::FromName(Info->LevelName)))
	];

	// 类别
	DetailsBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(2).VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(LOCTEXT("Category", "Category:"))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { const FChunkInfo* I = GetSelectedInfo(); return (I && I->Category == EChunkCategory::Gameplay) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) SetCategory(EChunkCategory::Gameplay); })
			[ SNew(STextBlock).Text(LOCTEXT("CatGameplay", "Gameplay")) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { const FChunkInfo* I = GetSelectedInfo(); return (I && I->Category == EChunkCategory::Background) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) SetCategory(EChunkCategory::Background); })
			[ SNew(STextBlock).Text(LOCTEXT("CatBG", "Background")) ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { const FChunkInfo* I = GetSelectedInfo(); return (I && I->Category == EChunkCategory::Persistent) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { if (S == ECheckBoxState::Checked) SetCategory(EChunkCategory::Persistent); })
			[ SNew(STextBlock).Text(LOCTEXT("CatPersistent", "Persistent")) ]
		]
	];

	// 标记
	auto AddFlag = [this](const FText& Label, bool (FChunkInfo::*Member))
	{
		DetailsBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this, Member]() { const FChunkInfo* I = GetSelectedInfo(); return (I && I->*Member) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Member](ECheckBoxState S) { SetChunkBool(Member, S == ECheckBoxState::Checked); })
			[ SNew(STextBlock).Text(Label) ]
		];
	};
	AddFlag(LOCTEXT("StartChunk", "Start Chunk (出生块)"), &FChunkInfo::bStartChunk);
	AddFlag(LOCTEXT("VisibleFromAll", "Visible From All (全局背景)"), &FChunkInfo::bVisibleFromAll);
	AddFlag(LOCTEXT("AutoBounds", "Auto Bounds (自动刷新包围盒)"), &FChunkInfo::bAutoBounds);

	// 背景可见性引用
	if (Info->Category == EChunkCategory::Background)
	{
		DetailsBox->AddSlot().AutoHeight().Padding(2, 8, 2, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("VisibleFrom", "Visible From Chunks:"))
		];
		TSharedRef<SVerticalBox> OwnerList = SNew(SVerticalBox);
		for (const FChunkInfo& Owner : Asset->Chunks)
		{
			if (Owner.Category != EChunkCategory::Gameplay)
			{
				continue;
			}
			OwnerList->AddSlot().AutoHeight().Padding(8, 1)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this, OwnerName = Owner.LevelName]() { const FChunkInfo* I = GetSelectedInfo(); return (I && I->VisibleFromChunks.Contains(OwnerName)) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, OwnerName = Owner.LevelName](ECheckBoxState) { ToggleVisibleFrom(OwnerName); })
				[ SNew(STextBlock).Text(FText::FromName(Owner.LevelName)) ]
			];
		}
		DetailsBox->AddSlot().AutoHeight().MaxHeight(160)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				OwnerList
			]
		];
	}

	// 包围盒信息
	DetailsBox->AddSlot().AutoHeight().Padding(2, 8, 2, 2)
	[
		SNew(STextBlock).Text(FText::Format(
			LOCTEXT("BoundsText", "Bounds: {0}   XRange: [{1}, {2}]"),
			Info->WorldBounds.IsValid ? FText::FromString(Info->WorldBounds.ToString()) : LOCTEXT("InvalidBounds", "Invalid"),
			FText::AsNumber(Info->XRange.X), FText::AsNumber(Info->XRange.Y)))
	];
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphEditor::RebuildConnections()
{
	ConnectionBox->ClearChildren();
	const FChunkInfo* Info = GetSelectedInfo();
	if (!Info)
	{
		ConnectionBox->AddSlot().AutoHeight().Padding(2)
		[
			SNew(STextBlock).Text(LOCTEXT("NoConnSel", "Select a chunk to edit connections."))
		];
		return;
	}

	ConnectionBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(STextBlock).Text(LOCTEXT("Connections", "Connections:"))
	];

	int32 Index = 0;
	for (const FChunkConnection& Conn : Asset->Connections)
	{
		if (Conn.FromLevel != SelectedChunkName && Conn.ToLevel != SelectedChunkName)
		{
			continue;
		}
		const FName Other = (Conn.FromLevel == SelectedChunkName) ? Conn.ToLevel : Conn.FromLevel;
		const FString AutoText = Conn.bAutoGenerated ? TEXT(" [auto]") : TEXT(" [manual]");
		ConnectionBox->AddSlot().AutoHeight().Padding(8, 1)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s — %s%s"), *SelectedChunkName.ToString(), *Other.ToString(), *AutoText)))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("RemoveConn", "X"))
				.OnClicked_Lambda([this, A = Conn.FromLevel, B = Conn.ToLevel]() { DoRemoveConnection(A, B); return FReply::Handled(); })
			]
		];
		++Index;
	}
	if (Index == 0)
	{
		ConnectionBox->AddSlot().AutoHeight().Padding(8, 1)
		[
			SNew(STextBlock).Text(LOCTEXT("NoConns", "(no connections)"))
		];
	}

	// 添加连接
	ConnectionBox->AddSlot().AutoHeight().Padding(2, 8, 2, 2)
	[
		SNew(SSeparator)
	];
	ConnectionBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(STextBlock).Text(LOCTEXT("AddConn", "Add connection:"))
	];
	ConnectionBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2)
		[
			SNew(SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&ChunkNameItems)
			.OnGenerateWidget_Lambda([](TSharedPtr<FName> N) { return SNew(STextBlock).Text(N.IsValid() ? FText::FromName(*N) : FText::GetEmpty()); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FName> N, ESelectInfo::Type) { ComboFromSel = N; })
			[
				SNew(STextBlock).Text_Lambda([this]() { return (ComboFromSel.IsValid() && !ComboFromSel->IsNone()) ? FText::FromName(*ComboFromSel) : LOCTEXT("ChunkA", "Chunk A..."); })
			]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(2)
		[
			SNew(SComboBox<TSharedPtr<FName>>)
			.OptionsSource(&ChunkNameItems)
			.OnGenerateWidget_Lambda([](TSharedPtr<FName> N) { return SNew(STextBlock).Text(N.IsValid() ? FText::FromName(*N) : FText::GetEmpty()); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<FName> N, ESelectInfo::Type) { ComboToSel = N; })
			[
				SNew(STextBlock).Text_Lambda([this]() { return (ComboToSel.IsValid() && !ComboToSel->IsNone()) ? FText::FromName(*ComboToSel) : LOCTEXT("ChunkB", "Chunk B..."); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(2)
		[
			SNew(SButton)
			.Text(LOCTEXT("Connect", "Connect"))
			.ToolTipText(LOCTEXT("ConnectTip", "Add an undirected connection (manual)"))
			.OnClicked_Lambda([this]() { DoAddConnection(); return FReply::Handled(); })
		]
	];
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphEditor::RebuildValidation()
{
	ValidationBox->ClearChildren();
	ValidationBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(STextBlock).Text(LOCTEXT("ValidationTitle", "Validation:"))
	];
	TSharedRef<SVerticalBox> MessageList = SNew(SVerticalBox);
	for (const FText& Msg : FChunkGraphBuilder::Validate(Asset.Get(), GetEditorWorld()))
	{
		FLinearColor Color = FLinearColor::White;
		FString S = Msg.ToString();
		if (S.StartsWith(TEXT("[Error]"))) Color = FLinearColor(1.f, 0.3f, 0.3f);
		else if (S.StartsWith(TEXT("[Warning]"))) Color = FLinearColor(1.f, 0.8f, 0.2f);
		MessageList->AddSlot().AutoHeight().Padding(4, 1)
		[
			SNew(STextBlock).Text(Msg).ColorAndOpacity(Color).AutoWrapText(true)
		];
	}
	ValidationBox->AddSlot().FillHeight(1.f)
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			MessageList
		]
	];
}

// ---------------------------------------------------------------------------------------------

void SChunkGraphEditor::DoNewAsset()
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
	IAssetTools& AssetTools = IAssetTools::Get();
#else
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
#endif
	FString PkgName, AssetName;
	AssetTools.CreateUniqueAssetName(TEXT("/Game/ChunkStreaming/ChunkGraph_Main"), TEXT(""), PkgName, AssetName);
	UChunkGraphAsset* NewAsset = Cast<UChunkGraphAsset>(
		AssetTools.CreateAsset(AssetName, TEXT("/Game/ChunkStreaming"), UChunkGraphAsset::StaticClass(), nullptr));
	if (NewAsset)
	{
		SetAsset(NewAsset);
		Notify(FString::Printf(TEXT("Created %s. Use 'Refresh Bounds' to fill it from streaming levels."), *AssetName));
	}
}

void SChunkGraphEditor::DoSave()
{
	if (!Asset.IsValid())
	{
		Notify(TEXT("No asset to save."));
		return;
	}
	TArray<UObject*> Objects;
	Objects.Add(Asset.Get());
	const bool bSaved = UPackageTools::SavePackagesForObjects(Objects);
	Notify(bSaved ? TEXT("Saved.") : TEXT("Save failed."));
}

void SChunkGraphEditor::DoRefreshBounds()
{
	if (!Asset.IsValid())
	{
		Notify(TEXT("Select or create a graph asset first."));
		return;
	}
	FChunkGraphBuilder::RefreshChunkInfos(Asset.Get(), GetEditorWorld());
	RefreshChunkItems();
	RebuildDetails();
	RefreshGraphFromAsset();
	if (Visualizer)
	{
		Visualizer->SetAsset(Asset.Get());
	}
	Notify(TEXT("Bounds refreshed."));
}

void SChunkGraphEditor::DoAutoGraph()
{
	if (!Asset.IsValid())
	{
		Notify(TEXT("Select or create a graph asset first."));
		return;
	}
	FChunkGraphBuilder::AutoConnect(Asset.Get());
	RefreshChunkItems();
	RebuildConnections();
	RefreshGraphFromAsset();
	if (Visualizer)
	{
		Visualizer->SetAsset(Asset.Get());
	}
	Notify(TEXT("Auto graph done (auto edges refreshed, manual edges kept)."));
}

void SChunkGraphEditor::DoAutoAssignBackgrounds()
{
	if (!Asset.IsValid())
	{
		Notify(TEXT("Select or create a graph asset first."));
		return;
	}
	FChunkGraphBuilder::AutoAssignBackgrounds(Asset.Get());
	RebuildDetails();
	RefreshGraphFromAsset();
	Notify(TEXT("Background references auto-assigned."));
}

void SChunkGraphEditor::DoValidate()
{
	RebuildValidation();
}

void SChunkGraphEditor::DoIsolateSelected()
{
	UWorld* World = GetEditorWorld();
	if (!World || !Asset.IsValid() || SelectedChunkName.IsNone())
	{
		Notify(TEXT("Select a chunk first."));
		return;
	}
	TArray<FName> Neighbors;
	Asset->GetNeighbors(SelectedChunkName, Neighbors);

	TArray<ULevel*> Levels;
	TArray<bool> Visible;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL || !SL->GetLoadedLevel())
		{
			continue;
		}
		const FName Pkg = FName(*SL->GetWorldAssetPackageName());
		Levels.Add(SL->GetLoadedLevel());
		Visible.Add(Pkg == SelectedChunkName || Neighbors.Contains(Pkg));
	}
	UEditorLevelUtils::SetLevelsVisibility(Levels, Visible, /*bForceLayersVisible*/ false, ELevelVisibilityDirtyMode::ModifyOnChange);
	Notify(TEXT("Isolated selected chunk + neighbors."));
}

void SChunkGraphEditor::DoShowAll()
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return;
	}
	TArray<ULevel*> Levels;
	TArray<bool> Visible;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL || !SL->GetLoadedLevel())
		{
			continue;
		}
		Levels.Add(SL->GetLoadedLevel());
		Visible.Add(true);
	}
	UEditorLevelUtils::SetLevelsVisibility(Levels, Visible, /*bForceLayersVisible*/ false, ELevelVisibilityDirtyMode::ModifyOnChange);
	Notify(TEXT("All chunks visible."));
}

void SChunkGraphEditor::DoAddConnection()
{
	if (!Asset.IsValid() || !ComboFromSel.IsValid() || !ComboToSel.IsValid() || ComboFromSel->IsNone() || ComboToSel->IsNone())
	{
		return;
	}
	const FName A = *ComboFromSel;
	const FName B = *ComboToSel;
	if (A == B)
	{
		return;
	}
	if (Asset->HasConnection(A, B))
	{
		Notify(TEXT("Connection already exists."));
		return;
	}
	FChunkConnection Conn;
	Conn.FromLevel = A;
	Conn.ToLevel = B;
	Conn.bAutoGenerated = false;
	Asset->Connections.Add(Conn);
	Asset->MarkPackageDirty();
	RebuildConnections();
	RefreshGraphFromAsset();
	if (Visualizer)
	{
		Visualizer->SetAsset(Asset.Get());
	}
}

void SChunkGraphEditor::DoRemoveConnection(FName A, FName B)
{
	if (!Asset.IsValid())
	{
		return;
	}
	Asset->Connections.RemoveAll([&](const FChunkConnection& Conn)
	{
		return (Conn.FromLevel == A && Conn.ToLevel == B) || (Conn.FromLevel == B && Conn.ToLevel == A);
	});
	Asset->MarkPackageDirty();
	RebuildConnections();
	RefreshGraphFromAsset();
	if (Visualizer)
	{
		Visualizer->SetAsset(Asset.Get());
	}
}


// ---------------------------------------------------------------------------------------------
// 节点图

void SChunkGraphEditor::RebuildGraphEditor()
{
	if (!GraphAreaBox.IsValid())
	{
		return;
	}
	GraphAreaBox->ClearChildren();

	if (!Asset.IsValid())
	{
		GraphEditorWidget.Reset();
		return;
	}

	if (GraphChangedHandle.IsValid() && EdGraph)
	{
		EdGraph->RemoveOnGraphChangedHandler(GraphChangedHandle);
		GraphChangedHandle.Reset();
	}

	UChunkGraphSchema::SetActiveAsset(Asset.Get());

	EdGraph = Cast<UChunkGraphEdGraph>(Asset->EditorGraph);
	if (!EdGraph)
	{
		EdGraph = NewObject<UChunkGraphEdGraph>(Asset.Get(), UChunkGraphEdGraph::StaticClass(), TEXT("ChunkEditorGraph"), RF_Transactional);
		Asset->EditorGraph = EdGraph;
	}
	EdGraph->Schema = UChunkGraphSchema::StaticClass();

	bIsSyncingGraph = true;
	FChunkGraphConverter::BuildNodesFromAsset(Asset.Get(), EdGraph);
	bIsSyncingGraph = false;

	GraphChangedHandle = EdGraph->AddOnGraphChangedHandler(FOnGraphChanged::FDelegate::CreateRaw(this, &SChunkGraphEditor::HandleGraphChanged));

	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("CornerText", "区块连接图：无向连接，只区分相连/不相连；背景块->玩法块=背景引用");
	Appearance.InstructionText = LOCTEXT("InstructionText", "按住区块边缘拖到另一个区块 = 建立连接；Ctrl+点击区块 = 断开其所有连接");

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateRaw(this, &SChunkGraphEditor::HandleGraphSelectionChanged);

	GraphEditorWidget = SNew(SGraphEditor)
		.GraphToEdit(EdGraph)
		.IsEditable(true)
		.Appearance(Appearance)
		.GraphEvents(Events)
		.ShowGraphStateOverlay(false);

	GraphAreaBox->AddSlot().FillHeight(1.f)
	[
		GraphEditorWidget.ToSharedRef()
	];
}

void SChunkGraphEditor::RefreshGraphFromAsset()
{
	if (!Asset.IsValid() || !EdGraph)
	{
		return;
	}
	bIsSyncingGraph = true;
	FChunkGraphConverter::BuildNodesFromAsset(Asset.Get(), EdGraph);
	bIsSyncingGraph = false;
}

void SChunkGraphEditor::HandleGraphChanged(const FEdGraphEditAction& InAction)
{
	if (bIsSyncingGraph || !Asset.IsValid() || !EdGraph)
	{
		return;
	}

	// 节点被删除：区块由资产管理，自动恢复（只允许删除连线）
	if ((InAction.Action & GRAPHACTION_RemoveNode) != 0)
	{
		bIsSyncingGraph = true;
		FChunkGraphConverter::BuildNodesFromAsset(Asset.Get(), EdGraph);
		bIsSyncingGraph = false;
		return;
	}

	bIsSyncingGraph = true;
	FChunkGraphConverter::SyncAssetFromGraph(Asset.Get(), EdGraph);
	bIsSyncingGraph = false;

	RefreshChunkItems();
	RebuildDetails();
	RebuildConnections();
	RebuildValidation();
	if (Visualizer)
	{
		Visualizer->SetAsset(Asset.Get());
	}
}

void SChunkGraphEditor::HandleGraphSelectionChanged(const FGraphPanelSelectionSet& InSelection)
{
	FName NewSelection = NAME_None;
	for (UObject* Obj : InSelection)
	{
		if (UChunkGraphNode* Node = Cast<UChunkGraphNode>(Obj))
		{
			NewSelection = Node->ChunkName;
			break;
		}
	}
	SelectedChunkName = NewSelection;
	if (Visualizer)
	{
		Visualizer->SetSelectedChunk(SelectedChunkName);
	}
	RebuildDetails();
	RebuildConnections();
}

#undef LOCTEXT_NAMESPACE
