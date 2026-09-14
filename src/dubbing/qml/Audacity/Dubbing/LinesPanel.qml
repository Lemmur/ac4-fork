/*
* Audacity: A Digital Audio Editor
*
* Панель списка реплик (M3, §6.3): тулбар фильтров (поиск, UNKNOWN,
* статус, расхождение, без референса) + виртуализованный список
* (ListView создаёт делегаты только для видимых строк — десятки тысяч
* реплик без лагов) + рабочая зона реплики (EN / RU с правкой через
* undo-штатный LineworkspaceController::setRuText).
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

    readonly property int rowHeight: 36
    readonly property int colStatusWidth: 106
    readonly property int colSpeakerWidth: 88
    readonly property int colDurWidth: 62
    readonly property int colMismatchWidth: 62

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

        //! ---------- Тулбар фильтров ----------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            SearchField {
                id: searchField
                Layout.fillWidth: true

                hint: "Поиск (текст, спикер, guid, сцена)"
                hintIcon: IconCode.SEARCH

                onSearchTextChanged: {
                    filterModel.searchText = searchText
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
                    }
                }

                CheckBox {
                    id: unknownBox
                    text: "UNKNOWN"

                    onClicked: {
                        filterModel.onlyUnknown = checked
                    }
                }

                CheckBox {
                    id: mismatchBox
                    text: "Расхождение"

                    onClicked: {
                        filterModel.onlyMismatch = checked
                    }
                }

                CheckBox {
                    id: noRefBox
                    text: "Без референса"

                    onClicked: {
                        filterModel.onlyNoReference = checked
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                StyledTextLabel {
                    text: listView.count + " / " + linesModel.rowCount
                }
            }
        }

        //! ---------- Заголовок колонок ----------
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: root.colStatusWidth
                text: "Статус"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            StyledTextLabel {
                Layout.preferredWidth: root.colSpeakerWidth
                text: "Спикер"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: "EN"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            StyledTextLabel {
                Layout.fillWidth: true
                text: "RU"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignLeft
            }

            StyledTextLabel {
                Layout.preferredWidth: root.colDurWidth
                text: "Длит., с"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignRight
            }

            StyledTextLabel {
                Layout.preferredWidth: root.colMismatchWidth
                text: "Расх., с"
                font: ui.theme.bodyBoldFont
                horizontalAlignment: Text.AlignRight
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

            delegate: ListItemBlank {
                id: lineItem

                width: ListView.view ? ListView.view.width : 0
                height: root.rowHeight

                isSelected: model.guid === root.currentGuid

                onDoubleClicked: function(mouse) {
                    //! двойной клик: выделение референс-клипа + позиция
                    //! воспроизведения + открытие текста в рабочей зоне
                    root.currentGuid = model.guid
                    controller.openLine(model.guid)
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    //! Статус: цветной маркер + текст
                    RowLayout {
                        Layout.preferredWidth: root.colStatusWidth

                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4

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
                            Layout.fillWidth: true

                            text: model.statusText
                            horizontalAlignment: Text.AlignLeft
                        }
                    }

                    StyledTextLabel {
                        Layout.preferredWidth: root.colSpeakerWidth

                        text: model.speaker
                        horizontalAlignment: Text.AlignLeft
                        opacity: model.speaker === "UNKNOWN" ? 0.6 : 1.0
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true

                        text: model.en
                        horizontalAlignment: Text.AlignLeft
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true

                        text: model.ru
                        horizontalAlignment: Text.AlignLeft
                        font: lineItem.isSelected ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                    }

                    StyledTextLabel {
                        Layout.preferredWidth: root.colDurWidth

                        text: model.dur.toFixed(2)
                        horizontalAlignment: Text.AlignRight
                    }

                    StyledTextLabel {
                        Layout.preferredWidth: root.colMismatchWidth

                        text: model.hasMismatch ? (model.mismatch > 0 ? "+" : "") + model.mismatch.toFixed(2) : "—"
                        horizontalAlignment: Text.AlignRight
                        color: model.hasMismatch ? ui.theme.accentColor : ui.theme.fontSecondaryColor
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
