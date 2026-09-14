/*
* Audacity: A Digital Audio Editor
*
* M2 roadmap, критерий готовности (roadmap §M2): импорт sample.json + папки
* с WAV: дерево файлов/сцен/реплик построено, порядок реплик = порядку ключей
* JSON; отсутствующий WAV -> статус «нет референса», остальные импортированы;
* расхождение длительности > порога -> предупреждение; повторный импорт после
* правки текста не затирает правки; undo возвращает проект к состоянию
* до импорта.
*
* Проводка зависимостей — как в src/trackedit/tests/au3tracksinteraction_tests.cpp:
* реальные внутренние классы (Au3Importer, Au3TracksInteraction,
* Au3ProjectHistory) с подстановкой моков через Inject::set().
*/
#include <gtest/gtest.h>
#include <gmock/gmock.h>

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

// реальные внутренние классы
#include "trackedit/internal/au3/au3tracksinteraction.h"
#include "trackedit/internal/au3/au3projecthistory.h"
#include "importexport/import/internal/au3/au3importer.h"

#include "au3wrap/au3types.h"
#include "au3wrap/internal/au3project.h"
#include "au3wrap/internal/domaccessor.h"
#include "au3wrap/internal/domconverter.h"

#include "au3-project-history/UndoManager.h"
#include "au3-track/Track.h"

#include "../internal/dubbingproject.h"
#include "../internal/dubbingservice.h"

using ::testing::NiceMock;
using ::testing::Invoke;
using ::testing::Return;

using namespace au;
using namespace au::au3;

namespace au::dubbing {
namespace {
const std::string SAMPLE_JSON = std::string(dubbing_tests_DATA_ROOT) + "/data/sample.json";
const std::string MINI_UNKNOWN_JSON = std::string(dubbing_tests_DATA_ROOT) + "/data/mini_unknown.json";

// guid-ы из sample.json (сцена cs_q000_1_opening файла q000_intro)
const char* GUID_FIRST = "6046256F4DF7E505F0906FBE58C09951";   // dur 1.861
const char* GUID_SECOND = "1C89F59B48693450902DBC8F43F87202";  // dur 1.075
const char* GUID_MISMATCH = "2A0F59574157D9EAD561A992313C42EE"; //!< dur 1.732, WAV будет 2.2 c
const char* GUID_NO_WAV = "41E304C446621137356856820F7BD252";  // dur 3.51, WAV не даём
const char* GUID_LATER = "4CD5D3A744E1DEA9B925DFA42BC98007";   //!< dur 0.747, добавим во втором проходе

//! Синтетический WAV: RIFF/WAVE, PCM 16 бит, моно 44100, тишина.
void writeSilenceWav(const std::filesystem::path& path, double seconds)
{
    const uint32_t rate = 44100;
    const uint32_t samples = static_cast<uint32_t>(std::llround(seconds * static_cast<double>(rate)));
    const uint32_t dataBytes = samples * 2; // 16 бит, моно
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

std::string joinErrors(const std::vector<std::string>& errors)
{
    std::string joined;
    for (const std::string& e : errors) {
        joined += e;
        joined += "\n";
    }
    return joined;
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

size_t totalClipCount(AudacityProject& prj)
{
    size_t count = 0;
    for (const Au3Track* t : Au3TrackList::Get(prj)) {
        if (const Au3WaveTrack* wt = dynamic_cast<const Au3WaveTrack*>(t)) {
            count += wt->Intervals().size();
        }
    }
    return count;
}
}

namespace {
//! Подстановка зависимостей. У внутренних классов trackedit/importer Inject-поля
//! приватны (friend только штатным тестам), поэтому реальные классы создаются
//! с собственным контекстом теста (ioc(globalCtx()) при id==0 возвращает null —
//! kors ioc.cpp:46-49), а реализации регистрируются в IOC этого контекста:
//! контекстные интерфейсы — в ioc(ctx), глобальные — в globalIoc().
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

class DubbingImportTests : public ::testing::Test
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

        ON_CALL(*m_globalContext, currentProject()).WillByDefault(Return(m_currentProject));
        ON_CALL(*m_globalContext, currentTrackeditProject()).WillByDefault(Return(m_trackEditProject));
        ON_CALL(*m_currentProject, au3ProjectPtr()).WillByDefault(Return(m_accessor->au3ProjectPtr()));
        ON_CALL(*m_currentProject, trackeditProject()).WillByDefault(Return(m_trackEditProject));

        //! выделение дорожек должно быть СОСТОЯТЕЛЬНЫМ: importIntoTrackInternal
        //! делает setSelectedTracks({dst}), а paste() читает selectedTracks()
        //! для выбора ветки вставки в целевую дорожку (иначе — pasteIntoNewTracks)
        ON_CALL(*m_selectionController, setSelectedTracks).WillByDefault(Invoke([this](const au::trackedit::TrackIdList& tracks, bool) {
            m_selectedTracks = tracks;
        }));
        ON_CALL(*m_selectionController, selectedTracks).WillByDefault(Invoke([this]() {
            return m_selectedTracks;
        }));
        ON_CALL(*m_selectionController, resetSelectedTracks).WillByDefault(Invoke([this]() {
            m_selectedTracks.clear();
        }));

        //! trackList отдаёт реальные дорожки проекта (как au3interactiontestbase.h)
        ON_CALL(*m_trackEditProject, trackList()).WillByDefault(Invoke([this]() {
            std::vector<au::trackedit::Track> result;
            for (const Au3Track* t : Au3TrackList::Get(projectRef())) {
                result.push_back(DomConverter::track(t));
            }
            return result;
        }));

        //! Реальные внутренние классы; их Inject-поля приватны (friend только
        //! штатным тестам), поэтому зависимости разрешаются через IOC
        //! контекста теста.
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
        return std::filesystem::temp_directory_path() / "dubbing_m2_wav";
    }

    //! Три WAV: два совпадают по dur, один (GUID_MISMATCH) длиннее на ~0.47 c.
    //! Второй кладём в подпапку — проверка рекурсивного сканирования.
    void prepareWavFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(wavDir(), ec); //!< устойчиво к случайно открытым хендлам
        writeSilenceWav(wavDir() / (std::string(GUID_FIRST) + ".wav"), 1.861);
        writeSilenceWav(wavDir() / "sub" / (std::string(GUID_SECOND) + ".wav"), 1.075);
        writeSilenceWav(wavDir() / (std::string(GUID_MISMATCH) + ".wav"), 2.2);
    }

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

    au::trackedit::TrackIdList m_selectedTracks;
    muse::modularity::ContextPtr m_testCtx;
    std::shared_ptr<au::trackedit::Au3TracksInteraction> m_tracksInteraction;
    std::shared_ptr<au::trackedit::Au3ProjectHistory> m_history;
    std::shared_ptr<au::importexport::Au3Importer> m_importer;
    std::shared_ptr<DubbingService> m_service;
};

//! (a) Дерево файлов/сцен/реплик построено; порядок реплик = порядок ключей
//! JSON (orderIndex), а не алфавитный порядок guid-ов.
TEST_F(DubbingImportTests, ImportFromJson_BuildsTreeInJsonOrder)
{
    auto result = m_service->importFromJson(muse::io::path_t(SAMPLE_JSON));

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.errors.empty()) << result.errors.front();
    EXPECT_EQ(result.filesTotal, 2);
    EXPECT_EQ(result.scenesTotal, 3);
    EXPECT_EQ(result.linesTotal, 47);
    EXPECT_EQ(result.linesAdded, 47 + 1); //!< 47 реплик + переход в дубляж-проект

    const DubbingMeta& m = meta();
    ASSERT_TRUE(m.isDubbing);
    ASSERT_EQ(m.files.size(), 2u);
    EXPECT_EQ(m.files[0].fileId, "q000_intro");
    ASSERT_EQ(m.files[0].scenes.size(), 2u);
    EXPECT_EQ(m.files[0].scenes[0].questId, "cs_q000_1_opening");
    EXPECT_EQ(m.files[0].scenes[1].questId, "q000_01_calling_lunka_gpl");

    const std::vector<Line>& lines = m.files[0].scenes[0].lines;
    ASSERT_EQ(lines.size(), 20u);
    //! алфавитно первым был бы 039DAE9E…: доказываем сохранение порядка ключей JSON
    EXPECT_EQ(lines[0].guid, GUID_FIRST);
    for (size_t i = 0; i < lines.size(); ++i) {
        EXPECT_EQ(lines[i].orderIndex, static_cast<int>(i)) << "index " << i;
        EXPECT_EQ(lines[i].status, LineStatus::New);
    }
    EXPECT_EQ(lines[0].en, "Oh! Coen, look!");
    EXPECT_EQ(lines[0].ru, "О! Коэн, смотри!");
    EXPECT_EQ(lines[0].speakerName, "Lunka");
    EXPECT_EQ(lines[0].speakerInternal, "Character.Secondary.Lunka");
    EXPECT_NEAR(lines[0].dur, 1.861, 1e-9);

    EXPECT_EQ(m.files[1].fileId, "_uncovered_c_scenes");
    ASSERT_EQ(m.files[1].scenes.size(), 1u);
    ASSERT_EQ(m.files[1].scenes[0].lines.size(), 2u);
    EXPECT_EQ(m.files[1].scenes[0].lines[0].guid, "01986A1445E8C13E2BDFDCB50A290081");
    EXPECT_EQ(m.files[1].scenes[0].lines[1].orderIndex, 1);
}

//! (a-доп) Пустой speaker_name -> "UNKNOWN" (мини-фикстура, в sample таких нет).
TEST_F(DubbingImportTests, ImportFromJson_EmptySpeakerBecomesUnknown)
{
    auto result = m_service->importFromJson(muse::io::path_t(MINI_UNKNOWN_JSON));
    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.errors.empty());

    const std::vector<Line>& lines = meta().files[0].scenes[0].lines;
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].guid, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    EXPECT_EQ(lines[0].speakerName, "UNKNOWN");
    EXPECT_EQ(lines[1].speakerName, "Tester");
}

//! (b) Массовый импорт WAV: REF-дорожка на file_id, refClipId заполнены,
//! рекурсивный поиск, реплика без WAV -> NoReference, расхождение длительности
//! -> предупреждение в результате, остальные не блокируются.
TEST_F(DubbingImportTests, ImportWavFolder_ClipsNoReferenceAndMismatch)
{
    prepareWavFolder();
    ASSERT_TRUE(m_service->importFromJson(muse::io::path_t(SAMPLE_JSON)).ok);

    auto result = m_service->importWavFolder(muse::io::path_t(wavDir().string()));

    ASSERT_TRUE(result.ok) << joinErrors(result.errors);
    EXPECT_EQ(result.importedCount, 3);
    EXPECT_EQ(result.noReferenceCount, 47 - 3);

    //! расхождение длительности: 2.2 (факт) против 1.732 (JSON), |diff| > 0.1
    ASSERT_EQ(result.mismatches.size(), 1u);
    EXPECT_EQ(result.mismatches[0].guid, GUID_MISMATCH);
    EXPECT_NEAR(result.mismatches[0].declaredDur, 1.732, 1e-9);
    EXPECT_NEAR(result.mismatches[0].actualDur, 2.2, 0.01);
    EXPECT_GT(std::abs(result.mismatches[0].diff), 0.1);

    //! дорожки: только одна REF q000_intro (у _uncovered… нет WAV — дорожка не создаётся)
    auto& tracks = Au3TrackList::Get(projectRef());
    ASSERT_EQ(tracks.Size(), 1u);
    const Au3Track* refTrack = *tracks.begin();
    EXPECT_EQ(DomConverter::track(refTrack).title.toStdString(), "REF q000_intro");

    const int64_t refTrackId = static_cast<int64_t>(refTrack->GetId());

    //! у реплик с WAV заполнены ссылки на клипы, статус New
    const Line* first = findLine(meta(), GUID_FIRST);
    const Line* second = findLine(meta(), GUID_SECOND);
    const Line* mismatch = findLine(meta(), GUID_MISMATCH);
    ASSERT_TRUE(first && second && mismatch);
    EXPECT_EQ(first->refTrackId, refTrackId);
    EXPECT_NE(first->refClipId, NO_CLIP_ID);
    EXPECT_EQ(second->refTrackId, refTrackId);
    EXPECT_NE(second->refClipId, NO_CLIP_ID);
    EXPECT_EQ(mismatch->refTrackId, refTrackId);
    EXPECT_NE(mismatch->refClipId, NO_CLIP_ID);
    EXPECT_EQ(mismatch->status, LineStatus::New); //!< расхождение — предупреждение, статус не меняется

    //! реплика без WAV: NoReference, ссылок нет (-1 = «нет», 0 — валидный id)
    const Line* noWav = findLine(meta(), GUID_NO_WAV);
    ASSERT_TRUE(noWav);
    EXPECT_EQ(noWav->status, LineStatus::NoReference);
    EXPECT_EQ(noWav->refTrackId, NO_TRACK_ID);
    EXPECT_EQ(noWav->refClipId, NO_CLIP_ID);

    //! клипы расположены последовательно по фактической длительности
    const auto* waveTrack = dynamic_cast<const Au3WaveTrack*>(refTrack);
    ASSERT_TRUE(waveTrack);
    ASSERT_EQ(waveTrack->Intervals().size(), 3u);

    auto clip1 = DomAccessor::findWaveClip(projectRef(), refTrackId, 0.0);
    ASSERT_TRUE(clip1);
    EXPECT_NEAR(clip1->GetPlayStartTime(), 0.0, 1e-6);
    EXPECT_NEAR(clip1->GetPlayEndTime(), 1.861, 0.01);

    auto clip2 = DomAccessor::findWaveClip(projectRef(), refTrackId, clip1->GetPlayEndTime());
    ASSERT_TRUE(clip2);
    EXPECT_NEAR(clip2->GetPlayStartTime(), clip1->GetPlayEndTime(), 1e-6);
    EXPECT_NEAR(clip2->GetPlayEndTime(), clip1->GetPlayEndTime() + 1.075, 0.01);

    auto clip3 = DomAccessor::findWaveClip(projectRef(), refTrackId, clip2->GetPlayEndTime());
    ASSERT_TRUE(clip3);
    EXPECT_NEAR(clip3->GetPlayEndTime(), clip2->GetPlayEndTime() + 2.2, 0.01);
}

//! (b-доп, M2-followup) Соседние расхождения в РАЗНЫЕ СТОРОНЫ: A короче dur,
//! B длиннее, C снова короче. Через объединённый importProject: клипы идут
//! подряд без наложений (start[i+1] == end[i]), длительность каждого клипа
//! равна длительности ЕГО WAV (не dur из JSON), ClipKey каждой реплики
//! указывает на ЕЁ клип (A — первый, B — второй, C — третий), предупреждений
//! о расхождении — 3 шт.
TEST_F(DubbingImportTests, NeighbourMismatchDuration_CorrectClipMapping)
{
    std::error_code ec;
    std::filesystem::remove_all(wavDir(), ec);
    writeSilenceWav(wavDir() / (std::string(GUID_FIRST) + ".wav"), 1.2);    //!< dur 1.861 -> короче
    writeSilenceWav(wavDir() / (std::string(GUID_SECOND) + ".wav"), 1.9);   //!< dur 1.075 -> длиннее
    writeSilenceWav(wavDir() / (std::string(GUID_MISMATCH) + ".wav"), 1.0); //!< dur 1.732 -> короче

    auto result = m_service->importProject(muse::io::path_t(SAMPLE_JSON),
                                           muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(result.ok) << joinErrors(result.json.errors) << joinErrors(result.wav.errors);
    EXPECT_EQ(result.wav.importedCount, 3);

    //! предупреждения о расхождении: 3 шт., в порядке реплик JSON
    ASSERT_EQ(result.wav.mismatches.size(), 3u);
    EXPECT_EQ(result.wav.mismatches[0].guid, GUID_FIRST);
    EXPECT_EQ(result.wav.mismatches[1].guid, GUID_SECOND);
    EXPECT_EQ(result.wav.mismatches[2].guid, GUID_MISMATCH);
    EXPECT_LT(result.wav.mismatches[0].diff, 0.0); //!< короче
    EXPECT_GT(result.wav.mismatches[1].diff, 0.0); //!< длиннее
    EXPECT_LT(result.wav.mismatches[2].diff, 0.0); //!< короче

    //! ссылки заполнены, все три клипа на одной REF-дорожке
    Line* a = findLine(meta(), GUID_FIRST);
    Line* b = findLine(meta(), GUID_SECOND);
    Line* c = findLine(meta(), GUID_MISMATCH);
    ASSERT_TRUE(a && b && c);
    ASSERT_NE(a->refClipId, NO_CLIP_ID);
    ASSERT_NE(b->refClipId, NO_CLIP_ID);
    ASSERT_NE(c->refClipId, NO_CLIP_ID);
    ASSERT_EQ(a->refTrackId, b->refTrackId);
    ASSERT_EQ(b->refTrackId, c->refTrackId);

    Au3WaveTrack* refTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(a->refTrackId));
    ASSERT_TRUE(refTrack);
    std::vector<std::shared_ptr<Au3WaveClip> > clips;
    for (const auto& clip : refTrack->Intervals()) {
        clips.push_back(clip);
    }
    ASSERT_EQ(clips.size(), 3u);

    //! (в) ClipKey указывает на СВОЙ клип: A — первый, B — второй, C — третий
    EXPECT_EQ(clips[0]->GetId(), a->refClipId);
    EXPECT_EQ(clips[1]->GetId(), b->refClipId);
    EXPECT_EQ(clips[2]->GetId(), c->refClipId);

    //! факт. startTime/endTime клипов по треку + clip-id (DomAccessor)
    auto clipA = DomAccessor::findWaveClip(refTrack, a->refClipId);
    auto clipB = DomAccessor::findWaveClip(refTrack, b->refClipId);
    auto clipC = DomAccessor::findWaveClip(refTrack, c->refClipId);
    ASSERT_TRUE(clipA && clipB && clipC);

    //! (а) клипы идут подряд без наложений: start[i+1] == end[i]
    EXPECT_NEAR(clipA->GetPlayStartTime(), 0.0, 1e-6);
    EXPECT_NEAR(clipB->GetPlayStartTime(), clipA->GetPlayEndTime(), 1e-6);
    EXPECT_NEAR(clipC->GetPlayStartTime(), clipB->GetPlayEndTime(), 1e-6);

    //! (б) длительность клипа == длительность ЕГО WAV (не dur из JSON)
    EXPECT_NEAR(clipA->GetPlayEndTime() - clipA->GetPlayStartTime(), 1.2, 0.01);
    EXPECT_NEAR(clipB->GetPlayEndTime() - clipB->GetPlayStartTime(), 1.9, 0.01);
    EXPECT_NEAR(clipC->GetPlayEndTime() - clipC->GetPlayStartTime(), 1.0, 0.01);
}

//! (c) Инкрементальность: после правки RU-текста повторный импорт сохраняет
//! правку, не дублирует реплики и клипы; новый WAV добавляется без наложений.
TEST_F(DubbingImportTests, IncrementalImport_PreservesWork)
{
    prepareWavFolder();
    ASSERT_TRUE(m_service->importFromJson(muse::io::path_t(SAMPLE_JSON)).ok);
    {
        auto wavResult = m_service->importWavFolder(muse::io::path_t(wavDir().string()));
        ASSERT_TRUE(wavResult.ok) << joinErrors(wavResult.errors);
    }

    //! правка текста пользователем
    const std::string editedRu = "Изменённый перевод реплики";
    ASSERT_TRUE(m_service->setLineRu(GUID_FIRST, editedRu));

    //! повторный импорт JSON: существующие реплики не трогаются
    auto jsonAgain = m_service->importFromJson(muse::io::path_t(SAMPLE_JSON));
    ASSERT_TRUE(jsonAgain.ok);
    EXPECT_EQ(jsonAgain.linesAdded, 0);
    EXPECT_EQ(jsonAgain.linesTotal, 47);
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, editedRu);

    //! повторный импорт WAV: клипы не дублируются, ссылки не перезаписываются
    const int64_t refClipIdBefore = findLine(meta(), GUID_FIRST)->refClipId;
    auto wavAgain = m_service->importWavFolder(muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(wavAgain.ok) << joinErrors(wavAgain.errors);
    EXPECT_EQ(wavAgain.importedCount, 0);
    EXPECT_EQ(wavAgain.alreadyImportedCount, 3);
    EXPECT_EQ(wavAgain.noReferenceCount, 47 - 3);
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->refClipId, refClipIdBefore);
    EXPECT_EQ(totalClipCount(projectRef()), 3u);
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, editedRu);

    //! появившийся WAV для новой реплики: импортируется ровно один клип,
    //! без наложения на существующие
    writeSilenceWav(wavDir() / (std::string(GUID_LATER) + ".wav"), 0.747);
    auto wavThird = m_service->importWavFolder(muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(wavThird.ok) << joinErrors(wavThird.errors);
    EXPECT_EQ(wavThird.importedCount, 1);
    EXPECT_EQ(wavThird.alreadyImportedCount, 3);
    EXPECT_EQ(totalClipCount(projectRef()), 4u);

    const Line* later = findLine(meta(), GUID_LATER);
    ASSERT_TRUE(later);
    EXPECT_NE(later->refClipId, NO_CLIP_ID);
    EXPECT_EQ(later->status, LineStatus::New);

    //! новый клип начинается не раньше конца предыдущих
    Au3WaveTrack* laterTrack = DomAccessor::findWaveTrack(projectRef(), Au3TrackId(later->refTrackId));
    ASSERT_TRUE(laterTrack);
    auto newClip = DomAccessor::findWaveClip(laterTrack, later->refClipId);
    ASSERT_TRUE(newClip);
    EXPECT_GE(newClip->GetPlayStartTime(), 1.861 + 1.075 + 2.2 - 0.01);
}

//! (d, M2-followup) Undo: importProject — ЕДИНЫЙ undo-шаг на весь импорт
//! (JSON + WAV без промежуточного пуша); ОДНА отмена возвращает проект
//! к состоянию до импорта (мета без файлов + нет REF-дорожек).
TEST_F(DubbingImportTests, UndoRestoresPreImportState)
{
    prepareWavFolder();

    auto result = m_service->importProject(muse::io::path_t(SAMPLE_JSON),
                                           muse::io::path_t(wavDir().string()));
    ASSERT_TRUE(result.ok) << joinErrors(result.json.errors) << joinErrors(result.wav.errors);
    EXPECT_EQ(result.json.linesTotal, 47);
    EXPECT_EQ(result.wav.importedCount, 3);

    //! UndoManager::Get(project).GetNumStates() == 2: начальное состояние
    //! (InitialState) + ОДНО импортное (промежуточного пуша «Импорт
    //! метаданных дубляжа» больше нет — консолидация не нужна).
    EXPECT_EQ(UndoManager::Get(projectRef()).GetNumStates(), 2u);

    //! undo «Импорт дубляжа»: мета без файлов, проект не дубляжный, нет REF-дорожек
    m_history->undo();
    {
        DubbingMeta& m = meta();
        EXPECT_TRUE(m.files.empty());
        EXPECT_FALSE(m.isDubbing);
    }
    EXPECT_EQ(Au3TrackList::Get(projectRef()).Size(), 0u);
}

//! setLineRu: правка текста реплики отменяется и возвращается штатным undo.
TEST_F(DubbingImportTests, SetLineRu_UndoRedo)
{
    ASSERT_TRUE(m_service->importFromJson(muse::io::path_t(SAMPLE_JSON)).ok);

    const std::string original = findLine(meta(), GUID_FIRST)->ru;
    ASSERT_TRUE(m_service->setLineRu(GUID_FIRST, "Новый перевод"));
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, "Новый перевод");

    m_history->undo(); //!< отмена правки (не отменяет импорт метаданных)
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, original);

    m_history->redo();
    EXPECT_EQ(findLine(meta(), GUID_FIRST)->ru, "Новый перевод");

    //! несуществующий guid — отказ без изменений
    EXPECT_FALSE(m_service->setLineRu("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF", "x"));
}
}
