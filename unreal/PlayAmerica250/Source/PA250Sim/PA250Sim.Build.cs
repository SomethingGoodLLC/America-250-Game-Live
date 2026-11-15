using System.IO;
using UnrealBuildTool;

// Thin Unreal wrapper around the canonical sim core in unreal/PA250Sim (single source of truth:
// PA250SimCore.cpp #includes the core .cpp files; nothing is copied).
public class PA250Sim : ModuleRules
{
	public PA250Sim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;       // plain C++17 core; no engine PCH
		bUseUnity = false;
		bEnableExceptions = true;             // nlohmann/json and the core use exceptions
		bEnableUndefinedIdentifierWarnings = false;
		ShadowVariableWarningLevel = WarningLevel.Off;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;

		string CoreDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "..", "..", "PA250Sim"));
		PublicIncludePaths.Add(Path.Combine(CoreDir, "include"));   // dependents: #include "pa250/sim.hpp"
		PrivateIncludePaths.AddRange(new string[] {
			CoreDir,                                     // #include "src/turn.cpp", "tests/sim_cases.hpp"
			Path.Combine(CoreDir, "src"),
			Path.Combine(CoreDir, "third_party"),
		});

		PublicDependencyModuleNames.Add("Core");
		// Windows exports nothing from a DLL unless it is named: the core marks its public functions
		// with PA250_API, which is empty everywhere else. (The Mac build exports the core through the
		// visibility pragma in PA250SimCore.cpp.) Callers need no import attribute for functions.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDefinitions.Add("PA250_API=__declspec(dllexport)");
		}
	}
}
