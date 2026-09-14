/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingservice.h"

#include <algorithm>

#include "project/iaudacityproject.h"

#include "import/dubbingjsonreader.h"

#include "dubbingproject.h"

#include "log.h"

using namespace au::dubbing;

DubbingService::DubbingService(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
    , m_importService(ctx)
{
}

AudacityProject* DubbingService::currentAu3Project() const
{
    auto project = globalContext()->currentProject();
    if (!project) {
        return nullptr;
    }
    return reinterpret_cast<AudacityProject*>(project->au3ProjectPtr());
}

JsonImportResult DubbingService::importFromJson(const muse::io::path_t& path)
{
    JsonImportResult result;

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        result.errors.push_back("нет открытого проекта");
        return result;
    }

    std::vector<GameFile> parsed;
    if (!m_jsonReader.read(path, parsed, result.errors)) {
        return result;
    }

    DubbingMeta& meta = DubbingProject::Get(*prj).meta();

    //! Инкрементальность по guid (решение M1): существующие реплики не трогаем
    //! вообще — ни тексты, ни статусы, ни ссылки; добавляем только новые
    //! file_id / quest_id / guid.
    for (GameFile& parsedFile : parsed) {
        auto fileIt = std::find_if(meta.files.begin(), meta.files.end(),
                                   [&parsedFile](const GameFile& f) { return f.fileId == parsedFile.fileId; });
        if (fileIt == meta.files.end()) {
            int added = 0;
            for (const Scene& s : parsedFile.scenes) {
                added += static_cast<int>(s.lines.size());
            }
            result.linesAdded += added;
            meta.files.push_back(std::move(parsedFile));
            continue;
        }
        for (Scene& parsedScene : parsedFile.scenes) {
            auto sceneIt = std::find_if(fileIt->scenes.begin(), fileIt->scenes.end(),
                                        [&parsedScene](const Scene& s) { return s.questId == parsedScene.questId; });
            if (sceneIt == fileIt->scenes.end()) {
                result.linesAdded += static_cast<int>(parsedScene.lines.size());
                fileIt->scenes.push_back(std::move(parsedScene));
                continue;
            }
            for (Line& parsedLine : parsedScene.lines) {
                const bool exists = std::any_of(sceneIt->lines.cbegin(), sceneIt->lines.cend(),
                                                [&parsedLine](const Line& l) { return l.guid == parsedLine.guid; });
                if (!exists) {
                    sceneIt->lines.push_back(std::move(parsedLine));
                    result.linesAdded++;
                }
            }
        }
    }

    if (!meta.isDubbing) {
        meta.isDubbing = true; //!< импорт метаданных делает проект дубляжным
        result.linesAdded++;  //!< считаем переход типа проекта изменением
    }

    //! Счётчики домена после импорта.
    for (const GameFile& f : meta.files) {
        for (const Scene& s : f.scenes) {
            result.linesTotal += static_cast<int>(s.lines.size());
        }
        result.scenesTotal += static_cast<int>(f.scenes.size());
    }
    result.filesTotal = static_cast<int>(meta.files.size());

    //! Одна запись отмены на пакет; метаданные снимет DubbingStateExtension (M1).
    if (result.linesAdded > 0) {
        projectHistory()->pushHistoryState("Импорт метаданных дубляжа", "Импорт дубляжа",
                                            trackedit::UndoPushType::CONSOLIDATE);
        m_domainChanged.notify();
    }

    result.ok = true;
    return result;
}

WavImportResult DubbingService::importWavFolder(const muse::io::path_t& folder)
{
    WavImportResult result;

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        result.errors.push_back("нет открытого проекта");
        return result;
    }

    DubbingMeta& meta = DubbingProject::Get(*prj).meta();
    result = m_importService.importWav(*prj, meta, folder);

    //! ОДИН pushHistoryState на пакет (UndoPush::CONSOLIDATE, au3 UndoManager.h:153-157):
    //! дорожки/клипы снимает штатный PushState, метаданные — DubbingStateExtension.
    if (result.importedCount > 0) {
        projectHistory()->pushHistoryState("Импорт дубляжа", "Импорт дубляжа",
                                            trackedit::UndoPushType::CONSOLIDATE);
        m_domainChanged.notify();
    }

    return result;
}

bool DubbingService::setLineRu(const std::string& guid, const std::string& text)
{
    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        return false;
    }

    DubbingMeta& meta = DubbingProject::Get(*prj).meta();
    for (GameFile& file : meta.files) {
        for (Scene& scene : file.scenes) {
            for (Line& line : scene.lines) {
                if (line.guid == guid) {
                    if (line.ru == text) {
                        return true;
                    }
                    line.ru = text;
                    projectHistory()->pushHistoryState("Правка текста реплики", "Правка текста");
                    m_domainChanged.notify();
                    return true;
                }
            }
        }
    }
    return false;
}
