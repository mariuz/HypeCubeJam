using UnrealBuildTool;

public class HypeCubeJam : ModuleRules
{
	public HypeCubeJam(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"AIModule", "GameplayTasks", "NavigationSystem", "NetCore",
			"DeveloperSettings", "Niagara", "RHI", "AudioMixer", "Slate", "SlateCore"
		});

		// Epic Online Services (lobby browser, stats, leaderboards): the C SDK through the engine's EOSShared plugin,
		// which loads the library, initialises it and ticks the platform. Sockets: the local adapter addresses.
		// The web (Wasm) has no EOS SDK and no online subsystems: there the subsystem is a stub that is always off
		// (Private/Online/HCJEOSSubsystemWeb.cpp), as with -HCJNoEOS.
		bool bWithEos = Target.Platform.ToString() != "Wasm";
		PrivateDefinitions.Add("HCJ_WITH_EOS=" + (bWithEos ? "1" : "0"));
		if (bWithEos)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "EOSShared", "EOSSDK", "Sockets" });
		}

		// The intro movie: a media player, a media texture and a file source, created at run time (no assets).
		PrivateDependencyModuleNames.AddRange(new string[] { "MediaAssets" });
	}
}
