/*
* Audacity: A Digital Audio Editor
*/
#include "dubbingimportservice.h"

#include <QDirIterator>
#include <QRegularExpression>
#include <QString>

#include <algorithm>
#include <map>

#include "au3wrap/au3types.h"
#include "au3wrap/internal/domaccessor.h"

#include "../dubbingconfiguration.h"

#include "log.h"

using namespace au::dubbing;
using namespace au::au3;

namespace {
std::shared_ptr<Au3WaveClip> findClipById(AudacityProject& prj, int64_t trackId, int64_t clipId)
{
    Au3WaveTrack* track = DomAccessor::findWaveTrack(prj, Au3TrackId(trackId));
    if (!track) {
        return nullptr;
    }
    return DomAccessor::findWaveClip(track, clipId);
}
}

DubbingImportService::DubbingImportService(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

WavImportResult DubbingImportService::importWav(AudacityProject& prj, DubbingMeta& meta, const muse::io::path_t& folder)
{
    WavImportResult result;

    //! 1. Рекурсивное сканирование папки, паттерн "{guid}.wav" (регистр нечувствителен).
    std::map<std::string, muse::io::path_t> wavByGuid;
    {
        const QRegularExpression rx(DUBBING_DEFAULT_WAV_NAME_PATTERN,
                                    QRegularExpression::CaseInsensitiveOption);
        QDirIterator it(folder.toQString(), { "*.wav" }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString filePath = it.next();
            const QRegularExpressionMatch m = rx.match(it.fileName());
            if (!m.hasMatch()) {
                LOGW() << "dubbing import: файл проигнорирован (не {guid}.wav): " << filePath;
                continue;
            }
            const std::string guid = m.captured(1).toUpper().toStdString();
            if (!wavByGuid.emplace(guid, muse::io::path_t(filePath)).second) {
                result.errors.push_back("дубликат WAV для guid " + guid + ": " + filePath.toStdString());
            }
        }
    }

    //! 2. Обход домена в порядке JSON; дорожка "REF <file_id>" создаётся
    //! лениво — к моменту первого реально импортируемого клипа.
    for (GameFile& file : meta.files) {
        trackedit::TrackId refTrackId = trackedit::INVALID_TRACK;
        double cursor = 0.0;

        for (Scene& scene : file.scenes) {
            for (Line& line : scene.lines) {
                //! 2а. Уже импортировано (инкрементальность): клип не дублируем,
                //! курсор двигаем за фактический конец существующего клипа.
                if (line.refClipId != NO_CLIP_ID) {
                    auto clip = findClipById(prj, line.refTrackId, line.refClipId);
                    if (clip) {
                        cursor = std::max(cursor, clip->GetPlayEndTime());
                        line.actualDur = clip->GetPlayDuration(); //!< M3: колонка/фильтр расхождений
                        result.alreadyImportedCount++;
                        continue;
                    }
                    //! клип удалён вручную — сбрасываем ссылку и импортируем заново
                    line.refTrackId = NO_TRACK_ID;
                    line.refClipId = NO_CLIP_ID;
                }

                const auto wavIt = wavByGuid.find(line.guid);
                if (wavIt == wavByGuid.cend()) {
                    //! нет референса: статус + счётчик, остальных не блокирует (§6.2)
                    if (line.status == LineStatus::New || line.status == LineStatus::NoReference) {
                        line.status = LineStatus::NoReference;
                    }
                    result.noReferenceCount++;
                    continue;
                }

                //! 2б. Сверка длительности ДО импорта (файл не меняет проект).
                const auto info = importer()->fileInfo(wavIt->second);
                if (info.isEmpty()) {
                    result.errors.push_back("не удалось прочитать WAV: " + wavIt->second.toString().toStdString());
                    continue;
                }
                line.actualDur = info.duration; //!< M3: колонка/фильтр расхождений (в домене, переживает save/load)

                const double diff = info.duration - line.dur;
                if (std::abs(diff) > DUBBING_DURATION_MISMATCH_THRESHOLD_SECS) {
                    result.mismatches.push_back({ line.guid, line.dur, info.duration, diff });
                    LOGW() << "dubbing import: расхождение длительности " << line.guid
                           << ": json=" << line.dur << "s, wav=" << info.duration << "s";
                }

                //! 2в. Дорожка "REF <file_id>" (моно) — лениво. 0 — ВАЛИДНЫЙ id!
                if (refTrackId == trackedit::INVALID_TRACK) {
                    refTrackId = tracksInteraction()->addWaveTrack(1);
                    if (refTrackId == trackedit::INVALID_TRACK) {
                        result.errors.push_back("не удалось создать дорожку REF для файла " + file.fileId);
                        break;
                    }
                    tracksInteraction()->changeTrackTitle(refTrackId,
                                                          muse::String("REF ") + muse::String::fromUtf8(file.fileId));
                }

                //! 2г. Строительный блок: импорт в дорожку на позицию cursor.
                if (!importer()->importIntoTrack(wavIt->second, refTrackId, cursor)) {
                    result.errors.push_back("ошибка импорта WAV: " + wavIt->second.toString().toStdString());
                    continue;
                }

                //! 2д. ClipKey импортированного клипа (importIntoTrack его не возвращает).
                auto clip = DomAccessor::findWaveClip(prj, refTrackId, cursor);
                if (!clip) {
                    result.errors.push_back("клип не найден после импорта: " + line.guid);
                    continue;
                }

                line.refTrackId = refTrackId;
                line.refClipId = clip->GetId();
                line.status = LineStatus::New;
                cursor = clip->GetPlayEndTime();
                result.importedCount++;
            }
        }
    }

    result.ok = result.errors.empty();
    LOGI() << "dubbing wav import: imported=" << result.importedCount
           << ", already=" << result.alreadyImportedCount
           << ", noReference=" << result.noReferenceCount
           << ", mismatches=" << result.mismatches.size()
           << ", errors=" << result.errors.size();
    return result;
}
