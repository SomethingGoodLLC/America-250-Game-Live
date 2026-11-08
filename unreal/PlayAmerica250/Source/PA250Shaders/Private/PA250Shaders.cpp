#include "PA250Shaders.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

void FPA250ShadersModule::StartupModule()
{
	const FString ShaderDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"));
	// UE 5.8 already maps /Project -> <Project>/Shaders when that folder exists;
	// mapping it again asserts at editor startup.
	if (!AllShaderSourceDirectoryMappings().Contains(TEXT("/Project")))
	{
		AddShaderSourceDirectoryMapping(TEXT("/Project"), ShaderDir);
	}
}

void FPA250ShadersModule::ShutdownModule() {}

IMPLEMENT_MODULE(FPA250ShadersModule, PA250Shaders)
