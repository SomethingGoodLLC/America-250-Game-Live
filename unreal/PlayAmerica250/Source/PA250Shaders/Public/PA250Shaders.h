#pragma once
#include "Modules/ModuleManager.h"

class FPA250ShadersModule : public IModuleInterface
{
public:
virtual void StartupModule() override;
virtual void ShutdownModule() override;
};
