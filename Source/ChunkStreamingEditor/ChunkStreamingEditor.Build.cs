using UnrealBuildTool;

public class ChunkStreamingEditor : ModuleRules
{
	public ChunkStreamingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

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
