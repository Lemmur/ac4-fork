/*
* Audacity: A Digital Audio Editor
*
* Реализация контроллера рабочей зоны реплики (M3).
*/
#include "lineworkspacecontroller.h"

#include "lineslistmodel.h"

#include "au3wrap/au3types.h"
#include "au3wrap/internal/domaccessor.h"

#include "log.h"

using namespace au::dubbing;

LineworkspaceController::LineworkspaceController(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

namespace {
//! Поиск реплики в снимке домена по guid.
const Line* findLine(const DubbingMeta& meta, const std::string& guid)
{
    for (const GameFile& file : meta.files) {
        for (const Scene& scene : file.scenes) {
            for (const Line& line : scene.lines) {
                if (line.guid == guid) {
                    return &line;
                }
            }
        }
    }
    return nullptr;
}

std::shared_ptr<au::au3::Au3WaveClip> findRefClip(AudacityProject* prj, const Line& line)
{
    if (!prj || line.refTrackId == NO_TRACK_ID || line.refClipId == NO_CLIP_ID) {
        return nullptr;
    }
    au::au3::Au3WaveTrack* track = au::au3::DomAccessor::findWaveTrack(*prj, au::au3::Au3TrackId(line.refTrackId));
    if (!track) {
        return nullptr;
    }
    return au::au3::DomAccessor::findWaveClip(track, line.refClipId);
}
}

QVariantMap LineworkspaceController::lineInfo(const QString& guid) const
{
    QVariantMap info;
    if (auto dub = dubbingProject()) {
        const DubbingMeta meta = dub->domainSnapshot();
        if (const Line* line = findLine(meta, guid.toStdString())) {
            info["guid"] = guid;
            info["en"] = QString::fromStdString(line->en);
            info["ru"] = QString::fromStdString(line->ru);
            info["refStart"] = referenceStartTime(guid); //!< -1 = референса нет
            info["speaker"] = QString::fromStdString(line->speakerName);
            info["statusText"] = LinesListModel::statusText(line->status);
            info["dur"] = line->actualDur >= 0.0 ? line->actualDur : line->dur;
            info["hasReference"] = line->refClipId != NO_CLIP_ID;
        }
    }
    return info;
}

double LineworkspaceController::referenceStartTime(const QString& guid) const
{
    if (auto dub = dubbingProject()) {
        const DubbingMeta meta = dub->domainSnapshot();
        if (const Line* line = findLine(meta, guid.toStdString())) {
            auto gc = globalContext();
            auto project = gc ? gc->currentProject() : nullptr;
            AudacityProject* prj = project
                                   ? reinterpret_cast<AudacityProject*>(project->au3ProjectPtr())
                                   : nullptr;
            if (auto clip = findRefClip(prj, *line)) {
                return clip->GetPlayStartTime();
            }
        }
    }
    return -1.0;
}

bool LineworkspaceController::openLine(const QString& guid)
{
    auto dub = dubbingProject();
    auto selection = selectionController();
    auto playback = playbackController();
    if (!dub || !selection || !playback) {
        return false;
    }

    const DubbingMeta meta = dub->domainSnapshot();
    const Line* line = findLine(meta, guid.toStdString());
    if (!line || line->refTrackId == NO_TRACK_ID || line->refClipId == NO_CLIP_ID) {
        //! референса нет — позиционировать нечего (панель показывает статус)
        return false;
    }

    //! 1. Выделение референс-клипа (подсветка в представлении дорожек).
    const trackedit::ClipKey clipKey { line->refTrackId, line->refClipId };
    selection->resetSelectedClips();
    selection->setSelectedClips({ clipKey }, true);

    //! 2. Позиция воспроизведения в начало клипа: тот же путь, что у
    //! PlaybackStateModel (playbackcontroller setLastPlaybackSeekTime);
    //! при старте воспроизведения плейхед (и вид) уходит на начало реплики.
    const double start = referenceStartTime(guid);
    if (start >= 0.0) {
        playback->setLastPlaybackSeekTime(muse::secs_t(start));
    }

    return true;
}

bool LineworkspaceController::setRuText(const QString& guid, const QString& text)
{
    if (auto dub = dubbingProject()) {
        return dub->setLineRu(guid.toStdString(), text.toStdString());
    }
    return false;
}
