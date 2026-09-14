/*
* Audacity: A Digital Audio Editor
*
* IOC-интерфейс доступа к домену дубляжа текущего проекта (M2).
* Реализация — internal/dubbingservice (регистрация в DubbingModule).
* Потребитель (QML-панель реплик) появится в M3; здесь только API + тесты.
*/
#pragma once

#include <string>
#include <vector>

#include "modularity/imoduleinterface.h"
#include "async/notification.h"
#include "io/path.h"

#include "dubbingtypes.h"

namespace au::dubbing {
//! Предупреждение о расхождении длительности референса (§6.2).
struct DurationMismatch {
    std::string guid;
    double declaredDur = 0.0; //!< dur из JSON, секунды
    double actualDur = 0.0;   //!< фактическая длительность WAV, секунды
    double diff = 0.0;        //!< actual - declared
};

//! Результат importFromJson: счётчики домена ПОСЛЕ импорта + ошибки.
struct JsonImportResult {
    bool ok = false;
    int filesTotal = 0;
    int scenesTotal = 0;
    int linesTotal = 0;
    int linesAdded = 0; //!< добавлено в этом проходе (инкрементальность по guid)
    std::vector<std::string> errors;
};

//! Результат importWavFolder (§6.2: импортировано / нет референса / расхождения).
struct WavImportResult {
    bool ok = false;
    int importedCount = 0;        //!< референсы, импортированные в этом проходе
    int alreadyImportedCount = 0; //!< пропущены: guid уже имеет refClipId (повтор не дублирует клип)
    int noReferenceCount = 0;     //!< без WAV -> статус NoReference, остальных не блокирует
    std::vector<DurationMismatch> mismatches; //!< предупреждения (не ошибки)
    std::vector<std::string> errors;
};

class IDubbingProject : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IDubbingProject)

public:
    virtual ~IDubbingProject() = default;

    //! Импорт метаданных реплик из JSON (UTF-8), инкрементально по guid:
    //! существующие реплики не изменяются, новые file_id/quest_id/guid добавляются.
    virtual JsonImportResult importFromJson(const muse::io::path_t& path) = 0;

    //! Массовый импорт WAV-референсов из папки (рекурсивно, "{guid}.wav"),
    //! одна референсная дорожка "REF <file_id>" на файл игры.
    virtual WavImportResult importWavFolder(const muse::io::path_t& folder) = 0;

    //! Правка RU-текста реплики (undo — штатный pushHistoryState, как в M1).
    virtual bool setLineRu(const std::string& guid, const std::string& text) = 0;

    //! Уведомление об изменении домена (для панели реплик M3).
    virtual muse::async::Notification domainChanged() const = 0;
};
}
