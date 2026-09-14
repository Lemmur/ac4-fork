/*
* Audacity: A Digital Audio Editor
*
* Модуль дубляжа (M1: ядро домена; M2: IOC-сервис импорта).
* Линкировка статических регистраций (ProjectFileIORegistry writer/reader,
* UndoRedoExtensionRegistry) + экспорт IDubbingProject в контексте приложения
* (паттерн ImporterModule/ImporterContext).
*/
#pragma once

#include "modularity/imodulesetup.h"

namespace au::dubbing {
class DubbingService;

class DubbingModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;

    muse::modularity::IContextSetup* newContext(const muse::modularity::ContextPtr& ctx) const override;
};

class DubbingContext : public muse::modularity::IContextSetup
{
public:
    DubbingContext(const muse::modularity::ContextPtr& ctx)
        : muse::modularity::IContextSetup(ctx) {}

    void registerExports() override;
    void onDeinit() override;

private:
    std::shared_ptr<DubbingService> m_service;
};
}
