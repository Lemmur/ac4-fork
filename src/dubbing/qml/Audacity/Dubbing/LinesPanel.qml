/*
* Audacity: A Digital Audio Editor
*
* Панель списка реплик (M3, §6.3, редизайн по требованию владельца):
* - Select файла игры (заголовок первого уровня) НАД списком;
* - сцены (quest_id) — раскрывающиеся заголовки;
* - строка реплики: статус · спикер · EN над RU · длительность в конце;
* - фильтры UNKNOWN/статус/расхождение/без референса + полнотекстовый
*   поиск (при активных фильтрах список — плоские результаты без
*   заголовков); рабочая зона реплики с правкой RU через undo-штатный
*   LineworkspaceController::setRuText.
* Виртуализация: ListView создаёт делегаты только видимых строк.
* Весь пользовательский текст — на русском (AGENTS.md §5).
*/
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents

import Audacity.Dubbing

Item {
    id: root

    property string currentGuid: "" //!< реплика, открытая в рабочей зоне

    readonly property int lineRowHeight: 52
    readonly property int headerRowHeight: 32
    readonly property int colStatusWidth: 26
    readonly property int colSpeakerWidth: 92
    readonly property int colDurWidth: 62

    //! Активен ли хоть один фильтр/поиск: в этом режиме модель даёт плоский
    //! список всех реплик файла (включая свёрнутые секции), без заголовков.
    function filteringActive() {
        return unknownBox.checked || statusDropdown.currentIndex > 0
               || mismatchBox.checked || noRefBox.checked || Boolean(searchField.searchText)
    }

    function syncFilteringMode() {
        linesModel.filteringActive = filteringActive()
    }

    LinesListModel {
        id: linesModel
    }

    LinesFilterModel {
        id: filterModel
        sourceModel: linesModel
    }

    LineworkspaceController {
        id: controller
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        //! ---------- Тулбар: файл + поиск ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            //! Заголовок первого уровня (файл игры) — Select над списком
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledTextLabel {
                    text: "Файл:"
                }

                StyledDropdown {
                    id: fileSelect
                    Layout.fillWidth: true

                    property var items: []

                    model: items
                    currentIndex: items.length > 0 ? 0 : -1

                    onActivated: function(index, value) {
                        if (value !== undefined && value !== null) {
                            linesModel.fileId = value
                        }
                    }

                    Connections {
                        target: linesModel

                        function onReloaded() {
                            //! файлы из домена; текущий — выбранным элементом
                            var ids = linesModel.fileIds()
                            var items = []
                            for (var i = 0; i < ids.length; ++i) {
                                items.push({ text: ids[i], value: ids[i] })
                            }
                            fileSelect.items = items
                            fileSelect.currentIndex = items.length > 0
                                                   ? fileSelect.indexOfValue(linesModel.fileId) : -1
                        }
                    }
                }
            }

            SearchField {
                id: searchField
                Layout.fillWidth: true

                hint: "Поиск (текст, спикер, guid, сцена)"
                hintIcon: IconCode.SEARCH

                onSearchTextChanged: {
                    filterModel.searchText = searchText
                    syncFilteringMode()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledDropdown {
                    id: statusDropdown
                    Layout.preferredWidth: 136

                    //! value: -1 = все статусы (dubbingtypes.h LineStatus)
                    model: [
                        { text: "Все статусы", value: -1 },
                        { text: "Новая", value: 0 },
                        { text: "Нет референса", value: 1 },
                        { text: "В работе", value: 2 },
                        { text: "Готова", value: 3 },
                        { text: "Экспортирована", value: 4 }
                    ]
                    currentIndex: 0

                    onActivated: function(index, value) {
                        filterModel.statusFilter = value
                        syncFilteringMode()
                    }
                }

                CheckBox {
                    id: unknownBox
                    text: "UNKNOWN"

                    onClicked: {
                        filterModel.onlyUnknown = checked
                        syncFilteringMode()
                    }
                }

                CheckBox {
                    id: mismatchBox
                    text: "Расхождение"

                    onClicked: {
                        filterModel.onlyMismatch = checked
                        syncFilteringMode()
                    }
                }

                CheckBox {
                    id: noRefBox
                    text: "Без референса"

                    onClicked: {
                        filterModel.onlyNoReference = checked
                        syncFilteringMode()
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                FlatButton {
                    visible: !filteringActive()
                    text: "Свернуть всё"

                    onClicked: {
                        linesModel.setAllScenesExpanded(false)
                    }
                }

                FlatButton {
                    visible: !filteringActive()
                    text: "Развернуть всё"

                    onClicked: {
                        linesModel.setAllScenesExpanded(true)
                    }
                }

                StyledTextLabel {
                    text: listView.count + " / " + linesModel.rowCount
                }
            }
        }

        //! ---------- Список реплик (виртуализация ListView) ----------
        StyledListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4

            model: filterModel

            scrollBarPolicy: ScrollBar.AlwaysOn

            delegate: Loader {
                width: ListView.view ? ListView.view.width : 0
                height: model.rowType === LinesListModel.SceneHeaderRow ? root.headerRowHeight : root.lineRowHeight

                sourceComponent: model.rowType === LinesListModel.SceneHeaderRow
                                 ? sceneHeaderComponent : lineRowComponent
            }

            Component {
                id: sceneHeaderComponent

                //! Раскрывающийся заголовок сцены (quest_id из JSON)
                ListItemBlank {
                    id: sceneHeader

                    mouseArea.hoverEnabled: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 10
                        spacing: 8

                        //! Стрелка раскрытия
                        StyledIconLabel {
                            Layout.preferredWidth: 14

                            iconCode: IconCode.ARROW_RIGHT
                            rotation: model.expanded ? 90 : 0

                            Behavior on rotation {
                                NumberAnimation { duration: 120 }
                            }
                        }

                        StyledTextLabel {
                            Layout.fillWidth: true

                            text: model.sectionTitle
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }

                        StyledTextLabel {
                            text: model.sectionLineCount + " реп."
                            opacity: 0.6
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1

                        color: ui.theme.strokeColor
                    }

                    onClicked: {
                        linesModel.toggleScene(model.sectionKey)
                    }
                }
            }

            Component {
                id: lineRowComponent

                //! Строка реплики: статус · спикер · EN над RU · длительность
                ListItemBlank {
                    id: lineItem

                    isSelected: model.guid === root.currentGuid

                    onDoubleClicked: function(mouse) {
                        //! двойной клик: выделение референс-клипа + позиция
                        //! воспроизведения + открытие текста в рабочей зоне
                        root.currentGuid = model.guid
                        controller.openLine(model.guid)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 20
                        anchors.rightMargin: 10
                        spacing: 8

                        //! Статус: цветной маркер (текст — в подсказке/рабочей зоне)
                        Rectangle {
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            Layout.alignment: Qt.AlignVCenter

                            color: {
                                switch (model.statusCode) {
                                case 1: return ui.theme.strokeColor      // нет референса
                                case 2: return ui.theme.linkColor         // в работе
                                case 3: return ui.theme.accentColor       // готова
                                case 4: return ui.theme.accentColor       // экспортирована
                                default: return ui.theme.buttonColor      // новая
                                }
                            }
                        }

                        StyledTextLabel {
                            Layout.preferredWidth: root.colSpeakerWidth
                            Layout.alignment: Qt.AlignVCenter

                            text: model.speaker
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            opacity: model.speaker === "UNKNOWN" ? 0.6 : 1.0
                        }

                        //! EN над RU (друг над другом)
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 1

                            StyledTextLabel {
                                Layout.fillWidth: true

                                text: model.en
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                opacity: 0.65
                                font: ui.theme.bodyFont
                            }

                            StyledTextLabel {
                                Layout.fillWidth: true

                                text: model.ru
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                font: lineItem.isSelected ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                            }
                        }

                        //! Время дорожки в конце; расхождение — подсветка
                        StyledTextLabel {
                            Layout.preferredWidth: root.colDurWidth
                            Layout.alignment: Qt.AlignVCenter

                            text: model.dur.toFixed(2) + " с"
                            horizontalAlignment: Text.AlignRight
                            color: model.hasMismatch ? ui.theme.accentColor : ui.theme.fontPrimaryColor
                        }
                    }
                }
            }
        }

        //! ---------- Рабочая зона реплики ----------
        Rectangle {
            id: workZone

            visible: root.currentGuid !== ""
            enabled: visible

            Layout.fillWidth: true
            Layout.preferredHeight: 136

            color: ui.theme.backgroundSecondaryColor
            border.width: 1
            border.color: ui.theme.strokeColor

            property var info: controller.lineInfo(root.currentGuid)

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    StyledTextLabel {
                        font: ui.theme.bodyBoldFont
                        text: (workZone.info.speaker || "") + " · " + (workZone.info.statusText || "")
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    FlatButton {
                        text: "Закрыть"

                        onClicked: {
                            root.currentGuid = ""
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    text: workZone.info.en || ""
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.WordWrap
                    font: ui.theme.largeBodyFont
                }

                TextInputField {
                    id: ruEdit

                    Layout.fillWidth: true

                    property string guidWhenFocused: root.currentGuid

                    hint: "RU-текст (Enter или потеря фокуса — применить, Ctrl+Z — отменить)"
                    currentText: workZone.info.ru || ""

                    onTextEditingFinished: function(newTextValue) {
                        //! Правка RU: ТОЛЬКО через IDubbingProject::setLineRu ->
                        //! pushHistoryState(«Правка текста реплики») — параллельных
                        //! механизмов отмены нет (AGENTS.md §5).
                        controller.setRuText(guidWhenFocused, newTextValue)
                    }
                }
            }
        }
    }
}
