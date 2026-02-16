using UnrealBuildTool;

public class BlueprintExporter : ModuleRules
{
	public BlueprintExporter(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"BlueprintGraph",
			"KismetCompiler",
			"Json",
			"JsonUtilities",
			"HTTPServer"
		});
	}
}
