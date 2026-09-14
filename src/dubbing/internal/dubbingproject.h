/*
* Audacity: A Digital Audio Editor
*
* Домен дубляжа, прикреплённый к AudacityProject (AttachedProjectObjects,
* по образцу au3-cloud-audiocom sync/ProjectCloudExtension).
* Персистентность: тег <dubbing> в корневом элементе <project> документа
* .aup3/.aup4 через ProjectFileIORegistry (XMLMethodRegistry<AudacityProject>).
*/
#pragma once

#include <string_view>

#include "au3-project/Project.h"
#include "au3-registries/ClientData.h"
#include "au3-xml/XMLTagHandler.h"

#include "../dubbingtypes.h"

class XMLWriter;

namespace au::dubbing {
class DubbingProject final : public ClientData::Base, public XMLTagHandler
{
public:
    static DubbingProject& Get(AudacityProject& project);
    static const DubbingProject& Get(const AudacityProject& project);

    explicit DubbingProject(AudacityProject& project);

    const DubbingMeta& meta() const { return m_meta; }
    DubbingMeta& meta() { return m_meta; }
    void setMeta(DubbingMeta meta) { m_meta = std::move(meta); }

    //! Восстановление ссылок на аудио после загрузки проекта: TrackId/ClipId
    //! не переживают save/load (au3 переназначает id), поэтому REF-дорожки
    //! ищутся по имени «REF <file_id>», клипы — по порядку на дорожке
    //! (= порядок реплик с референсом в домене, см. DubbingImportService).
    static void reconcileReferences(AudacityProject& project);

    //! Быстрая проверка: ссылки домена указывают на существующие дорожки?
    //! false -> нужен reconcile (после загрузки .aup4 id перегенерированы).
    static bool referencesNeedReconcile(const AudacityProject& project);

    //! Сериализация в <dubbing>…</dubbing> внутри <project>
    void WriteXML(XMLWriter& xmlFile) const;

    // XMLTagHandler (чтение при загрузке проекта)
    bool HandleXMLTag(const std::string_view& tag, const AttributesList& attrs) override;
    XMLTagHandler* HandleXMLChild(const std::string_view& tag) override;

private:
    AudacityProject& mProject;
    DubbingMeta m_meta;

    // Состояние парсера при загрузке
    GameFile* m_currentFile = nullptr;
    Scene* m_currentScene = nullptr;
    Line* m_currentLine = nullptr;
};
}
