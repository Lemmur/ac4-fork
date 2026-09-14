/*
* Audacity: A Digital Audio Editor
*
* M1 roadmap, критерий готовности: создать дубляж-проект -> изменить RU-текст ->
* undo/redo восстанавливает текст -> сохранить -> переоткрыть -> домен идентичен.
*
* Проект создаётся через Au3ProjectAccessor (au3wrap) — тот же путь, что и в
* src/record/tests/au3record_tests.cpp, с IOC-контекстом muse.
*/
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "modularity/ioc.h"

#include "au3wrap/au3types.h"
#include "au3wrap/internal/au3project.h"

#include "au3-project-file-io/ProjectFileIO.h"
#include "au3-project-history/ProjectHistory.h"
#include "au3-project-history/UndoManager.h"
#include "au3-strings/TranslatableString.h"

#include "../internal/dubbingproject.h"

namespace au::dubbing {
namespace {
using Au3ProjectAccessor = au::au3::Au3ProjectAccessor;
using au::au3::Au3Project;

DubbingMeta makeSampleMeta()
{
    DubbingMeta meta;
    meta.isDubbing = true;
    meta.schemaVersion = 1;

    GameFile file;
    file.fileId = "q000_intro";
    Scene scene;
    scene.questId = "cs_q000_1_opening";
    Line l1;
    l1.guid = "6046256F4DF7E505F0906FBE58C09951";
    l1.en = "Oh! Coen, look!";
    l1.ru = "О! Коэн, смотри!";
    l1.speakerName = "Lunka";
    l1.speakerInternal = "Character.Secondary.Lunka";
    l1.dur = 1.861;
    l1.orderIndex = 0;
    l1.status = LineStatus::New;
    Line l2;
    l2.guid = "1C89F59B48693450902DBC8F43F87202";
    l2.en = "Isn't it pretty?";
    l2.ru = "Красиво, правда?";
    l2.speakerName = "";
    l2.speakerInternal = "Character.Secondary.Lunka";
    l2.dur = 1.075;
    l2.orderIndex = 1;
    l2.status = LineStatus::NoReference;
    l2.takes.push_back(TakeInfo { 11, 22, true });
    l2.masterTrackId = 33;
    l2.masterClipId = 44;
    scene.lines = { l1, l2 };
    file.scenes = { scene };
    meta.files = { file };
    return meta;
}

bool metaEquals(const DubbingMeta& a, const DubbingMeta& b)
{
    if (a.isDubbing != b.isDubbing || a.schemaVersion != b.schemaVersion
        || a.files.size() != b.files.size()) {
        return false;
    }
    for (size_t f = 0; f < a.files.size(); ++f) {
        const auto& fa = a.files[f];
        const auto& fb = b.files[f];
        if (fa.fileId != fb.fileId || fa.scenes.size() != fb.scenes.size()) {
            return false;
        }
        for (size_t s = 0; s < fa.scenes.size(); ++s) {
            const auto& sa = fa.scenes[s];
            const auto& sb = fb.scenes[s];
            if (sa.questId != sb.questId || sa.lines.size() != sb.lines.size()) {
                return false;
            }
            for (size_t l = 0; l < sa.lines.size(); ++l) {
                const auto& la = sa.lines[l];
                const auto& lb = sb.lines[l];
                if (la.guid != lb.guid || la.en != lb.en || la.ru != lb.ru
                    || la.speakerName != lb.speakerName || la.speakerInternal != lb.speakerInternal
                    || std::abs(la.dur - lb.dur) > 1e-6 || la.orderIndex != lb.orderIndex
                    || la.status != lb.status || la.refTrackId != lb.refTrackId
                    || la.refClipId != lb.refClipId || la.masterTrackId != lb.masterTrackId
                    || la.masterClipId != lb.masterClipId || la.takes.size() != lb.takes.size()) {
                    return false;
                }
                for (size_t t = 0; t < la.takes.size(); ++t) {
                    if (la.takes[t].trackId != lb.takes[t].trackId
                        || la.takes[t].clipId != lb.takes[t].clipId
                        || la.takes[t].markedBest != lb.takes[t].markedBest) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}
}

class DubbingDomainTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_accessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
        ASSERT_TRUE(m_accessor->open().valid()); // AudacityProject::Create + временная база
        DubbingProject::Get(projectRef()).setMeta(makeSampleMeta());
    }

    void TearDown() override
    {
        if (m_accessor) {
            m_accessor->close();
        }
    }

    Au3Project& projectRef() const
    {
        return *reinterpret_cast<Au3Project*>(m_accessor->au3ProjectPtr());
    }

    Line& firstLine()
    {
        return DubbingProject::Get(projectRef()).meta().files[0].scenes[0].lines[0];
    }

    std::shared_ptr<Au3ProjectAccessor> m_accessor;
};

//! Правка RU-текста отменяется и возвращается штатным Undo/Redo au3
//! (ProjectHistory + DubbingStateExtension), без параллельного механизма.
TEST_F(DubbingDomainTests, UndoRedo_RuText)
{
    auto& history = ProjectHistory::Get(projectRef());
    history.InitialState();

    const std::string before = firstLine().ru;
    firstLine().ru = "О! Коэн, взгляни!";
    history.PushState(Verbatim("Правка текста реплики"), Verbatim("Правка текста"));

    ASSERT_EQ(firstLine().ru, "О! Коэн, взгляни!");
    ASSERT_TRUE(history.UndoAvailable());

    history.SetStateTo(0); // undo
    EXPECT_EQ(firstLine().ru, before);

    ASSERT_TRUE(history.RedoAvailable());
    history.SetStateTo(1); // redo
    EXPECT_EQ(firstLine().ru, "О! Коэн, взгляни!");
}

//! Сохранение в файл и повторное открытие: домен идентичен,
//! файл переносим одним файлом (без дополнительных папок).
TEST_F(DubbingDomainTests, SaveAndReopen_DomainRoundTrip)
{
    auto path = std::filesystem::temp_directory_path() / "dubbing_m1_roundtrip.aup4";
    const muse::io::path_t filePath = path.lexically_normal().string();

    ProjectHistory::Get(projectRef()).InitialState();
    ASSERT_TRUE(m_accessor->save(filePath));
    ASSERT_TRUE(std::filesystem::exists(path));

    // Открываем в новом проекте
    auto accessor2 = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
    ASSERT_TRUE(accessor2->open().valid());
    ASSERT_TRUE(accessor2->load(filePath, true).valid());

    const DubbingMeta& reloaded = DubbingProject::Get(
        *reinterpret_cast<Au3Project*>(accessor2->au3ProjectPtr())).meta();
    EXPECT_TRUE(reloaded.isDubbing);
    EXPECT_TRUE(metaEquals(makeSampleMeta(), reloaded))
    << "домен после переоткрытия должен совпадать с исходным";

    accessor2->close();
    ProjectFileIO::RemoveProject(wxString::FromUTF8(path.string().c_str()));
}

//! Обычный (не дубляж) проект не является дубляжным после создания.
TEST_F(DubbingDomainTests, RegularProject_NotDubbing)
{
    auto accessor = std::make_shared<Au3ProjectAccessor>(muse::modularity::globalCtx());
    ASSERT_TRUE(accessor->open().valid());
    EXPECT_FALSE(DubbingProject::Get(
                     *reinterpret_cast<Au3Project*>(accessor->au3ProjectPtr())).meta().isDubbing);
    accessor->close();
}
}
