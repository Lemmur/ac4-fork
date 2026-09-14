/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingstateextension.h"

#include "au3-project/Project.h"

#include "dubbingproject.h"

using namespace au::dubbing;

UndoRedoExtensionRegistry::Entry<DubbingStateExtension> DubbingStateExtension::registryEntry {
    [](AudacityProject& project) -> std::shared_ptr<UndoStateExtension> {
        return std::make_shared<DubbingStateExtension>(DubbingProject::Get(project).meta());
    }
};

DubbingStateExtension::DubbingStateExtension(DubbingMeta snapshot)
    : m_snapshot{std::move(snapshot)}
{
}

void DubbingStateExtension::RestoreUndoRedoState(AudacityProject& project)
{
    DubbingProject::Get(project).setMeta(m_snapshot);
}
