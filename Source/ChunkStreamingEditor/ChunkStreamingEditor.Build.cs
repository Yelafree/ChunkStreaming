using UnrealBuildTool;

public class ChunkStreamingEditor : ModuleRules
{
	public ChunkStreamingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// UE 5.0：FEditorStyle 位于 EditorStyle 模块（5.1+ 用 SlateCore 的 FAppStyle）
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 1)
		{
			PrivateDependencyModuleNames.Add("EditorStyle");
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ChunkStreaming"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"PropertyEditor",
			"AssetTools",
			"WorkspaceMenuStructure",
			"LevelEditor",
				"GraphEditor",
				"ToolMenus"
		});
	}
}
