using UnrealBuildTool;
using System.IO;

public class Opus : ModuleRules
{
    public Opus(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string Root = ModuleDirectory; // .../Source/ThirdParty/Opus
        PublicIncludePaths.Add(Path.Combine(Root, "Include"));

        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            string LibPath = Path.Combine(Root, "Lib", "Win64", "Release");
            PublicAdditionalLibraries.Add(Path.Combine(LibPath, "opus.lib"));
        }
        else
        {
            // TODO: �������� Linux/Mac/Android �� ���� �������������
        }

        // ���� �� ������� �� ������� ���������:
        bEnableExceptions = true;
    }
}
