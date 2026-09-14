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

    readonly property int lineRowHeight: 46
    readonly property int headerRowHeight: 28
    readonly property int colSpeakerWidth: 80
    readonly property int colDurWidth: 56
    readonly property int listRightMargin: 18 //!< запас справа: вертикальный скроллбар

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
                        fileSelect.currentIndex = index //!< StyledDropdown не запоминает выбор сам
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

            //! Ряд 1: селект статуса + компактные кнопки секций
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
                        statusDropdown.currentIndex = index //!< StyledDropdown не запоминает выбор сам
                        filterModel.statusFilter = value
                        syncFilteringMode()
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                FlatButton {
                    visible: !filteringActive()
                    height: 24
                    text: "Свернуть"

                    onClicked: {
                        linesModel.setAllScenesExpanded(false)
                    }
                }

                FlatButton {
                    visible: !filteringActive()
                    height: 24
                    text: "Развернуть"

                    onClicked: {
                        linesModel.setAllScenesExpanded(true)
                    }
                }
            }

            //! Ряд 2: чекбоксы фильтров (Flow — автоперенос в узкой панели)
            Flow {
                Layout.fillWidth: true
                spacing: 8

                CheckBox {
                    id: unknownBox
                    text: "UNKNOWN"
                    scale: 0.85

                    //! muse CheckBox не переключает checked сам — вручную
                    onClicked: {
                        unknownBox.checked = !unknownBox.checked
                        filterModel.onlyUnknown = unknownBox.checked
                        syncFilteringMode()
                    }
                }

                CheckBox {
                    id: mismatchBox
                    text: "Расхождение"
                    scale: 0.85

                    onClicked: {
                        mismatchBox.checked = !mismatchBox.checked
                        filterModel.onlyMismatch = mismatchBox.checked
                        syncFilteringMode()
                    }
                }

                CheckBox {
                    id: noRefBox
                    text: "Без референса"
                    scale: 0.85

                    onClicked: {
                        noRefBox.checked = !noRefBox.checked
                        filterModel.onlyNoReference = noRefBox.checked
                        syncFilteringMode()
                    }
                }
            }
        }

        //! ---------- Список реплик (виртуализация ListView) ----------
        StyledListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 4

            clip: true //!< контент не должен рисоваться за границами панели

            model: filterModel

            scrollBarPolicy: ScrollBar.AlwaysOn

            //! ЕДИНЫЙ делегат (без Loader/Component: их creation-контекст
            //! вне делегата не содержит ролей — model.* оказался бы undefined)
            delegate: ListItemBlank {
                id: rowItem

                readonly property bool isHeader: model.rowType === LinesListModel.SceneHeaderRow

                width: ListView.view ? ListView.view.width : 0
                height: isHeader ? root.headerRowHeight : root.lineRowHeight

                isSelected: !isHeader && model.guid === root.currentGuid

                //! Фон заголовка — чуть темнее фона панели (выделение секций)
                Rectangle {
                    anchors.fill: parent
                    visible: rowItem.isHeader

                    color: ui.theme.backgroundSecondaryColor
                }

                //! ----- Заголовок сцены (quest_id из JSON) -----
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: root.listRightMargin
                    spacing: 8
                    visible: rowItem.isHeader

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
                    visible: rowItem.isHeader

                    color: ui.theme.strokeColor
                }

                //! ----- Строка реплики: статус · спикер · EN над RU · длительность -----
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: root.listRightMargin
                    spacing: 8
                    visible: !rowItem.isHeader

                    //! Статус: цветной маркер (текст — в рабочей зоне)
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
                            font: rowItem.isSelected ? ui.theme.bodyBoldFont : ui.theme.bodyFont
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

                onClicked: function(mouse) {
                    if (rowItem.isHeader) {
                        linesModel.toggleScene(model.sectionKey)
                    }
                }

                onDoubleClicked: function(mouse) {
                    if (!rowItem.isHeader) {
                        //! двойной клик: выделение референс-клипа + позиция
                        //! воспроизведения + открытие текста в рабочей зоне
                        root.currentGuid = model.guid
                        controller.openLine(model.guid)
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
