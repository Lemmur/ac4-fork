/*
* Audacity: A Digital Audio Editor
*
* Модель списка реплик (M3, §6.3): ПЛОСКАЯ таблица строк домена
* (файл игры -> сцена -> реплика разворачивается один раз при reload),
* строки хранятся в std::vector<Row> — data() имеет цену O(1), rowCount
* O(1), QML ListView создаёт делегаты только для видимых строк
* (виртуализация на десятках тысяч реплик).
* Перестройка — по IDubbingProject::domainChanged() и смене текущего
* проекта (IGlobalContext::currentProjectChanged).
*/
#pragma once

#include <QAbstractListModel>
#include <QQmlParserStatus>

#include "async/asyncable.h"

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iprojecthistory.h"

#include "../dubbingtypes.h"
#include "../idubbingproject.h"

namespace au::dubbing {
class LinesListModel : public QAbstractListModel, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

public:
    explicit LinesListModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    enum Roles {
        GuidRole = Qt::UserRole + 1,
        FileIdRole,
        QuestIdRole,
        StatusRole,
        StatusTextRole,
        SpeakerRole,
        EnRole,
        RuRole,
        DurRole,          //!< длительность референса: фактическая, если известна, иначе из JSON
        DeclaredDurRole,  //!< dur из JSON (база сверки расхождения)
        ActualDurRole,    //!< фактическая длительность WAV, -1 = неизвестно
        MismatchRole,     //!< actualDur - dur (0, если фактическая неизвестна)
        HasMismatchRole,  //!< |actualDur - dur| > порога (dubbingconfiguration.h)
        HasReferenceRole, //!< референсный клип есть (refClipId != NO_CLIP_ID)
        RefTrackIdRole,
        RefClipIdRole,
        OrderIndexRole,
        SearchBlobRole,   //!< внутренняя роль: нижний регистр текстовых полей (быстрый поиск)
    };

    //! Перестройка из снимка домена (публично — для тестов; из QML вызывается
    //! автоматически при componentComplete и по уведомлениям).
    Q_INVOKABLE void reload();

    Q_INVOKABLE QString guidAt(int row) const;

    //! Русское название статуса (колонка «статус», §6.3)
    static QString statusText(LineStatus status);

    muse::ContextInject<IDubbingProject> dubbingProject = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<trackedit::IProjectHistory> projectHistory = { this };

public: // QQmlParserStatus
    void classBegin() override;
    void componentComplete() override { reload(); }

private:
    void buildFromSnapshot(const DubbingMeta& meta);
    void connectNotifications();

    struct Row {
        QString guid;
        QString fileId;
        QString questId;
        QString speaker;
        QString en;
        QString ru;
        QString searchBlob; //!< guid+file+scene+speaker+en+ru в нижнем регистре (один contains на строку)
        double dur = 0.0;
        double actualDur = -1.0;
        LineStatus status = LineStatus::New;
        int64_t refTrackId = NO_TRACK_ID;
        int64_t refClipId = NO_CLIP_ID;
        int orderIndex = 0;
    };

    std::vector<Row> m_rows;
    bool m_connected = false;
};
}
