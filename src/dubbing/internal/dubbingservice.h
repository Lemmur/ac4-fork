/*
* Audacity: A Digital Audio Editor
*
* Реализация IDubbingProject (M2): фасад над доменом DubbingProject текущего
* проекта + читатель JSON + массовый импорт WAV. Отмена — только штатный
* IProjectHistory: ОДИН pushHistoryState на пакет (CONSOLIDATE), метаданные
* снимает DubbingStateExtension (механизм M1).
*/
#pragma once

#include "modularity/ioc.h"

#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "au3-project/Project.h"

#include "../idubbingproject.h"
#include "../import/dubbingimportservice.h"
#include "../import/dubbingjsonreader.h"

namespace au::dubbing {
class DubbingService final : public IDubbingProject, public muse::Contextable
{
public:
    explicit DubbingService(const muse::modularity::ContextPtr& ctx);

    muse::ContextInject<au::context::IGlobalContext> globalContext { this };
    muse::ContextInject<au::trackedit::IProjectHistory> projectHistory { this };

    JsonImportResult importFromJson(const muse::io::path_t& path) override;
    WavImportResult importWavFolder(const muse::io::path_t& folder) override;
    bool setLineRu(const std::string& guid, const std::string& text) override;

    muse::async::Notification domainChanged() const override { return m_domainChanged; }

    //! Доступ к вложенному сервису импорта (подстановка зависимостей в тестах).
    DubbingImportService& importService() { return m_importService; }

private:
    AudacityProject* currentAu3Project() const;

    DubbingImportService m_importService;
    DubbingJsonReader m_jsonReader;
    muse::async::Notification m_domainChanged;
};
}
