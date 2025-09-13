#include "Modules/ModuleManager.h"

class FAudioReplicatorModule : public IModuleInterface
{
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};
IMPLEMENT_MODULE(FAudioReplicatorModule, AudioReplicator)
