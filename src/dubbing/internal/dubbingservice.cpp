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

void DubbingService::ensureDomainSubscribed()
{
    if (m_domainSubscribed) {
        return;
    }
    m_domainSubscribed = true;

    //! После загрузки .aup4 au3 перегенерирует TrackId/ClipId: сохранённые
    //! в домене ссылки указывают в никуда («нет референса» у всех).
    //! Восстанавливаем маппинг (REF-дорожка по имени, клипы по порядку
    //! реплик с референсом) и уведомляем панель.
    if (auto ctx = globalContext()) {
        ctx->currentProjectChanged().onNotify(this, [this] {
            if (AudacityProject* prj = currentAu3Project()) {
                DubbingProject::reconcileReferences(*prj);
                m_domainChanged.notify();
            }
        });
    }
}

JsonImportResult DubbingService::importFromJson(const muse::io::path_t& path)
{
    JsonImportResult result;

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        result.errors.push_back("нет открытого проекта");
        return result;
    }

    doImportJson(*prj, path, result);

    //! Одна запись отмены на пакет; метаданные снимет DubbingStateExtension (M1).
    if (result.linesAdded > 0) {
        projectHistory()->pushHistoryState("Импорт метаданных дубляжа", "Импорт дубляжа",
                                            trackedit::UndoPushType::CONSOLIDATE);
        m_domainChanged.notify();
    }

    return result;
}

//! Этап JSON без записи отмены: разбор + инкрементальное слияние в домен
//! (общий код importFromJson и importProject; push делает вызывающий).
void DubbingService::doImportJson(AudacityProject& prj, const muse::io::path_t& path, JsonImportResult& result)
{
    std::vector<GameFile> parsed;
    if (!m_jsonReader.read(path, parsed, result.errors)) {
        return;
    }

    DubbingMeta& meta = DubbingProject::Get(prj).meta();

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

    result.ok = true;
}

WavImportResult DubbingService::importWavFolder(const muse::io::path_t& folder)
{
    WavImportResult result;

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        result.errors.push_back("нет открытого проекта");
        return result;
    }

    doImportWav(*prj, folder, result);

    //! ОДИН pushHistoryState на пакет (UndoPush::CONSOLIDATE, au3 UndoManager.h:153-157):
    //! дорожки/клипы снимает штатный PushState, метаданные — DubbingStateExtension.
    if (result.importedCount > 0) {
        projectHistory()->pushHistoryState("Импорт дубляжа", "Импорт дубляжа",
                                            trackedit::UndoPushType::CONSOLIDATE);
        m_domainChanged.notify();
    }

    return result;
}

//! Этап WAV без записи отмены: массовый импорт референсов в REF-дорожки
//! (общий код importWavFolder и importProject; push делает вызывающий).
void DubbingService::doImportWav(AudacityProject& prj, const muse::io::path_t& folder, WavImportResult& result)
{
    DubbingMeta& meta = DubbingProject::Get(prj).meta();
    result = m_importService.importWav(prj, meta, folder);
}

ProjectImportResult DubbingService::importProject(const muse::io::path_t& jsonPath,
                                                  const muse::io::path_t& wavFolder)
{
    ProjectImportResult result;

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        result.json.errors.push_back("нет открытого проекта");
        return result;
    }

    //! Полный импорт одним undo-шагом: этапы JSON -> WAV выполняются БЕЗ
    //! промежуточной записи отмены — ограничений со стороны au3 нет,
    //! PushState зовёт вызывающий (UndoManager.cpp:237-265).
    doImportJson(*prj, jsonPath, result.json);
    if (result.json.ok) {
        doImportWav(*prj, wavFolder, result.wav);
    }
    result.ok = result.json.ok && result.wav.ok;

    //! ЕДИНАЯ запись отмены на весь импорт: дорожки/клипы снимает штатный
    //! PushState (UndoPush::CONSOLIDATE), метаданные — DubbingStateExtension.
    if (result.json.linesAdded > 0 || result.wav.importedCount > 0) {
        projectHistory()->pushHistoryState("Импорт дубляжа", "Импорт дубляжа",
                                            trackedit::UndoPushType::CONSOLIDATE);
        m_domainChanged.notify();
    }

    return result;
}

DubbingMeta DubbingService::domainSnapshot() const
{
    const_cast<DubbingService*>(this)->ensureDomainSubscribed();

    AudacityProject* prj = currentAu3Project();
    if (!prj) {
        return DubbingMeta{};
    }

    //! Ленивый reconcile: панель/первый снапшот создаются ПОСЛЕ открытия
    //! проекта (подписка на currentProjectChanged опаздывает) — проверяем
    //! ссылки и восстанавливаем их здесь; идемпотентно (после успешного
    //! reconcile проверки дёшевы и ничего не меняют).
    if (DubbingProject::referencesNeedReconcile(*prj)) {
        DubbingProject::reconcileReferences(*prj);
    }

    return DubbingProject::Get(*prj).meta(); //!< одна копия на domainChanged (панель M3)
}

bool DubbingService::isDubbingProject() const
{
    AudacityProject* prj = currentAu3Project();
    return prj && DubbingProject::Get(*prj).meta().isDubbing;
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
