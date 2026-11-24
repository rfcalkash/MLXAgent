import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Controls.Material

Page {
    id: settingsPage

    // Входящий контракт для вкладки "Сервер"
    required property string serverUrl
    required property string serverStatus
    required property string activeModel
    required property bool textOnlyMode
    required property bool isBusy
    required property var models
    required property int activeModelIndex

    // Входящий контракт для вкладки "Инструменты"
    required property var toolCtrl

    // Исходящие сигналы
    signal backClicked()
    signal connectClicked(string url)
    signal loadModelClicked(string modelId, bool textOnly)
    signal unloadModelClicked()
    signal downloadModelClicked()
    signal deleteModelClicked(string modelId)

    onServerUrlChanged: urlField.text = serverUrl
    onActiveModelIndexChanged: modelSelector.currentIndex = activeModelIndex
    Component.onCompleted: {
        urlField.text = serverUrl
        modelSelector.currentIndex = activeModelIndex
    }

    FolderDialog {
        id: workspacePicker
        title: "Выберите рабочую директорию (песочницу)"
        onAccepted: settingsPage.toolCtrl.setWorkspaceUrl(selectedFolder)
    }

    FileDialog {
        id: cmakePicker
        title: "Укажите исполняемый файл CMake"
        onAccepted: settingsPage.toolCtrl.setCmakeUrl(selectedFile)
    }

    FileDialog {
        id: qmllintPicker
        title: "Укажите исполняемый файл qmllint"
        onAccepted: settingsPage.toolCtrl.setQmllintUrl(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Собственная шапка страницы настроек
        ToolBar {
            Layout.fillWidth: true
            Material.background: settingsPage.Material.background

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 12

                RoundButton {
                    text: "←"
                    flat: true
                    font.pixelSize: 18
                    onClicked: settingsPage.backClicked()
                }
                Label {
                    text: "Настройки"
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                }
                Label {
                    text: settingsPage.serverStatus + (settingsPage.textOnlyMode && settingsPage.serverStatus === "LOADED" ? " [TXT]" : "")
                    font.bold: true
                    color: settingsPage.serverStatus === "LOADED" ? Material.color(Material.Green) :
                                                                    (settingsPage.serverStatus === "OFFLINE" ? Material.color(Material.Red) : Material.color(Material.Orange))
                }
            }
        }

        // Переключатель вкладок
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            currentIndex: swipeView.currentIndex

            TabButton { text: "Сервер и модели" }
            TabButton { text: "Инструменты агента" }
        }

        SwipeView {
            id: swipeView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Material.frameVerticalPadding
            clip: true
            currentIndex: tabBar.currentIndex

            // --- ВКЛАДКА 1: СЕРВЕР И МОДЕЛИ ---
            ScrollView {
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: parent.width
                    anchors.margins: 16
                    spacing: 14

                    Item { height: 4 }

                    Label { text: "Подключение к серверу MLX"; font.bold: true }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        TextField {
                            id: urlField
                            Layout.fillWidth: true
                            placeholderText: "http://192.168.1.X:8000"
                        }
                        Button {
                            text: "Подключить"
                            onClicked: settingsPage.connectClicked(urlField.text)
                        }
                    }

                    Label {
                        text: "Активная модель: " + (settingsPage.activeModel || "не загружена")
                        Layout.leftMargin: 12
                        opacity: 0.8
                    }

                    MenuSeparator { Layout.fillWidth: true }

                    Label { text: "Управление моделями"; font.bold: true }

                    ComboBox {
                        id: modelSelector
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        model: settingsPage.models
                        textRole: "id"
                        enabled: !settingsPage.isBusy && settingsPage.serverStatus !== "LOADED"
                    }

                    CheckBox {
                        id: textOnlyCheck
                        Layout.leftMargin: 12
                        text: "Режим Text-only (отключить Vision Encoder)"
                        checked: false
                        enabled: !settingsPage.isBusy && settingsPage.serverStatus !== "LOADED"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 8

                        Button {
                            text: settingsPage.serverStatus === "LOADED" ? "Выгрузить" : "Загрузить"
                            highlighted: true
                            Layout.fillWidth: true
                            enabled: !settingsPage.isBusy && (settingsPage.serverStatus === "LOADED" || modelSelector.currentIndex >= 0)
                            onClicked: {
                                if (settingsPage.serverStatus === "LOADED") {
                                    settingsPage.unloadModelClicked()
                                } else {
                                    var selectedId = settingsPage.models[modelSelector.currentIndex].id
                                    settingsPage.loadModelClicked(selectedId, textOnlyCheck.checked)
                                }
                            }
                        }
                        Button {
                            text: "+ Скачать с HF"
                            enabled: !settingsPage.isBusy
                            onClicked: settingsPage.downloadModelClicked()
                        }
                        Button {
                            text: "Удалить"
                            enabled: !settingsPage.isBusy && settingsPage.serverStatus !== "LOADED" && modelSelector.currentIndex >= 0
                            onClicked: {
                                var selectedId = settingsPage.models[modelSelector.currentIndex].id
                                settingsPage.deleteModelClicked(selectedId)
                            }
                        }
                    }
                }
            }

            // --- ВКЛАДКА 2: ИНСТРУМЕНТЫ АГЕНТА ---
            ScrollView {
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: parent.width
                    spacing: 12

                    Item { height: 4 }

                    Switch {
                        Layout.leftMargin: 12
                        text: "Включить режим агента (Tool Calling)"
                        checked: settingsPage.toolCtrl.agentEnabled
                        onToggled: settingsPage.toolCtrl.agentEnabled = checked
                    }

                    Label { text: "Рабочая директория (песочница ФС):"; font.bold: true; Layout.leftMargin: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            text: settingsPage.toolCtrl.workspaceRoot
                            placeholderText: "Не выбрана (инструменты ФС отключены)"
                        }
                        Button {
                            text: "Выбрать..."
                            onClicked: workspacePicker.open()
                        }
                    }

                    Label { text: "Инструменты сборки и линтинга:"; font.bold: true; Layout.leftMargin: 12 }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label { text: settingsPage.toolCtrl.cmakeValid ? "✓ CMake:" : "⚠ CMake:"; width: 85 }
                        TextField {
                            Layout.fillWidth: true
                            text: settingsPage.toolCtrl.cmakePath
                            placeholderText: "Не найден (инструмент отключен)"
                            onEditingFinished: settingsPage.toolCtrl.cmakePath = text
                        }
                        Button { text: "..."; onClicked: cmakePicker.open() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Label { text: settingsPage.toolCtrl.qmllintValid ? "✓ qmllint:" : "⚠ qmllint:"; width: 85 }
                        TextField {
                            Layout.fillWidth: true
                            text: settingsPage.toolCtrl.qmllintPath
                            placeholderText: "Не найден (инструмент отключен)"
                            onEditingFinished: settingsPage.toolCtrl.qmllintPath = text
                        }
                        Button { text: "..."; onClicked: qmllintPicker.open() }
                    }
                    Label { text: "Права доступа инструментов:"; font.bold: true; Layout.leftMargin: 12 }
                    Repeater {
                        model: [
                            { key: "file_create",   label: "Файлы: Создание (Create)" },
                            { key: "file_read",     label: "Файлы: Чтение (Read)" },
                            { key: "file_update",   label: "Файлы: Изменение (Update)" },
                            { key: "file_delete",   label: "Файлы: Удаление (Delete)" },
                            { key: "dir_create",    label: "Папки: Создание (Create)" },
                            { key: "dir_read",      label: "Папки: Просмотр (Read)" },
                            { key: "dir_update",    label: "Папки: Перемещение (Update)" },
                            { key: "dir_delete",    label: "Папки: Удаление (Delete)" },
                            { key: "web_fetch",     label: "Сеть: Загрузка страниц (Fetch)" },
                            { key: "cmake_build",   label: "Сборка: CMake Build" },
                            { key: "qmllint_check", label: "Анализ: qmllint" }
                        ]
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Label {
                                text: modelData.label
                                Layout.fillWidth: true
                            }
                            ComboBox {
                                model: ["allow", "ask", "deny"]
                                currentIndex: model.indexOf(settingsPage.toolCtrl.permissions[modelData.key] || "deny")
                                onActivated: settingsPage.toolCtrl.setPermission(modelData.key, currentText)
                            }
                        }
                    }
                    Item { height: 12 }
                }
            }
        }
    }
}