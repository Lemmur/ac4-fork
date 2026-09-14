/*
* Audacity: A Digital Audio Editor
*
* Модель списка реплик (M3, §6.3): ПЛОСКАЯ таблица строк домена
* (файл игры -> сцена -> реплика разворачивается один раз при reload)
* c ЗАГОЛОВКАМИ СЦЕН (раскрывающиеся секции) и выбранным файлом игры
* (Select над списком). data()/rowCount() — O(1), QML ListView создаёт
* делегаты только для видимых строк (виртуализация на десятках тысяч
* реплик); сворачивание сцен — пересборка раскладки (beginResetModel).
* Перестройка — по IDubbingProject::domainChanged(), смене проекта
* (IGlobalContext::currentProjectChanged) и undo/redo (historyChanged).
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

    //! Выбранный файл игры (заголовок первого уровня, Select над списком).
    Q_PROPERTY(QString fileId READ fileId WRITE setFileId NOTIFY fileIdChanged)

    //! Режим фильтрации/поиска: плоский список ВСЕХ реплик выбранного файла
    //! (включая свёрнутые секции), без заголовков. Сбрасывается в false —
    //! обратно к раскрывающимся секциям.
    Q_PROPERTY(bool filteringActive READ isFilteringActive WRITE setFilteringActive NOTIFY filteringActiveChanged)

    //! Число строк раскладки (для счётчика QML: rowCount без скобок — method)
    Q_PROPERTY(int count READ rowCount NOTIFY reloaded)

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
        // Иерархия (M3-редизайн: раскрывающиеся заголовки сцен)
        RowTypeRole,          //!< 0 = реплика, 1 = заголовок сцены
        SectionKeyRole,       //!< ключ секции (file + '\x1f' + scene) для toggleScene
        SectionTitleRole,     //!< quest_id для отображения
        SectionLineCountRole, //!< число реплик в сцене
        ExpandedRole,         //!< секция раскрыта (только для заголовка)
    };

    //! Типы строк (RowTypeRole)
    enum RowType { LineRow = 0, SceneHeaderRow = 1 };
    Q_ENUM(RowType)

    QString fileId() const;
    void setFileId(const QString& fileId);

    bool isFilteringActive() const;
    void setFilteringActive(bool active);

    //! Список файлов игры домена (для Select над списком).
    Q_INVOKABLE QVariantList fileIds() const;

    //! Раскрыть/свернуть сцену по ключу секции (SectionKeyRole).
    Q_INVOKABLE void toggleScene(const QString& sectionKey);

    //! Раскрыть/свернуть все сцены выбранного файла.
    Q_INVOKABLE void setAllScenesExpanded(bool expanded);

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

signals:
    void fileIdChanged();
    void filteringActiveChanged();
    void reloaded(); //!< QML обновляет Select файлов после перестройки

private:
    void buildFromSnapshot(const DubbingMeta& meta);
    void connectNotifications();
    void appendSceneLines(const GameFile& file, const Scene& scene);

    struct Row {
        RowType type = LineRow;
        // реплика
        QString guid;
        QString fileId;
        QString questId;
        QString speaker;
        QString en;
        QString ru;
        QString searchBlob; //!< guid+file+scene+speaker+en+ru в нижнем регистре
        double dur = 0.0;
        double actualDur = -1.0;
        LineStatus status = LineStatus::New;
        int64_t refTrackId = NO_TRACK_ID;
        int64_t refClipId = NO_CLIP_ID;
        int orderIndex = 0;
        // заголовок сцены
        QString sectionKey;
        QString sectionTitle;
        int sectionLineCount = 0;
        bool expanded = false;
    };

    std::vector<Row> m_rows;
    std::vector<QString> m_fileIds;             //!< файлы домена в порядке JSON
    QString m_fileId;                           //!< выбранный файл (пусто = первый)
    std::map<std::string, bool> m_expandedByKey;//!< состояние секций (переживает reload)
    bool m_filteringActive = false;             //!< режим поиска: плоский список без секций
    bool m_connected = false;
};
}
