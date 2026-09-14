/*
* Audacity: A Digital Audio Editor
*
* Контроллер рабочей зоны реплики (M3, §6.3): двойной клик по строке
* панели — выделение референс-клипа (ISelectionController), позиция
* воспроизведения в начало клипа (IPlaybackController::setLastPlaybackSeekTime
* — тот же путь, что PlaybackStateModel), правка RU-текста — только через
* IDubbingProject::setLineRu (штатный pushHistoryState, параллельных
* механизмов отмены нет — AGENTS.md §5).
*/
#pragma once

#include <QObject>
#include <QQmlParserStatus>

#include "async/asyncable.h"

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "trackedit/iselectioncontroller.h"
#include "playback/iplaybackcontroller.h"

#include "../idubbingproject.h"

namespace au::dubbing {
class LineworkspaceController : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)

public:
    explicit LineworkspaceController(QObject* parent = nullptr);

    //! Двойной клик: найти реплику по guid, выделить референс-клип,
    //! поставить позицию воспроизведения в его начало. false — реплика
    //! не найдена или референса нет.
    Q_INVOKABLE bool openLine(const QString& guid);

    //! Данные реплики для рабочей зоны (en/ru/speaker/statusText/dur/
    //! hasReference); пустой map — реплика не найдена.
    Q_INVOKABLE QVariantMap lineInfo(const QString& guid) const;

    //! Правка RU-текста из панели: IDubbingProject::setLineRu ->
    //! pushHistoryState(«Правка текста реплики»), Ctrl+Z возвращает текст.
    Q_INVOKABLE bool setRuText(const QString& guid, const QString& text);

    //! Начало референс-клипа, секунды (-1 — референса нет/клип не найден).
    Q_INVOKABLE double referenceStartTime(const QString& guid) const;

    muse::ContextInject<IDubbingProject> dubbingProject = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<trackedit::ISelectionController> selectionController = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };

public: // QQmlParserStatus
    void classBegin() override {}
    void componentComplete() override {}
};
}
