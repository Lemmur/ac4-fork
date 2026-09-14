/*
* Audacity: A Digital Audio Editor
*
* Домен дубляжа: иерархия файл игры -> сцена -> реплика (AGENTS.md §6.1, §6.2).
* Хранится внутри .aup3/.aup4 как тег <dubbing> корневого элемента <project>
* (ProjectFileIORegistry::ObjectWriterEntry, см. internal/dubbingproject.cpp).
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace au::dubbing {
//! Статус реплики (колонка «статус» панели 6.3, фильтры экспорта 6.8)
enum class LineStatus : int {
    New = 0,          //!< импортирована из JSON, работа не начата
    NoReference = 1,  //!< референсный WAV не найден при импорте
    InProgress = 2,   //!< есть тейки, мастер ещё не собран
    Ready = 3,        //!< мастер-клип собран
    Exported = 4,     //!< экспортирован в WAV (6.8)
};

//! Тейк: ссылка на записанный клип (TrackId, ClipId — trackedittypes.h)
struct TakeInfo {
    int64_t trackId = 0;
    int64_t clipId = 0;
    bool markedBest = false; //!< ручная маркировка «лучший» (6.5)
};

//! Реплика дубляжа (guid = ключ сопоставления с WAV-референсом)
struct Line {
    std::string guid;
    std::string en;
    std::string ru;
    std::string speakerName;      //!< пустое имя -> "UNKNOWN" на импорте (§6.2)
    std::string speakerInternal;  //!< Character.Main.Coen и т.п.
    double dur = 0.0;             //!< длительность референса из JSON, секунды
    int orderIndex = 0;           //!< порядок ключей JSON = порядок реплик (§4.2)
    LineStatus status = LineStatus::New;

    //! Ссылки на аудио (0 = отсутствует)
    int64_t refTrackId = 0;
    int64_t refClipId = 0;
    std::vector<TakeInfo> takes;
    int64_t masterTrackId = 0;
    int64_t masterClipId = 0;
};

//! Сцена (quest_id)
struct Scene {
    std::string questId;
    std::vector<Line> lines;
};

//! Файл игры (file_id)
struct GameFile {
    std::string fileId;
    std::vector<Scene> scenes;
};

//! Корневые метаданные дубляж-проекта.
//! Стратегия (roadmap §2): один дубляж-проект = один файл игры,
//! поэтому files обычно содержит один элемент.
struct DubbingMeta {
    bool isDubbing = false;   //!< признак типа проекта «Дубляж»
    int schemaVersion = 1;   //!< версия схемы для миграций
    std::vector<GameFile> files;
};
}
