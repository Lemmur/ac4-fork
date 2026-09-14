/*
* Audacity: A Digital Audio Editor
*
* Инициализация тестового окружения dubbing: модуль au3wrap (создание
* AudacityProject требует Au3WrapModule::onInit, регистрация WAV-импортёров —
* onAllInited) + фейк IImporterConfiguration (tempo detection NEVER) +
* headless-BasicUI (PCM-импорт репортит прогресс; Au3BasicUI строит диалог
* с activeContext()==null в консольном тесте — ContextInject по нулевому
* контексту роняет разрешение IOC).
*/
#include "testing/environment.h"

#include <memory>

#include "au3wrap/au3wrapmodule.h"

#include "au3-basic-ui/BasicUI.h"

#include "RegisterImportPlugins.h" // au3-import-export-modules: в приложении вызывается конструктором ImporterModule

#include "importexport/import/iimporterconfiguration.h"

#include "modularity/ioc.h"

namespace {
class ImporterConfigurationFake final : public au::importexport::IImporterConfiguration
{
public:
    au::importexport::TempoDetectionPref::TempoDetection tempoDetectionPref() const override
    {
        return au::importexport::TempoDetectionPref::TempoDetection::NEVER;
    }

    void setTempoDetectionPref(au::importexport::TempoDetectionPref::TempoDetection) override {}
    muse::async::Notification tempoDetectionPrefChanged() const override { return muse::async::Notification(); }
    std::vector<std::string> tempoDetectionWorkspaces() const override { return {}; }
    void setTempoDetectionWorkspaces(const std::vector<std::string>&) override {}
    muse::async::Notification tempoDetectionWorkspacesChanged() const override { return muse::async::Notification(); }
    au::importexport::LoopAction subsequentImportLoopAction() const override
    {
        return au::importexport::LoopAction::Ask;
    }

    void setSubsequentImportLoopAction(au::importexport::LoopAction) override {}
    muse::async::Notification subsequentImportLoopActionChanged() const override { return muse::async::Notification(); }
};

//! Headless-замена BasicUI: прогресс-диалоги без UI и без IOC-разрешений,
//! остальные сервисы — no-op (по образцу Au3BasicUI).
class NullProgressDialog final : public BasicUI::ProgressDialog
{
public:
    BasicUI::ProgressResult Poll(unsigned long long, unsigned long long, const TranslatableString&) override
    {
        return BasicUI::ProgressResult::Success;
    }

    bool Cancelled() const override { return false; }
    void SetMessage(const TranslatableString&) override {}
    void SetDialogTitle(const TranslatableString&) override {}
    void Reinit() override {}
};

class NullGenericProgressDialog final : public BasicUI::GenericProgressDialog
{
public:
    BasicUI::ProgressResult Pulse() override { return BasicUI::ProgressResult::Success; }
};

class HeadlessBasicUI final : public BasicUI::Services
{
protected:
    std::unique_ptr<BasicUI::ProgressDialog> DoMakeProgress(const TranslatableString&, const TranslatableString&,
                                                            unsigned, const TranslatableString&) override
    {
        return std::make_unique<NullProgressDialog>();
    }

    std::unique_ptr<BasicUI::GenericProgressDialog> DoMakeGenericProgress(const BasicUI::WindowPlacement&,
                                                                          const TranslatableString&,
                                                                          const TranslatableString&, int) override
    {
        return std::make_unique<NullGenericProgressDialog>();
    }

    void DoCallAfter(const BasicUI::Action&) override {}
    void DoYield() override {}
    void DoShowErrorDialog(const BasicUI::WindowPlacement&, const TranslatableString&, const TranslatableString&,
                           const ManualPageID&, const BasicUI::ErrorDialogOptions&) override {}
    BasicUI::MessageBoxResult DoMessageBox(const TranslatableString&, BasicUI::MessageBoxOptions) override
    {
        return BasicUI::MessageBoxResult::None;
    }

    int DoMultiDialog(const TranslatableString&, const TranslatableString&, const TranslatableStrings&,
                      const ManualPageID&, const TranslatableString&, bool) override
    {
        return -1;
    }

    bool DoOpenInDefaultBrowser(const wxString&) override { return false; }
    std::unique_ptr<BasicUI::WindowPlacement> DoFindFocus() override { return nullptr; }
    void DoSetFocus(const BasicUI::WindowPlacement&) override {}
    bool IsUsingRtlLayout() const override { return false; }
    bool IsUiThread() const override { return false; }
};

HeadlessBasicUI s_headlessBasicUI;
}

static muse::testing::SuiteEnvironment dubbing_se
    = muse::testing::SuiteEnvironment()
      .setDependencyModules({ new au::au3::Au3WrapModule(), })
      .setPreInit([]() {
    //! ДО onAllInited: Importer::Initialize() снимает снапшот реестра плагинов
    //! через std::call_once (Import.cpp:182-190), регистрировать позже бессмысленно.
    RegisterImportPlugins();
}).setPostInit([]() {
    //! после Au3WrapModule::onInit (устанавливает Au3BasicUI) подменяем на headless
    BasicUI::Install(&s_headlessBasicUI);

    auto config = std::make_shared<ImporterConfigurationFake>();
    muse::modularity::globalIoc()->registerExport<au::importexport::IImporterConfiguration>("utests", config);
}).setDeInit([]() {
    BasicUI::Install(nullptr);
    muse::modularity::globalIoc()->unregister<au::importexport::IImporterConfiguration>("utests");
});
