// Prebuilt dwarfkit static library + headers (BLUEPRINT.md 8.1). Build the lib
// with DK_WITH_CURL=OFF and the engine's CRT:
//   cmake -S <dwarfkit> -B build-ue -DDK_WITH_CURL=OFF -DDK_BUILD_TESTS=OFF \
//         -DDK_BUILD_TOOLS=OFF -DDK_BUILD_EXAMPLES=OFF -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL
//   cmake --build build-ue --config Release --target dwarfkit
// then copy build-ue/src/Release/dwarfkit.lib into lib/<Platform>/ and the
// include/ + third_party header trees into include/.
using System.IO;
using UnrealBuildTool;

public class DwarfkitLib : ModuleRules
{
    public DwarfkitLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string IncludeDir = Path.Combine(ModuleDirectory, "include");
        PublicSystemIncludePaths.Add(IncludeDir);
        PublicSystemIncludePaths.Add(Path.Combine(IncludeDir, "third_party"));

        string LibDir = Path.Combine(ModuleDirectory, "lib", Target.Platform.ToString());
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "dwarfkit.lib"));
        }
        else
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libdwarfkit.a"));
        }
    }
}
