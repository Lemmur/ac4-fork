/*
* Audacity: A Digital Audio Editor
*
* M3 roadmap, критерий готовности (roadmap §M3): панель с 30 000 реплик —
* замер производительности (построение модели, проход фильтра, поиск,
* выборка видимой страницы строк); фильтры UNKNOWN/статус/расхождение/
* нет референса; полнотекстовый поиск; двойной клик выделяет референсный
* клип и ставит позицию воспроизведения; правка RU-текста из панели
* отменяется штатным undo (pushHistoryState «Правка текста реплики»).
*
* Проводка зависимостей — как в dubbingimport_tests.cpp (M2): реальные
* Au3ProjectAccessor / Au3TracksInteraction / Au3ProjectHistory /
* DubbingService, Inject-поля модели/контроллера панели — через .set().
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "modularity/ioc.h"

// моки
#include "context/tests/mocks/globalcontextmock.h"
#include "context/tests/mocks/playbackstatemock.h"
#include "project/tests/mocks/audacityprojectmock.h"
#include "trackedit/tests/mocks/clipsinteractionmock.h"
#include "trackedit/tests/mocks/selectioncontrollermock.h"
#include "trackedit/tests/mocks/trackeditconfigurationmock.h"
#include "trackedit/tests/mocks/trackeditprojectmock.h"
#include "trackedit/tests/mocks/tracknavigationcontrollermock.h"
#include "interactive/tests/mocks/interactivemock.h"
#include "playback/tests/mocks/playbackcontrollermock.h"

// реальные внутренние классы
#include "trackedit/internal/au3/au3tracksinteraction.h"
#include "trackedit/internal/au3/au3projecthistory.h"
#include "importexport/import/internal/au3/au3importer.h"

#include "au3wrap/au3types.h"
#include "au3wrap/internal/au3project.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"

#include "au3-project-file-io/ProjectFileIO.h"
#include "au3-project-history/ProjectHistory.h"
#include "au3-project-history/UndoManager.h"
#include "au3-track/Track.h"

#include "../internal/dubbingproject.h"
#include "../internal/dubbingservice.h"
#include "../panel/lineslistmodel.h"
#include "../panel/linesfiltermodel.h"
#include "../panel/lineworkspacecontroller.h"

using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::Return;

using namespace au;
using namespace au::au3;

namespace au::dubbing {
namespace {
const std::string SAMPLE_JSON = std::string(dubbing_tests_DATA_ROOT) + "/data/sample.json";

const char* GUID_FIRST = "6046256F4DF7E505F0906FBE58C09951";   //!< dur 1.861
const char* GUID_SECOND = "1C89F59B48693450902DBC8F43F87202";  //!< dur 1.075
const char* GUID_MISMATCH = "2A0F59574157D9EAD561A992313C42EE"; //!< dur 1.732, WAV 2.2 c -> расхождение
const char* GUID_NO_WAV = "41E304C446621137356856820F7BD252";  //!< WAV не даём

//! Синтетический WAV: RIFF/WAVE, PCM 16 бит, моно 44100, тишина.
void writeSilenceWav(const std::filesystem::path& path, double seconds)
{
    const uint32_t rate = 44100;
    const uint32_t samples = static_cast<uint32_t>(std::llround(seconds * static_cast<double>(rate)));
    const uint32_t dataBytes = samples * 2;
    const uint32_t byteRate = rate * 2;

    std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(f.is_open());

    auto wr32 = [&f](uint32_t v) {
        char b[4] = { static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF),
                      static_cast<char>((v >> 16) & 0xFF), static_cast<char>((v >> 24) & 0xFF) };
        f.write(b, 4);
    };
    auto wr16 = [&f](uint16_t v) {
        char b[2] = { static_cast<char>(v & 0xFF), static_cast<char>((v >> 8) & 0xFF) };
        f.write(b, 2);
    };

    f.write("RIFF", 4);
    wr32(36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    wr32(16);
    wr16(1);          // PCM
    wr16(1);          // моно
    wr32(rate);
    wr32(byteRate);
    wr16(2);          // block align
    wr16(16);         // биты на сэмпл
    f.write("data", 4);
    wr32(dataBytes);
    const std::vector<char> silence(dataBytes, 0);
    f.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    f.close();
}

Line* findLine(DubbingMeta& meta, const std::string& guid)
{
    for (GameFile& file : meta.files) {
        for (Scene& scene : file.scenes) {
            for (Line& line : scene.lines) {
                if (line.guid == guid) {
                    return &line;
                }
            }
        }
    }
    return nullptr;
}

//! Небольшой домен для функциональных проверок фильтров/поиска.
DubbingMeta makeSmallMeta()
{
    DubbingMeta meta;
    meta.isDubbing = true;

    GameFile file;
    file.fileId = "q000_intro";

    Scene scene1;
    scene1.questId = "scene_one";
    Line l1;
    l1.guid = "A0000000000000000000000000000001";
    l1.en = "Hello Coen";
    l1.ru = "Привет, Коэн";
    l1.speakerName = "Lunka";
    l1.speakerInternal = "Character.Secondary.Lunka";
    l1.dur = 1.0;
    l1.actualDur = 1.02;  //!< без расхождения
    l1.refTrackId = 0;
    l1.refClipId = 100;   //!< референс есть
    l1.orderIndex = 0;
    l1.status = LineStatus::New;

    Line l2;
    l2.guid = "A0000000000000000000000000000002";
    l2.en = "Unknown speaker line";
    l2.ru = "Реплика без спикера";
    l2.speakerName = "UNKNOWN";
    l2.speakerInternal = "";
    l2.dur = 2.0;
    l2.actualDur = 2.5;   //!< расхождение +0.5 c
    l2.refTrackId = 0;
    l2.refClipId = 101;
    l2.orderIndex = 1;
    l2.status = LineStatus::InProgress;

    Line l3;
    l3.guid = "A0000000000000000000000000000003";
    l3.en = "No reference here";
    l3.ru = "Референса нет";
    l3.speakerName = "Coen";
    l3.speakerInternal = "Character.Main.Coen";
    l3.dur = 3.0;
    l3.actualDur = -1.0;  //!< фактическая неизвестна
    l3.orderIndex = 2;
    l3.status = LineStatus::NoReference; //!< refClipId = NO_CLIP_ID

    scene1.lines = { l1, l2, l3 };

    Scene scene2;
    scene2.questId = "scene_two";
    Line l4;
    l4.guid = "A0000000000000000000000000000004";
    l4.en = "Done line";
    l4.ru = "Готовая реплика";
    l4.speakerName = "UNKNOWN";
    l4.speakerInternal = "";
    l4.dur = 1.5;
    l4.actualDur = 1.5;
    l4.refTrackId = 0;
    l4.refClipId = 103;
    l4.orderIndex = 3;
    l4.status = LineStatus::Ready;

    scene2.lines = { l4 };

    file.scenes = { scene1, scene2 };
    meta.files = { file };
    return meta;
}

//! Домен для нагрузочного критерия §M3: ОДИН файл игры с 30 000 реплик
//! (Select файла ограничивает список одним файлом — грузим его целиком;
//! 600 сцен x 50 реплик, спикеры/статусы/расхождения вразброс).
DubbingMeta make30kMeta()
{
    DubbingMeta meta;
    meta.isDubbing = true;

    int counter = 0;
    GameFile file;
    file.fileId = "file_big";
    file.scenes.reserve(600);
    for (int s = 0; s < 600; ++s) {
        Scene scene;
        scene.questId = "quest_" + std::to_string(s);
        scene.lines.reserve(50);
        for (int l = 0; l < 50; ++l) {
            Line line;
            char guid[33];
            snprintf(guid, sizeof(guid), "%032X", counter);
            line.guid = guid;
            line.en = "English line number " + std::to_string(counter);
            line.ru = "Русская реплика номер " + std::to_string(counter);
            line.speakerName = (counter % 5 == 0) ? "UNKNOWN" : ("Speaker " + std::to_string(counter % 7));
            line.speakerInternal = "Character.Test." + std::to_string(counter % 7);
            line.dur = 0.5 + (counter % 40) * 0.1;
            line.actualDur = (counter % 11 == 0) ? -1.0 : line.dur;
            if (counter % 7 == 0) {
                line.actualDur = line.dur + 0.5; //!< расхождение > порога
            }
            line.refTrackId = (counter % 11 == 0) ? NO_TRACK_ID : 0;
            line.refClipId = (counter % 11 == 0) ? NO_CLIP_ID : counter;
            line.orderIndex = counter;
            line.status = static_cast<LineStatus>(counter % 5);
            scene.lines.push_back(std::move(line));
            ++counter;
        }
        file.scenes.push_back(std::move(scene));
    }
    meta.files.push_back(std::move(file));
    assert(counter == 30000);
    return meta;
}

//! Домен многих файлов: 50 файлов x 60 сцен x 10 реплик (тест Select'а).
DubbingMeta makeManyFilesMeta()
{
    DubbingMeta meta;
    meta.isDubbing = true;

    int counter = 0;
    meta.files.reserve(50);
    for (int f = 0; f < 50; ++f) {
        GameFile file;
        file.fileId = "file_" + std::to_string(f);
        file.scenes.reserve(60);
        for (int s = 0; s < 60; ++s) {
            Scene scene;
            scene.questId = "quest_" + std::to_string(f) + "_" + std::to_string(s);
            scene.lines.reserve(10);
            for (int l = 0; l < 10; ++l) {
                Line line;
                char guid[33];
                snprintf(guid, sizeof(guid), "%032X", counter);
                line.guid = guid;
                line.en = "English line " + std::to_string(counter);
                line.ru = "Русская реплика " + std::to_string(counter);
                line.speakerName = (counter % 5 == 0) ? "UNKNOWN" : "Speaker";
                line.dur = 1.0;
                line.orderIndex = counter;
                scene.lines.push_back(std::move(line));
                ++counter;
            }
            file.scenes.push_back(std::move(scene));
        }
        meta.files.push_back(std::move(file));
    }
    return meta;
}

double msSince(const std::chrono::steady_clock::time_point& t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

constexpr muse::modularity::IoCID TEST_CTX_ID = 1;

muse::modularity::ContextPtr makeTestCtx()
{
    return std::make_shared<muse::modularity::Context>(TEST_CTX_ID);
}

template<typename I>
void registerCtxIoc(const muse::modularity::ContextPtr& ctx, const std::shared_ptr<I>& impl)
{
    auto ctxIoc = muse::modularity::ioc(ctx);
    ctxIoc->unregister<I>("utests");
    ctxIoc->registerExport<I>("utests", impl);
}

template<typename I>
void unregisterCtxIoc(const muse::modularity::ContextPtr& ctx)
{
    muse::modularity::ioc(ctx)->unregister<I>("utests");
}

template<typename I>
void registerGlobalIoc(const std::shared_ptr<I>& impl)
{
    muse::modularity::globalIoc()->unregister<I>("utests");
    muse::modularity::globalIoc()->registerExport<I>("utests", impl);
}

template<typename I>
void unregisterGlobalIoc()
{
    muse::modularity::globalIoc()->unregister<I>("utests");
}
}

class DubbingPanelTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_testCtx = makeTestCtx();

        m_accessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
        ASSERT_TRUE(m_accessor->open().valid());

        m_globalContext = std::make_shared<NiceMock<context::GlobalContextMock> >();
        m_currentProject = std::make_shared<NiceMock<project::AudacityProjectMock> >();
        m_trackEditProject = std::make_shared<NiceMock<au::trackedit::TrackeditProjectMock> >();
        m_playbackState = std::make_shared<NiceMock<context::PlaybackStateMock> >();
        m_selectionController = std::make_shared<NiceMock<au::trackedit::SelectionControllerMock> >();
        m_trackNavigationController = std::make_shared<NiceMock<au::trackedit::TrackNavigationControllerMock> >();
        m_interactive = std::make_shared<NiceMock<muse::InteractiveMock> >();
        m_trackeditConfiguration = std::make_shared<NiceMock<au::trackedit::TrackeditConfigurationMock> >();
        m_clipsInteraction = std::make_shared<NiceMock<au::trackedit::ClipsInteractionMock> >();
        m_playbackController = std::make_shared<NiceMock<au::playback::PlaybackControllerMock> >();

        ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(m_currentProject));
        ON_CALL(*m_globalContext, currentTrackeditProject()).WillByDefault(Return(m_trackEditProject));
        ON_CALL(*m_currentProject, au3ProjectPtr()).WillByDefault(Return(m_accessor->au3ProjectPtr()));
        ON_CALL(*m_currentProject, trackeditProject()).WillByDefault(Return(m_trackEditProject));

        //! выделение дорожек должно быть СОСТОЯТЕЛЬНЫМ (как в M2)
        ON_CALL(*m_selectionController, setSelectedTracks).WillByDefault(Invoke([this](
            const au::trackedit::TrackIdList& tracks, bool) {
            m_selectedTracks = tracks;
        }));
        ON_CALL(*m_selectionController, selectedTracks).WillByDefault(Invoke([this]() {
            return m_selectedTracks;
        }));
        ON_CALL(*m_selectionController, resetSelectedTracks).WillByDefault(Invoke([this]() {
            m_selectedTracks.clear();
        }));

        //! trackList отдаёт реальные дорожки проекта (как в M2 / au3interactiontestbase.h):
        //! без этого addWaveTrack/paste работают с пустым списком и падают
        ON_CALL(*m_trackEditProject, trackList()).WillByDefault(Invoke([this]() {
            std::vector<au::trackedit::Track> result;
            for (const Au3Track* t : Au3TrackList::Get(projectRef())) {
                result.push_back(DomConverter::track(t));
            }
            return result;
        }));

        m_tracksInteraction = std::make_shared<au::trackedit::Au3TracksInteraction>(m_testCtx);
        m_history = std::make_shared<au::trackedit::Au3ProjectHistory>(m_testCtx);
        m_importer = std::make_shared<au::importexport::Au3Importer>(m_testCtx);

        registerCtxIoc<au::context::IGlobalContext>(m_testCtx, m_globalContext);
        registerCtxIoc<au::trackedit::ISelectionController>(m_testCtx, m_selectionController);
        registerCtxIoc<au::trackedit::ITrackNavigationController>(m_testCtx, m_trackNavigationController);
        registerCtxIoc<muse::IInteractive>(m_testCtx, m_interactive);
        registerGlobalIoc<au::trackedit::ITrackeditConfiguration>(m_trackeditConfiguration);
        registerCtxIoc<au::trackedit::IClipsInteraction>(m_testCtx, m_clipsInteraction);
        registerCtxIoc<au::trackedit::ITracksInteraction>(m_testCtx, m_tracksInteraction);
        registerCtxIoc<au::trackedit::IProjectHistory>(m_testCtx, m_history);
        registerCtxIoc<au::importexport::IImporter>(m_testCtx, m_importer);

        m_service = std::make_shared<DubbingService>(m_testCtx);
        m_service->globalContext.set(m_globalContext);
        m_service->projectHistory.set(m_history);
        m_service->importService().globalContext.set(m_globalContext);
        m_service->importService().importer.set(m_importer);
        m_service->importService().tracksInteraction.set(m_tracksInteraction);

        m_history->init(); // ProjectHistory::InitialState
    }

    void TearDown() override
    {
        unregisterCtxIoc<au::context::IGlobalContext>(m_testCtx);
        unregisterCtxIoc<au::trackedit::ISelectionController>(m_testCtx);
        unregisterCtxIoc<au::trackedit::ITrackNavigationController>(m_testCtx);
        unregisterCtxIoc<muse::IInteractive>(m_testCtx);
        unregisterGlobalIoc<au::trackedit::ITrackeditConfiguration>();
        unregisterCtxIoc<au::trackedit::IClipsInteraction>(m_testCtx);
        unregisterCtxIoc<au::trackedit::ITracksInteraction>(m_testCtx);
        unregisterCtxIoc<au::trackedit::IProjectHistory>(m_testCtx);
        unregisterCtxIoc<au::importexport::IImporter>(m_testCtx);
        muse::modularity::removeIoC(m_testCtx);

        if (m_accessor) {
            Au3TrackList::Get(projectRef()).Clear();
            m_accessor->close();
        }
    }

    AudacityProject& projectRef() const
    {
        return *reinterpret_cast<AudacityProject*>(m_accessor->au3ProjectPtr());
    }

    DubbingMeta& meta()
    {
        return DubbingProject::Get(projectRef()).meta();
    }

    std::filesystem::path wavDir()
    {
        return std::filesystem::temp_directory_path() / "dubbing_m3_wav";
    }

    //! Модель панели с подставленным сервисом (Inject::set).
    std::unique_ptr<LinesListModel> makeModel()
    {
        auto model = std::make_unique<LinesListModel>();
        model->dubbingProject.set(m_service);
        model->globalContext.set(m_globalContext);
        model->projectHistory.set(m_history);
        return model;
    }

    std::unique_ptr<LineworkspaceController> makeController()
    {
        auto controller = std::make_unique<LineworkspaceController>();
        controller->dubbingProject.set(m_service);
        controller->globalContext.set(m_globalContext);
        controller->selectionController.set(m_selectionController);
        controller->trackNavigationController.set(m_trackNavigationController);
        controller->playbackController.set(m_playbackController);
        return controller;
    }

    muse::modularity::ContextPtr m_testCtx;
    std::shared_ptr<Au3ProjectAccessor> m_accessor;
    std::shared_ptr<NiceMock<context::GlobalContextMock> > m_globalContext;
    std::shared_ptr<NiceMock<project::AudacityProjectMock> > m_currentProject;
    std::shared_ptr<NiceMock<au::trackedit::TrackeditProjectMock> > m_trackEditProject;
    std::shared_ptr<NiceMock<context::PlaybackStateMock> > m_playbackState;
    std::shared_ptr<NiceMock<au::trackedit::SelectionControllerMock> > m_selectionController;
    std::shared_ptr<NiceMock<au::trackedit::TrackNavigationControllerMock> > m_trackNavigationController;
    std::shared_ptr<NiceMock<muse::InteractiveMock> > m_interactive;
    std::shared_ptr<NiceMock<au::trackedit::TrackeditConfigurationMock> > m_trackeditConfiguration;
    std::shared_ptr<NiceMock<au::trackedit::ClipsInteractionMock> > m_clipsInteraction;
    std::shared_ptr<NiceMock<au::playback::PlaybackControllerMock> > m_playbackController;
    std::shared_ptr<au::trackedit::Au3TracksInteraction> m_tracksInteraction;
    std::shared_ptr<au::trackedit::Au3ProjectHistory> m_history;
    std::shared_ptr<au::importexport::Au3Importer> m_importer;
    std::shared_ptr<DubbingService> m_service;
    au::trackedit::TrackIdList m_selectedTracks;
};

//! Модель строится из снимка домена: раскрывающиеся заголовки сцен
//! (первая раскрыта, остальные свёрнуты), реплики в порядке домена,
//! роли (статус/спикер/EN/RU/длительности/расхождение) заполняются верно.
TEST_F(DubbingPanelTests, ModelBuild_RolesAndOrder)
{
    meta() = makeSmallMeta();

    auto model = makeModel();
    model->reload();

    const auto row = [&model](int r, LinesListModel::Roles role) {
        return model->data(model->index(r, 0), role);
    };

    //! Файл (первый уровень) выбирается автоматически — единственный
    EXPECT_EQ(model->fileIds().size(), 1u);
    EXPECT_EQ(model->fileId().toStdString(), "q000_intro");

    //! Раскладка: [H scene_one][3 реплики][H scene_two(свёрнута)]
    ASSERT_EQ(model->rowCount(), 5);
    EXPECT_EQ(row(0, LinesListModel::RowTypeRole).toInt(),
              static_cast<int>(LinesListModel::SceneHeaderRow));
    EXPECT_EQ(row(0, LinesListModel::SectionTitleRole).toString().toStdString(), "scene_one");
    EXPECT_EQ(row(0, LinesListModel::SectionLineCountRole).toInt(), 3);
    EXPECT_TRUE(row(0, LinesListModel::ExpandedRole).toBool());

    EXPECT_EQ(row(1, LinesListModel::GuidRole).toString().toStdString(),
              "A0000000000000000000000000000001");
    EXPECT_EQ(row(1, LinesListModel::StatusTextRole).toString().toStdString(), "Новая");
    EXPECT_EQ(row(2, LinesListModel::StatusTextRole).toString().toStdString(), "В работе");
    EXPECT_EQ(row(3, LinesListModel::StatusTextRole).toString().toStdString(), "Нет референса");

    EXPECT_EQ(row(4, LinesListModel::RowTypeRole).toInt(),
              static_cast<int>(LinesListModel::SceneHeaderRow));
    EXPECT_EQ(row(4, LinesListModel::SectionTitleRole).toString().toStdString(), "scene_two");
    EXPECT_FALSE(row(4, LinesListModel::ExpandedRole).toBool());

    EXPECT_EQ(row(1, LinesListModel::SpeakerRole).toString().toStdString(), "Lunka");
    EXPECT_EQ(row(2, LinesListModel::SpeakerRole).toString().toStdString(), "UNKNOWN");

    EXPECT_EQ(row(1, LinesListModel::EnRole).toString().toStdString(), "Hello Coen");
    EXPECT_EQ(row(1, LinesListModel::RuRole).toString().toStdString(), "Привет, Коэн");

    //! длительность: фактическая, если известна, иначе из JSON
    EXPECT_NEAR(row(1, LinesListModel::DurRole).toDouble(), 1.02, 1e-9);
    EXPECT_NEAR(row(3, LinesListModel::DurRole).toDouble(), 3.0, 1e-9);

    //! расхождение: 0.5 у второй реплики, неизвестно -> 0/нет
    EXPECT_NEAR(row(2, LinesListModel::MismatchRole).toDouble(), 0.5, 1e-9);
    EXPECT_FALSE(row(1, LinesListModel::HasMismatchRole).toBool());
    EXPECT_TRUE(row(2, LinesListModel::HasMismatchRole).toBool());
    EXPECT_FALSE(row(3, LinesListModel::HasMismatchRole).toBool()); //!< actualDur неизвестна

    EXPECT_TRUE(row(1, LinesListModel::HasReferenceRole).toBool());
    EXPECT_FALSE(row(3, LinesListModel::HasReferenceRole).toBool());

    //! Раскрытие scene_two: строка l4 появляется; повторное — сворачивает
    const QString key2 = row(4, LinesListModel::SectionKeyRole).toString();
    model->toggleScene(key2);
    ASSERT_EQ(model->rowCount(), 6);
    EXPECT_EQ(row(5, LinesListModel::GuidRole).toString().toStdString(),
              "A0000000000000000000000000000004");
    EXPECT_EQ(row(5, LinesListModel::StatusTextRole).toString().toStdString(), "Готова");

    model->toggleScene(key2);
    EXPECT_EQ(model->rowCount(), 5);

    //! Развернуть/свернуть всё
    model->setAllScenesExpanded(true);
    EXPECT_EQ(model->rowCount(), 6); //!< 2 заголовка + 4 реплики
    model->setAllScenesExpanded(false);
    EXPECT_EQ(model->rowCount(), 2); //!< только заголовки
}

//! Иерархия первого уровня: выбор файла (Select над списком) ограничивает
//! раскладку сценами выбранного файла; свёрнутость переживает переключение.
TEST_F(DubbingPanelTests, Hierarchy_FileSelectAndSections)
{
    meta() = makeManyFilesMeta(); //!< 50 файлов x 60 сцен x 10 реплик

    auto model = makeModel();
    model->reload();

    ASSERT_EQ(model->fileIds().size(), 50u);
    EXPECT_EQ(model->fileId().toStdString(), "file_0"); //!< авто-выбор первого

    //! file_0: 60 заголовков; первая сцена раскрыта (+10 реплик)
    EXPECT_EQ(model->rowCount(), 60 + 10);

    //! выбираем file_7: только его сцены, первая раскрыта
    model->setFileId(QStringLiteral("file_7"));
    EXPECT_EQ(model->rowCount(), 60 + 10);
    EXPECT_EQ(model->data(model->index(0, 0), LinesListModel::SectionTitleRole).toString().toStdString(),
              "quest_7_0");

    //! развернуть всё в file_7: 60 заголовков + 600 реплик
    model->setAllScenesExpanded(true);
    EXPECT_EQ(model->rowCount(), 60 + 600);

    //! возврат к file_0: раскладка file_0 не тронута (свёрнутость по ключам)
    model->setFileId(QStringLiteral("file_0"));
    EXPECT_EQ(model->rowCount(), 60 + 10);
}

//! Фильтры §6.3: UNKNOWN / статус / расхождение / нет референса.
//! Режим фильтрации включает ВСЕ реплики файла, включая свёрнутые секции.
TEST_F(DubbingPanelTests, Filters_UnknownStatusMismatchNoReference)
{
    meta() = makeSmallMeta();

    auto model = makeModel();
    model->reload();
    model->setFilteringActive(true); //!< плоский список (scene_two свёрнута!)
    ASSERT_EQ(model->rowCount(), 4);

    LinesFilterModel filter;
    filter.setSourceModel(model.get());

    const auto guids = [&filter]() {
        std::vector<std::string> result;
        for (int i = 0; i < filter.rowCount(); ++i) {
            QModelIndex idx = filter.index(i, 0);
            result.push_back(
                filter.data(idx, LinesListModel::GuidRole).toString().toStdString());
        }
        return result;
    };

    //! UNKNOWN: строки 2 (speaker_name -> UNKNOWN) и 4
    filter.setOnlyUnknown(true);
    EXPECT_EQ(guids(), (std::vector<std::string> { "A0000000000000000000000000000002",
                                                   "A0000000000000000000000000000004" }));

    //! сброс фильтров (QObject не копируется — сбрасываем сеттерами)
    const auto resetFilters = [&filter]() {
        filter.setOnlyUnknown(false);
        filter.setStatusFilter(-1);
        filter.setOnlyMismatch(false);
        filter.setOnlyNoReference(false);
    };

    //! статус «Нет референса»: только строка 3
    resetFilters();
    filter.setStatusFilter(static_cast<int>(LineStatus::NoReference));
    EXPECT_EQ(guids(), (std::vector<std::string> { "A0000000000000000000000000000003" }));

    //! расхождение: только строка 2 (+0.5 c > порога 0.1)
    resetFilters();
    filter.setOnlyMismatch(true);
    EXPECT_EQ(guids(), (std::vector<std::string> { "A0000000000000000000000000000002" }));

    //! нет референса: только строка 3
    resetFilters();
    filter.setOnlyNoReference(true);
    EXPECT_EQ(guids(), (std::vector<std::string> { "A0000000000000000000000000000003" }));

    //! комбинированный: UNKNOWN + расхождение -> строка 2
    resetFilters();
    filter.setOnlyUnknown(true);
    filter.setOnlyMismatch(true);
    EXPECT_EQ(guids(), (std::vector<std::string> { "A0000000000000000000000000000002" }));
}

//! Полнотекстовый поиск: guid / спикер / EN / RU / сцена, регистронезависимо.
TEST_F(DubbingPanelTests, Search_FullText)
{
    meta() = makeSmallMeta();

    auto model = makeModel();
    model->reload();
    model->setFilteringActive(true);
    ASSERT_EQ(model->rowCount(), 4);

    LinesFilterModel filter;
    filter.setSourceModel(model.get());

    const auto count = [&filter]() {
        return filter.rowCount();
    };

    //! поиск по RU
    filter.setSearchText("привет");
    EXPECT_EQ(count(), 1);
    EXPECT_EQ(filter.data(filter.index(0, 0), LinesListModel::RuRole).toString().toStdString(),
              "Привет, Коэн");

    //! поиск по EN (нижний регистр запроса)
    filter.setSearchText("unknown speaker");
    EXPECT_EQ(count(), 1);

    //! поиск по guid (hex-регистр не важен)
    filter.setSearchText("a0000000000000000000000000000004");
    EXPECT_EQ(count(), 1);
    EXPECT_EQ(filter.data(filter.index(0, 0), LinesListModel::StatusTextRole).toString().toStdString(),
              "Готова");

    //! поиск по сцене
    filter.setSearchText("scene_two");
    EXPECT_EQ(count(), 1);

    //! нет совпадений
    filter.setSearchText("нет такой строки");
    EXPECT_EQ(count(), 0);

    //! сброс поиска (как syncFilteringMode в QML): возвращается иерархия —
    //! 2 заголовка + 3 реплики раскрытой первой сцены
    filter.setSearchText("");
    model->setFilteringActive(false);
    EXPECT_EQ(count(), 5);
}

//! Критерий готовности §M3: 30 000 реплик. Замеры: построение модели,
//! проход фильтра, проход поиска, выборка видимой страницы (50 строк,
//! все роли). data()/rowCount — O(1), ListView создаёт делегаты только
//! для видимых строк — бюджет на кадр задаётся выборкой страницы.
TEST_F(DubbingPanelTests, Virtualization_30k_Performance)
{
    const int totalLines = 30000;
    meta() = make30kMeta(); //!< ОДИН файл: 600 сцен x 50 реплик

    auto model = makeModel();

    //! 1) Построение модели из снимка домена (свёрнутые секции:
    //! 600 заголовков + 50 реплик раскрытой первой сцены)
    const auto t0 = std::chrono::steady_clock::now();
    model->reload();
    const double buildMs = msSince(t0);
    std::cout << "[  PERF  ] build 30000 rows (collapsed): " << buildMs << " ms" << std::endl;
    ASSERT_EQ(model->rowCount(), 600 + 50);
    EXPECT_LT(buildMs, 2000.0) << "построение модели на 30 000 реплик уложилось в бюджет";

    //! 1б) Полная раскладка: все секции раскрыты (600 заголовков + 30000 реплик)
    const auto t0b = std::chrono::steady_clock::now();
    model->setAllScenesExpanded(true);
    const double expandMs = msSince(t0b);
    std::cout << "[  PERF  ] expand all (30600 rows): " << expandMs << " ms" << std::endl;
    ASSERT_EQ(model->rowCount(), totalLines + 600);
    EXPECT_LT(expandMs, 2000.0);

    LinesFilterModel filter;
    filter.setSourceModel(model.get());

    //! 2) Проход фильтра «UNKNOWN» по 30 000 реплик: включаем поисковый
    //! режим (плоский список, свёрнутость секций не мешает фильтрам)
    model->setFilteringActive(true);

    //! 2) Проход фильтра «UNKNOWN» по 30 000 реплик (заголовки скрыты)
    const auto t1 = std::chrono::steady_clock::now();
    filter.setOnlyUnknown(true);
    const double filterMs = msSince(t1);
    std::cout << "[  PERF  ] filter UNKNOWN over 30000 rows: " << filterMs << " ms, kept "
              << filter.rowCount() << std::endl;
    EXPECT_EQ(filter.rowCount(), totalLines / 5); //!< каждый 5-й — UNKNOWN
    EXPECT_LT(filterMs, 1000.0);

    //! 3) Полнотекстовый поиск по 30 000 строкам (запрос с границей поля:
    //! «...1234 русская...» отсекает 12340..12349 — подстрочный поиск);
    //! предварительно снимаем фильтр UNKNOWN (1234 не кратен 5)
    filter.setOnlyUnknown(false);
    const auto t2 = std::chrono::steady_clock::now();
    filter.setSearchText("number 1234 русская");
    const double searchMs = msSince(t2);
    std::cout << "[  PERF  ] search over 30000 rows: " << searchMs << " ms, kept "
              << filter.rowCount() << std::endl;
    EXPECT_EQ(filter.rowCount(), 1); //!< "English line number 1234" — ровно одна
    EXPECT_LT(searchMs, 1000.0);

    //! 4) Выборка видимой страницы (эмуляция кадра ListView):
    //! 50 строк x все роли, из середины списка (возврат к иерархии 30600).
    filter.setSearchText("");
    filter.setOnlyUnknown(false);
    model->setFilteringActive(false);
    ASSERT_EQ(model->rowCount(), totalLines + 600);

    static const int roles[] = {
        LinesListModel::GuidRole, LinesListModel::FileIdRole, LinesListModel::QuestIdRole,
        LinesListModel::StatusRole, LinesListModel::StatusTextRole, LinesListModel::SpeakerRole,
        LinesListModel::EnRole, LinesListModel::RuRole, LinesListModel::DurRole,
        LinesListModel::ActualDurRole, LinesListModel::MismatchRole, LinesListModel::HasMismatchRole,
        LinesListModel::HasReferenceRole, LinesListModel::RefTrackIdRole, LinesListModel::RefClipIdRole,
        LinesListModel::OrderIndexRole
    };

    const auto t3 = std::chrono::steady_clock::now();
    int fetched = 0;
    for (int page = 0; page < 10; ++page) { //!< 10 «кадров» прокрутки
        const int base = 15000 + page * 50;
        for (int r = base; r < base + 50; ++r) {
            const QModelIndex idx = filter.index(r, 0);
            for (int role : roles) {
                fetched += filter.data(idx, role).isNull() ? 0 : 1;
            }
        }
    }
    const double viewportMs = msSince(t3);
    std::cout << "[  PERF  ] 10 viewport pages (50 rows, all roles): " << viewportMs
              << " ms (" << fetched << " values)" << std::endl;
    EXPECT_LT(viewportMs, 50.0) << "10 кадров прокрутки укладываются в бюджет";

    //! 5) rowCount — O(1): повторные вызовы бесплатны
    const auto t4 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) {
        filter.rowCount();
    }
    const double rowCountMs = msSince(t4);
    std::cout << "[  PERF  ] 100000 x rowCount(): " << rowCountMs << " ms" << std::endl;
    EXPECT_LT(rowCountMs, 100.0);
}

//! Двойной клик: openLine выделяет референс-клип (ISelectionController)
//! и ставит позицию воспроизведения в начало клипа
//! (IPlaybackController::setLastPlaybackSeekTime — путь PlaybackStateModel).
TEST_F(DubbingPanelTests, OpenLine_SelectsClipAndSeeks)
{
    std::error_code ec;
    std::filesystem::remove_all(wavDir(), ec);
    writeSilenceWav(wavDir() / (std::string(GUID_FIRST) + ".wav"), 1.861);

    auto result = m_service->importProject(muse::io::path_t(SAMPLE_JSON),
                                           muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.wav.importedCount, 1);

    Line* line = findLine(meta(), GUID_FIRST);
    ASSERT_TRUE(line);
    ASSERT_NE(line->refClipId, NO_CLIP_ID);

    auto controller = makeController();

    //! ожидания: выделение ровно одного клипа с ClipKey реплики
    EXPECT_CALL(*m_selectionController, setSelectedClips(testing::_, true)).WillOnce(
        Invoke([line](const au::trackedit::ClipKeyList& clips, bool) {
            ASSERT_EQ(clips.size(), 1u);
            EXPECT_EQ(clips[0].trackId, line->refTrackId);
            EXPECT_EQ(clips[0].itemId, line->refClipId);
        }));

    //! фокус дорожки референса (путь UI-клика: trackclipslistmodel.cpp:803)
    EXPECT_CALL(*m_trackNavigationController, setFocusedTrack(line->refTrackId, false)).Times(1);

    //! позиция воспроизведения = начало референс-клипа (первый клип дорожки -> 0.0);
    //! number_t<double> не матчится DoubleNear — снимаем значение через Invoke
    double seekTime = -12345.0;
    EXPECT_CALL(*m_playbackController, setLastPlaybackSeekTime).Times(1).WillOnce(
        Invoke([&](muse::secs_t t) {
            seekTime = t.raw();
        }));

    EXPECT_TRUE(controller->openLine(QString::fromUtf8(GUID_FIRST)));
    EXPECT_NEAR(seekTime, 0.0, 1e-6); //!< позиция = начало первого клипа REF-дорожки

    //! реплика без референса: false, ничего не выделяется
    EXPECT_CALL(*m_selectionController, setSelectedClips(testing::_, testing::_)).Times(0);
    EXPECT_FALSE(controller->openLine(QString::fromUtf8(GUID_NO_WAV)));

    //! lineInfo рабочей зоны (вариант А: позиция референса для метки)
    const QVariantMap info = controller->lineInfo(QString::fromUtf8(GUID_FIRST));
    EXPECT_EQ(info["speaker"].toString().toStdString(), "Lunka");
    EXPECT_TRUE(info["hasReference"].toBool());
    EXPECT_EQ(info["en"].toString().toStdString(), "Oh! Coen, look!");
    EXPECT_NEAR(info["refStart"].toDouble(), 0.0, 1e-6); //!< первый клип дорожки
    EXPECT_NEAR(controller->lineInfo(QString::fromUtf8(GUID_NO_WAV))["refStart"].toDouble(), -1.0, 1e-6);
}

//! Правка RU-текста из панели: только pushHistoryState («Правка текста
//! реплики»), отмена и возврат штатным undo/redo; модель обновляется
//! по domainChanged автоматически.
TEST_F(DubbingPanelTests, PanelTextEdit_UndoRedo_ModelFollows)
{
    ASSERT_TRUE(m_service->importFromJson(muse::io::path_t(SAMPLE_JSON)).ok);

    const std::string original = findLine(meta(), GUID_FIRST)->ru;

    auto model = makeModel();
    model->reload(); //!< подключает domainChanged + иерархия (первый файл)

    //! Строка реплики ищется сканом по guid (раскладка содержит заголовки сцен)
    const auto findRow = [&model](const char* guid) {
        for (int r = 0; r < model->rowCount(); ++r) {
            if (model->data(model->index(r, 0), LinesListModel::GuidRole).toString().toStdString()
                == guid) {
                return r;
            }
        }
        return -1;
    };

    //! GUID_FIRST — в первом файле домена: раскрываем все сцены и находим
    model->setAllScenesExpanded(true);
    ASSERT_GE(findRow(GUID_FIRST), 0);

    auto controller = makeController();
    ASSERT_TRUE(controller->setRuText(QString::fromUtf8(GUID_FIRST), "Правка из панели"));

    //! домен обновлён
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, "Правка из панели");

    //! модель перестроилась по domainChanged БЕЗ ручного reload()
    const int row = findRow(GUID_FIRST);
    ASSERT_GE(row, 0);
    EXPECT_EQ(model->data(model->index(row, 0), LinesListModel::RuRole).toString().toStdString(),
              "Правка из панели");

    //! отмена правки (Ctrl+Z): штатный undo, один шаг
    m_history->undo();
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, original);
    EXPECT_EQ(model->data(model->index(findRow(GUID_FIRST), 0),
                          LinesListModel::RuRole).toString().toStdString(), original);

    //! возврат (Ctrl+Shift+Z)
    m_history->redo();
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, "Правка из панели");

    //! несуществующий guid — отказ
    EXPECT_FALSE(controller->setRuText("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", "x"));
}

//! actualDur (колонка/фильтр расхождений) переживает save/load —
//! атрибут actual_dur опционален при чтении (старые файлы читаются).
TEST_F(DubbingPanelTests, ActualDur_SavedAndReloaded)
{
    meta() = makeSmallMeta();

    auto path = std::filesystem::temp_directory_path() / "dubbing_m3_actualdur.aup4";
    const muse::io::path_t filePath = path.lexically_normal().string();

    ProjectHistory::Get(projectRef()).InitialState();
    ASSERT_TRUE(m_accessor->save(filePath));

    auto accessor2 = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
    ASSERT_TRUE(accessor2->open().valid());
    ASSERT_TRUE(accessor2->load(filePath, true).valid());

    const DubbingMeta& reloaded = DubbingProject::Get(
        *reinterpret_cast<Au3Project*>(accessor2->au3ProjectPtr())).meta();

    ASSERT_EQ(reloaded.files.size(), 1u);
    ASSERT_EQ(reloaded.files[0].scenes.size(), 2u);
    const auto& lines = reloaded.files[0].scenes[0].lines;
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_NEAR(lines[0].actualDur, 1.02, 1e-6);
    EXPECT_NEAR(lines[1].actualDur, 2.5, 1e-6);
    EXPECT_NEAR(lines[2].actualDur, -1.0, 1e-6);

    accessor2->close();
    ProjectFileIO::RemoveProject(wxString::FromUTF8(path.string().c_str()));
}

//! Генератор проекта для РУЧНОЙ проверки M1–M3 через exe (инструкция —
//! отчёт сессии / SESSION_NOTES): sample.json + 3 WAV (один с расхождением
//! длительности, 44 реплики без референса), одна правка RU-текста.
//! Файл: <repo>/manual_check/m3_dubbing_demo.aup4 — открыть в
//! dist\bin\Audacity4.exe, меню «Вид -> Реплики».
TEST_F(DubbingPanelTests, ManualCheck_CreateDemoProject)
{
    std::error_code ec;
    std::filesystem::remove_all(wavDir(), ec);
    writeSilenceWav(wavDir() / (std::string(GUID_FIRST) + ".wav"), 1.861);
    writeSilenceWav(wavDir() / (std::string(GUID_SECOND) + ".wav"), 1.075);
    writeSilenceWav(wavDir() / (std::string(GUID_MISMATCH) + ".wav"), 2.2); //!< расхождение: dur 1.732

    auto result = m_service->importProject(muse::io::path_t(SAMPLE_JSON),
                                           muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.json.linesTotal, 47);
    ASSERT_EQ(result.wav.importedCount, 3);
    ASSERT_EQ(result.wav.mismatches.size(), 1u);

    ASSERT_TRUE(m_service->setLineRu(GUID_FIRST, "Правка текста для ручной проверки"));

    const auto out = std::filesystem::weakly_canonical(
        std::filesystem::path(dubbing_tests_DATA_ROOT) / "../../../manual_check/m3_dubbing_demo.aup4");
    std::filesystem::create_directories(out.parent_path());

    ProjectHistory::Get(projectRef()).InitialState();
    ASSERT_TRUE(m_accessor->save(muse::io::path_t(out.string())));
    ASSERT_TRUE(std::filesystem::exists(out));

    std::cout << "\n[MANUAL] Проект для ручной проверки M3: " << out.string() << "\n"
              << "[MANUAL] Открыть в dist\\bin\\Audacity4.exe -> Вид -> Реплики\n" << std::endl;
}
}
