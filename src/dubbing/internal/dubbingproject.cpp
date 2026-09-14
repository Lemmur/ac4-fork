/*
* Audacity: A Digital Audio Editor
*
* Реализация домена дубляжа + регистрация в ProjectFileIORegistry
* (штатная точка расширения документа проекта, вызывается из
* ProjectFileIO::WriteXML при каждом сохранении и автосейве).
*/
#include "dubbingproject.h"

#include <wx/string.h>

#include <algorithm>

#include "au3-xml/XMLWriter.h"
#include "au3-track/Track.h"
#include "au3-wave-track/WaveTrack.h"
#include "au3wrap/internal/domaccessor.h"

using namespace au::dubbing;

namespace {
//! Фабрика прикреплённого объекта (по образцу ProjectCloudExtension)
const AudacityProject::AttachedObjects::RegisteredFactory dubbingKey {
    [](AudacityProject& project) {
        return std::make_shared<DubbingProject>(project);
    }
};

std::string toStd(const wxString& s)
{
    return std::string(s.utf8_str());
}

wxString toWx(const std::string& s)
{
    return wxString::FromUTF8(s.c_str(), s.size());
}
}

DubbingProject& DubbingProject::Get(AudacityProject& project)
{
    return project.AttachedObjects::Get<DubbingProject&>(dubbingKey);
}

const DubbingProject& DubbingProject::Get(const AudacityProject& project)
{
    return Get(const_cast<AudacityProject&>(project));
}

DubbingProject::DubbingProject(AudacityProject& project)
    : mProject{project}
{
}

void DubbingProject::WriteXML(XMLWriter& xmlFile) const
{
    xmlFile.StartTag(wxT("dubbing"));
    xmlFile.WriteAttr(wxT("version"), m_meta.schemaVersion);

    for (const auto& file : m_meta.files) {
        xmlFile.StartTag(wxT("file"));
        xmlFile.WriteAttr(wxT("id"), toWx(file.fileId));

        for (const auto& scene : file.scenes) {
            xmlFile.StartTag(wxT("scene"));
            xmlFile.WriteAttr(wxT("id"), toWx(scene.questId));

            for (const auto& line : scene.lines) {
                xmlFile.StartTag(wxT("line"));
                xmlFile.WriteAttr(wxT("guid"), toWx(line.guid));
                xmlFile.WriteAttr(wxT("en"), toWx(line.en));
                xmlFile.WriteAttr(wxT("ru"), toWx(line.ru));
                xmlFile.WriteAttr(wxT("speaker"), toWx(line.speakerName));
                xmlFile.WriteAttr(wxT("speaker_internal"), toWx(line.speakerInternal));
                xmlFile.WriteAttr(wxT("dur"), line.dur, 6);
                if (line.actualDur >= 0.0) {
                    //! фактическая длительность WAV (M3: колонка/фильтр расхождений);
                    //! атрибут опционален при чтении — старые проекты читаются без него
                    xmlFile.WriteAttr(wxT("actual_dur"), line.actualDur, 6);
                }
                xmlFile.WriteAttr(wxT("order"), line.orderIndex);
                xmlFile.WriteAttr(wxT("status"), static_cast<int>(line.status));
                xmlFile.WriteAttr(wxT("reftrack"), line.refTrackId);
                xmlFile.WriteAttr(wxT("refclip"), line.refClipId);
                xmlFile.WriteAttr(wxT("mastertrack"), line.masterTrackId);
                xmlFile.WriteAttr(wxT("masterclip"), line.masterClipId);

                for (const auto& take : line.takes) {
                    xmlFile.StartTag(wxT("take"));
                    xmlFile.WriteAttr(wxT("track"), take.trackId);
                    xmlFile.WriteAttr(wxT("clip"), take.clipId);
                    xmlFile.WriteAttr(wxT("best"), take.markedBest);
                    xmlFile.EndTag(wxT("take"));
                }

                xmlFile.EndTag(wxT("line"));
            }

            xmlFile.EndTag(wxT("scene"));
        }

        xmlFile.EndTag(wxT("file"));
    }

    xmlFile.EndTag(wxT("dubbing"));
}

bool DubbingProject::HandleXMLTag(const std::string_view& tag, const AttributesList& attrs)
{
    if (tag == "dubbing") {
        m_meta = DubbingMeta{};
        m_meta.isDubbing = true;
        m_currentFile = nullptr;
        m_currentScene = nullptr;
        m_currentLine = nullptr;
        for (const auto& [name, value] : attrs) {
            if (name == "version") {
                long v = 0;
                value.ToWString().ToLong(&v);
                m_meta.schemaVersion = static_cast<int>(v);
            }
        }
        return true;
    }

    if (tag == "file") {
        m_meta.files.emplace_back();
        m_currentFile = &m_meta.files.back();
        m_currentScene = nullptr;
        m_currentLine = nullptr;
        for (const auto& [name, value] : attrs) {
            if (name == "id") {
                m_currentFile->fileId = toStd(value.ToWString());
            }
        }
        return true;
    }

    if (tag == "scene") {
        if (!m_currentFile) {
            return false;
        }
        m_currentFile->scenes.emplace_back();
        m_currentScene = &m_currentFile->scenes.back();
        m_currentLine = nullptr;
        for (const auto& [name, value] : attrs) {
            if (name == "id") {
                m_currentScene->questId = toStd(value.ToWString());
            }
        }
        return true;
    }

    if (tag == "line") {
        if (!m_currentScene) {
            return false;
        }
        m_currentScene->lines.emplace_back();
        m_currentLine = &m_currentScene->lines.back();
        for (const auto& [name, value] : attrs) {
            const wxString v = value.ToWString();
            if (name == "guid") {
                m_currentLine->guid = toStd(v);
            } else if (name == "en") {
                m_currentLine->en = toStd(v);
            } else if (name == "ru") {
                m_currentLine->ru = toStd(v);
            } else if (name == "speaker") {
                m_currentLine->speakerName = toStd(v);
            } else if (name == "speaker_internal") {
                m_currentLine->speakerInternal = toStd(v);
            } else if (name == "dur") {
                double d = 0.0;
                v.ToDouble(&d);
                m_currentLine->dur = d;
            } else if (name == "actual_dur") {
                double d = 0.0;
                v.ToDouble(&d);
                m_currentLine->actualDur = d;
            } else if (name == "order") {
                long n = 0;
                v.ToLong(&n);
                m_currentLine->orderIndex = static_cast<int>(n);
            } else if (name == "status") {
                long n = 0;
                v.ToLong(&n);
                m_currentLine->status = static_cast<LineStatus>(n);
            } else if (name == "reftrack") {
                long long n = 0;
                v.ToLongLong(&n);
                m_currentLine->refTrackId = n;
            } else if (name == "refclip") {
                long long n = 0;
                v.ToLongLong(&n);
                m_currentLine->refClipId = n;
            } else if (name == "mastertrack") {
                long long n = 0;
                v.ToLongLong(&n);
                m_currentLine->masterTrackId = n;
            } else if (name == "masterclip") {
                long long n = 0;
                v.ToLongLong(&n);
                m_currentLine->masterClipId = n;
            }
        }
        return true;
    }

    if (tag == "take") {
        if (!m_currentLine) {
            return false;
        }
        m_currentLine->takes.emplace_back();
        TakeInfo& take = m_currentLine->takes.back();
        for (const auto& [name, value] : attrs) {
            const wxString v = value.ToWString();
            if (name == "track") {
                long long n = 0;
                v.ToLongLong(&n);
                take.trackId = n;
            } else if (name == "clip") {
                long long n = 0;
                v.ToLongLong(&n);
                take.clipId = n;
            } else if (name == "best") {
                take.markedBest = (v == wxT("1") || v.Lower() == wxT("true"));
            }
        }
        return true;
    }

    return false;
}

XMLTagHandler* DubbingProject::HandleXMLChild(const std::string_view&)
{
    // Все вложенные теги (<file>, <scene>, <line>, <take>) разбираем сами
    return this;
}

void DubbingProject::reconcileReferences(AudacityProject& project)
{
    DubbingMeta& meta = DubbingProject::Get(project).meta();
    if (!meta.isDubbing) {
        return;
    }

    auto& trackList = Au3TrackList::Get(project);

    for (GameFile& file : meta.files) {
        //! REF-дорожка файла — по сохранённому имени «REF <file_id>»
        Au3WaveTrack* refTrack = nullptr;
        const wxString wantedName = toWx("REF " + file.fileId);
        for (auto track : trackList) {
            if (track && track->GetName() == wantedName) {
                refTrack = dynamic_cast<Au3WaveTrack*>(track);
                break;
            }
        }

        //! Нет REF-дорожки (переименована/удалена/тестовый домен без
        //! дорожек) — файл не трогаем: это не «загрузка с новыми id»,
        //! а осознанное состояние проекта.
        if (!refTrack) {
            continue;
        }

        //! Клипы дорожки в порядке времени (порядок реплик с референсом)
        std::vector<std::shared_ptr<Au3WaveClip> > clips;
        auto clipList = au::au3::DomAccessor::waveClipsAsList(refTrack);
        clips.assign(clipList.begin(), clipList.end());
        std::sort(clips.begin(), clips.end(),
                  [](const auto& a, const auto& b) { return a->GetPlayStartTime() < b->GetPlayStartTime(); });

        size_t clipIdx = 0;
        for (Scene& scene : file.scenes) {
            for (Line& line : scene.lines) {
                if (line.refClipId == NO_CLIP_ID) {
                    continue; //!< реплика без референса
                }
                if (clipIdx < clips.size()) {
                    line.refTrackId = refTrack->GetId();
                    line.refClipId = clips[clipIdx]->GetId();
                    ++clipIdx;
                } else {
                    line.refTrackId = NO_TRACK_ID;
                    line.refClipId = NO_CLIP_ID;
                }
            }
        }
    }
}

bool DubbingProject::referencesNeedReconcile(const AudacityProject& project)
{
    const DubbingMeta& meta = DubbingProject::Get(project).meta();
    const auto& trackList = Au3TrackList::Get(project);

    for (const GameFile& file : meta.files) {
        for (const Scene& scene : file.scenes) {
            for (const Line& line : scene.lines) {
                if (line.refClipId == NO_CLIP_ID) {
                    continue;
                }
                bool found = false;
                for (const auto& track : trackList) {
                    if (track && TrackId(track->GetId()) == line.refTrackId) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return true; //!< ссылка в никуда — нужен reconcile
                }
            }
        }
    }
    return false;
}

// ============================================================
// Регистрация в реестре документа проекта:
// вызывается из ProjectFileIO::WriteXML (ProjectFileIO.cpp, CallWriters)
// при каждом Save/SaveCopy/AutoSave; чтение — при LoadProject.
// ============================================================
namespace {
const ProjectFileIORegistry::ObjectWriterEntry dubbingWriterEntry {
    [](const AudacityProject& project, XMLWriter& xmlFile) {
        const DubbingProject& dubbing = DubbingProject::Get(project);
        if (!dubbing.meta().isDubbing) {
            return; // обычный проект — тег не пишем
        }
        dubbing.WriteXML(xmlFile);
    }
};

const ProjectFileIORegistry::ObjectReaderEntry dubbingReaderEntry {
    "dubbing",
    [](AudacityProject& project) -> XMLTagHandler* {
        return &DubbingProject::Get(project);
    }
};
}
