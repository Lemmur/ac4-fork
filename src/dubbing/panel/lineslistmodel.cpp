/*
* Audacity: A Digital Audio Editor
*
* Реализация модели списка реплик (M3). Плоская развёртка домена
* (порядок = порядок следования в домене: файл -> сцена -> реплика),
* значения кэшируются в QString — data() без конверсий, O(1).
*/
#include "lineslistmodel.h"

#include <cmath>

#include "log.h"

#include "../dubbingconfiguration.h"

using namespace au::dubbing;

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
}

void LinesListModel::buildFromSnapshot(const DubbingMeta& meta)
{
    m_rows.clear();

    if (!meta.isDubbing) {
        return; //!< обычный проект — пустая панель
    }

    //! Оценка ёмкости заранее: одна аллокация на десятки тысяч реплик.
    size_t total = 0;
    for (const GameFile& f : meta.files) {
        for (const Scene& s : f.scenes) {
            total += s.lines.size();
        }
    }
    m_rows.reserve(total);

    for (const GameFile& file : meta.files) {
        const QString fileId = QString::fromStdString(file.fileId).toLower();
        for (const Scene& scene : file.scenes) {
            const QString questId = QString::fromStdString(scene.questId).toLower();
            for (const Line& line : scene.lines) {
                Row row;
                row.guid = QString::fromStdString(line.guid);
                row.fileId = fileId;
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

                row.searchBlob = (row.guid + ' ' + fileId + ' ' + questId + ' '
                                  + row.speaker + ' ' + row.en + ' ' + row.ru).toLower();

                m_rows.push_back(std::move(row));
            }
        }
    }
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
    };
}

QString LinesListModel::guidAt(int row) const
{
    if (row < 0 || static_cast<size_t>(row) >= m_rows.size()) {
        return {};
    }
    return m_rows[static_cast<size_t>(row)].guid;
}
