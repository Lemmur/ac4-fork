/*
* Audacity: A Digital Audio Editor
*
* Массовый импорт WAV-референсов дубляжа (§6.2, M2).
* Строительные блоки (дословно, без дублирования):
*  - IImporter::importIntoTrack (src/importexport/import/internal/au3/au3importer.h:35)
*    — импорт файла аудио в дорожку с позиции startTime;
*  - ITracksInteraction::addWaveTrack / changeTrackTitle — создание моно-дорожки
*    "REF <file_id>" и именование (как в Au3Importer::importLegacyAup);
*  - DomAccessor::findWaveClip(prj, trackId, time) — ClipKey импортированного клипа.
* Историю (pushHistoryState) сервис НЕ вызывает: один пуш на пакет делает
* вызывающий (DubbingService), по образцу Audacity4Project::importIntoTracks.
*/
#pragma once

#include "modularity/ioc.h"

#include "context/iglobalcontext.h"
#include "importexport/import/iimporter.h"
#include "trackedit/itracksinteraction.h"

#include "au3-project/Project.h"

#include "../dubbingtypes.h"
#include "../idubbingproject.h"

namespace au::dubbing {
class DubbingImportService : public muse::Contextable
{
public:
    explicit DubbingImportService(const muse::modularity::ContextPtr& ctx);

    muse::ContextInject<au::context::IGlobalContext> globalContext { this };
    muse::ContextInject<au::importexport::IImporter> importer { this };
    muse::ContextInject<au::trackedit::ITracksInteraction> tracksInteraction { this };

    //! Импортирует референсы из папки (рекурсивно, "{guid}.wav") в домен meta:
    //! создаёт дорожки "REF <file_id>", заполняет refTrackId/refClipId/статусы.
    //! Инкрементальность: guid с уже установленным refClipId не импортируется
    //! повторно (клип не дублируется).
    WavImportResult importWav(AudacityProject& prj, DubbingMeta& meta, const muse::io::path_t& folder);
};
}
