using UnrealBuildTool;

public class Dwarfkit : ModuleRules
{
    public Dwarfkit(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "DwarfkitLib"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "HTTP", "WebSockets", "Json"
        });
    }
}
