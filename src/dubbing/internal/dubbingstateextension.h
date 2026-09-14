/*
* Audacity: A Digital Audio Editor
*
* Отмена изменений метаданных дубляжа через штатный UndoStack au3
* (AGENTS.md §5: параллельный механизм отмены запрещён).
*/
#pragma once

#include "au3-project-history/UndoManager.h"

#include "../dubbingtypes.h"

namespace au::dubbing {
//! Снимок домена дубляжа в состоянии Undo/Redo.
//! Сейвер регистрируется статически (registryEntry) и вызывается
//! UndoManager'ом при PushState/ModifyState; восстановление —
//! RestoreUndoRedoState при Undo/Redo (ProjectHistory::PopState).
class DubbingStateExtension final : public UndoStateExtension
{
public:
    static UndoRedoExtensionRegistry::Entry<DubbingStateExtension> registryEntry;

    explicit DubbingStateExtension(DubbingMeta snapshot);

    void RestoreUndoRedoState(AudacityProject& project) override;

private:
    DubbingMeta m_snapshot;
};
}
