/*
* Audacity: A Digital Audio Editor
*
* Реализация модели списка реплик (M3). Плоская раскладка с заголовками
* сцен: для выбранного файла идут секции quest_id, внутри — реплики
* (порядок = порядок JSON). Свернутые секции не содержат строк реплик —
* rowCount остаётся O(1), data() O(1) (значения кэшируются в QString).
*/
#include "lineslistmodel.h"

#include <cmath>
#include <map>

#include "log.h"

#include "../dubbingconfiguration.h"

using namespace au::dubbing;

namespace {
//! Ключ секции: файл + сцена (уникален даже при одинаковых quest_id).
QString makeSectionKey(const QString& fileId, const QString& questId)
{
    return fileId + QStringLiteral("\x1f") + questId;
}
}

LinesListModel::LinesListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void LinesListModel::classBegin()
{
    connectNotifications();
}

void LinesListModel::connectNotifications()
{
    if (m_connected) {
        return;
    }

    //! Смена текущего проекта -> перестройка (домен новый).
    if (auto ctx = globalContext()) {
        ctx->currentProjectChanged().onNotify(this, [this] {
            m_fileId.clear(); //!< файл по умолчанию пере-select'ится заново
            reload();
        });
    }

    //! Изменение домена (импорт, правка текста) -> перестройка.
    if (auto dub = dubbingProject()) {
        dub->domainChanged().onNotify(this, [this] {
            reload();
        });
    }

    //! Undo/redo восстанавливает домен молча (DubbingStateExtension) —
    //! панель следит за историей проекта, как HistoryPanelModel.
    if (auto history = projectHistory()) {
        history->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) {
            reload();
        });
    }

    m_connected = true;
}

void LinesListModel::reload()
{
    connectNotifications();

    DubbingMeta snapshot;
    if (auto dub = dubbingProject()) {
        snapshot = dub->domainSnapshot();
    }

    beginResetModel();
    buildFromSnapshot(snapshot);
    endResetModel();

    emit reloaded();
}

void LinesListModel::buildFromSnapshot(const DubbingMeta& meta)
{
    m_rows.clear();
    m_fileIds.clear();

    if (!meta.isDubbing) {
        return; //!< обычный проект — пустая панель
    }

    m_fileIds.reserve(meta.files.size());
    for (const GameFile& f : meta.files) {
        m_fileIds.push_back(QString::fromStdString(f.fileId));
    }

    //! Выбранный файл: сохранённый, иначе первый (по порядку JSON).
    QString selected = m_fileId;
    if (selected.isEmpty() || std::find(m_fileIds.cbegin(), m_fileIds.cend(), selected) == m_fileIds.cend()) {
        selected = m_fileIds.empty() ? QString() : m_fileIds.front();
    }
    if (selected != m_fileId) {
        m_fileId = selected;
    }

    const GameFile* currentFile = nullptr;
    for (const GameFile& f : meta.files) {
        if (QString::fromStdString(f.fileId) == m_fileId) {
            currentFile = &f;
            break;
        }
    }
    if (!currentFile) {
        return;
    }

    //! Режим фильтрации/поиска: плоский список ВСЕХ реплик выбранного файла
    //! (включая свёрнутые секции), без заголовков — фильтры обязаны видеть
    //! содержимое свёрнутых сцен.
    if (m_filteringActive) {
        for (const Scene& scene : currentFile->scenes) {
            appendSceneLines(*currentFile, scene);
        }
        return;
    }

    //! Первая секция по умолчанию раскрыта (обзор + сразу видно содержимое).
    bool firstScene = true;

    for (const Scene& scene : currentFile->scenes) {
        const QString questId = QString::fromStdString(scene.questId);
        const QString sectionKey = makeSectionKey(m_fileId, questId);

        auto it = m_expandedByKey.find(sectionKey.toStdString());
        bool expanded = (it != m_expandedByKey.end()) ? it->second : firstScene;
        firstScene = false;

        Row header;
        header.type = SceneHeaderRow;
        header.sectionKey = sectionKey;
        header.sectionTitle = questId;
        header.sectionLineCount = static_cast<int>(scene.lines.size());
        header.expanded = expanded;
        header.searchBlob = (m_fileId + ' ' + questId).toLower();
        m_rows.push_back(std::move(header));

        if (!expanded) {
            continue;
        }

        appendSceneLines(*currentFile, scene);
    }
}

QString LinesListModel::fileId() const
{
    return m_fileId;
}

void LinesListModel::setFileId(const QString& fileId)
{
    if (m_fileId == fileId) {
        return;
    }
    m_fileId = fileId;
    emit fileIdChanged();
    reload();
}

bool LinesListModel::isFilteringActive() const
{
    return m_filteringActive;
}

void LinesListModel::setFilteringActive(bool active)
{
    if (m_filteringActive == active) {
        return;
    }
    m_filteringActive = active;
    emit filteringActiveChanged();
    reload();
}

//! Добавить строки реплик сцены (общий код иерархического и поискового режимов).
void LinesListModel::appendSceneLines(const GameFile& file, const Scene& scene)
{
    const QString questId = QString::fromStdString(scene.questId);

    m_rows.reserve(m_rows.size() + scene.lines.size());
    for (const Line& line : scene.lines) {
        Row row;
        row.type = LineRow;
        row.guid = QString::fromStdString(line.guid);
        row.fileId = m_fileId;
        row.questId = questId;
        row.speaker = QString::fromStdString(line.speakerName);
        row.en = QString::fromStdString(line.en);
        row.ru = QString::fromStdString(line.ru);
        row.dur = line.dur;
        row.actualDur = line.actualDur;
        row.status = line.status;
        row.refTrackId = line.refTrackId;
        row.refClipId = line.refClipId;
        row.orderIndex = line.orderIndex;

        row.searchBlob = (row.guid + ' ' + m_fileId + ' ' + questId + ' '
                          + row.speaker + ' ' + row.en + ' ' + row.ru).toLower();

        m_rows.push_back(std::move(row));
    }
}

QVariantList LinesListModel::fileIds() const
{
    QVariantList result;
    for (const QString& id : m_fileIds) {
        result << id;
    }
    return result;
}

void LinesListModel::toggleScene(const QString& sectionKey)
{
    const std::string key = sectionKey.toStdString();
    const bool expandedNow = m_expandedByKey.count(key) ? m_expandedByKey[key] : false;
    m_expandedByKey[key] = !expandedNow;
    reload();
}

void LinesListModel::setAllScenesExpanded(bool expanded)
{
    //! Явно фиксируем состояние всех секций текущего файла (при сворачивании
    //! это важно: иначе первая сцена снова развернётся по умолчанию).
    m_expandedByKey.clear();
    for (const Row& r : m_rows) {
        if (r.type == SceneHeaderRow) {
            m_expandedByKey[r.sectionKey.toStdString()] = expanded;
        }
    }
    reload();
}

QString LinesListModel::statusText(LineStatus status)
{
    switch (status) {
    case LineStatus::New: return QStringLiteral("Новая");
    case LineStatus::NoReference: return QStringLiteral("Нет референса");
    case LineStatus::InProgress: return QStringLiteral("В работе");
    case LineStatus::Ready: return QStringLiteral("Готова");
    case LineStatus::Exported: return QStringLiteral("Экспортирована");
    }
    return QStringLiteral("Новая");
}

QVariant LinesListModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || static_cast<size_t>(row) >= m_rows.size()) {
        return {};
    }

    const Row& r = m_rows[static_cast<size_t>(row)];

    switch (role) {
    case RowTypeRole: return static_cast<int>(r.type);
    case SectionKeyRole: return r.sectionKey;
    case SectionTitleRole: return r.sectionTitle;
    case SectionLineCountRole: return r.sectionLineCount;
    case ExpandedRole: return r.expanded;
    }

    if (r.type == SceneHeaderRow) {
        return {}; //!< остальные роли — только для реплик
    }

    switch (role) {
    case GuidRole: return r.guid;
    case FileIdRole: return r.fileId;
    case QuestIdRole: return r.questId;
    case StatusRole: return static_cast<int>(r.status);
    case StatusTextRole: return statusText(r.status);
    case SpeakerRole: return r.speaker;
    case EnRole: return r.en;
    case RuRole: return r.ru;
    case DurRole: return r.actualDur >= 0.0 ? r.actualDur : r.dur;
    case DeclaredDurRole: return r.dur;
    case ActualDurRole: return r.actualDur;
    case MismatchRole: return r.actualDur >= 0.0 ? (r.actualDur - r.dur) : 0.0;
    case HasMismatchRole:
        return r.actualDur >= 0.0
               && std::abs(r.actualDur - r.dur) > DUBBING_DURATION_MISMATCH_THRESHOLD_SECS;
    case HasReferenceRole: return r.refClipId != NO_CLIP_ID;
    case RefTrackIdRole: return static_cast<qlonglong>(r.refTrackId);
    case RefClipIdRole: return static_cast<qlonglong>(r.refClipId);
    case OrderIndexRole: return r.orderIndex;
    case SearchBlobRole: return r.searchBlob;
    }

    return {};
}

int LinesListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_rows.size());
}

QHash<int, QByteArray> LinesListModel::roleNames() const
{
    return {
        { GuidRole, "guid" },
        { FileIdRole, "fileId" },
        { QuestIdRole, "questId" },
        { StatusRole, "statusCode" },
        { StatusTextRole, "statusText" },
        { SpeakerRole, "speaker" },
        { EnRole, "en" },
        { RuRole, "ru" },
        { DurRole, "dur" },
        { DeclaredDurRole, "declaredDur" },
        { ActualDurRole, "actualDur" },
        { MismatchRole, "mismatch" },
        { HasMismatchRole, "hasMismatch" },
        { HasReferenceRole, "hasReference" },
        { RefTrackIdRole, "refTrackId" },
        { RefClipIdRole, "refClipId" },
        { OrderIndexRole, "orderIndex" },
        { SearchBlobRole, "searchBlob" },
        { RowTypeRole, "rowType" },
        { SectionKeyRole, "sectionKey" },
        { SectionTitleRole, "sectionTitle" },
        { SectionLineCountRole, "sectionLineCount" },
        { ExpandedRole, "expanded" },
    };
}

QString LinesListModel::guidAt(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= m_rows.size()) {
        return {};
    }
    const Row& r = m_rows[static_cast<size_t>(row)];
    return r.type == LineRow ? r.guid : QString();
}
