#include "Modules/ModuleManager.h"
#include "WavToolsLog.h"

DEFINE_LOG_CATEGORY(LogWavTools);

class FWavToolsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override {}
    virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FWavToolsModule, WavTools)
