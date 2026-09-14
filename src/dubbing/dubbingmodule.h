/*
* Audacity: A Digital Audio Editor
*
* Модуль дубляжа (M1 roadmap: ядро домена).
* Пустой IModuleSetup гарантирует линковку статических регистраций
* (ProjectFileIORegistry writer/reader, UndoRedoExtensionRegistry) в приложение.
*/
#pragma once

#include "modularity/imodulesetup.h"

namespace au::dubbing {
class DubbingModule : public muse::modularity::IModuleSetup
{
public:
    std::string moduleName() const override;
};
}
