using UnrealBuildTool;

public class AudioReplicator : ModuleRules
{
    public AudioReplicator(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core", "CoreUObject", "Engine", "NetCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Opus"
        });

        PublicDefinitions.Add("AUDIO_REPL_OPUS_SR=48000"); // дефолтная частота
    }
}
