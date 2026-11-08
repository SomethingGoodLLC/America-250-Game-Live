#include "Modules/ModuleManager.h"
#if WITH_EDITOR
#include "Engine/Texture2D.h"
#include "Misc/CoreDelegates.h"
#include "TextureCompiler.h"
#endif

// Editor-only workaround, host side: the plugin's UMapObject::PostLoad reads its lookup texture on the CPU.
// When L_Colonies1775 loads, DA_Colonies1775 is post-loaded before that texture has platform data, which
// crashes in ReadTextureToArray. Loading and compiling the texture once before any map loads avoids it.
class FPortHostModule : public FDefaultGameModuleImpl {
 virtual void StartupModule() override {
#if WITH_EDITOR
  FCoreDelegates::OnPostEngineInit.AddLambda([] {
   if (IsRunningCommandlet()) return;
   if (UTexture2D* Lookup = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Colonies1775/T_Colonies1775_Lookup.T_Colonies1775_Lookup"), nullptr, LOAD_NoWarn)) {
    FTextureCompilingManager::Get().FinishCompilation({Lookup});
    Lookup->AddToRoot();
   }
  });
#endif
 }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FPortHostModule, PortHost, "PortHost");
