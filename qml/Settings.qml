import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    width: 480
    height: 400
    color: "#020806"
    border.color: "#00cc88"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 28
            color: "#07110e"
            border.color: "#00cc88"
            border.width: 1

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 30
                property point startPos
                onPressed: startPos = Qt.point(mouseX, mouseY)
                onPositionChanged: if (pressed && settingsCtrl) settingsCtrl.cmdDrag(mouseX - startPos.x, mouseY - startPos.y)
            }

            Text {
                anchors.left: parent.left; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("系统设置")
                color: "#00cc88"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"
            }

            Rectangle {
                anchors.right: parent.right
                width: 30; height: 28
                color: closeMA.containsMouse ? "#5a0a0a" : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: "#00cc88"; font.pixelSize: 12 }
                MouseArea { id: closeMA; anchors.fill: parent; hoverEnabled: true; onClicked: if (settingsCtrl) settingsCtrl.cmdClose() }
            }
        }

        // 内容区
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 12

            // 截图路径
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("截图保存路径"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0a1a12"; border.color: "#00cc88"; border.width: 1; radius: 2
                    TextInput {
                        anchors.fill: parent; anchors.margins: 4
                        text: settingsCtrl ? settingsCtrl.capturePath : ""
                        color: "#00ff99"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                        onTextChanged: if (settingsCtrl) settingsCtrl.capturePath = text
                    }
                }
                Rectangle {
                    width: 26; height: 26; color: bma1.containsMouse ? "#0d2a1e" : "transparent"
                    border.color: "#00cc88"; border.width: 1; radius: 2
                    Text { anchors.centerIn: parent; text: "…"; color: "#00cc88"; font.pixelSize: 14 }
                    MouseArea { id: bma1; anchors.fill: parent; hoverEnabled: true; onClicked: if (settingsCtrl) settingsCtrl.browseCaptureDir() }
                }
            }

            // 录像路径
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("录像保存路径"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0a1a12"; border.color: "#00cc88"; border.width: 1; radius: 2
                    TextInput {
                        anchors.fill: parent; anchors.margins: 4
                        text: settingsCtrl ? settingsCtrl.recordPath : ""
                        color: "#00ff99"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                        onTextChanged: if (settingsCtrl) settingsCtrl.recordPath = text
                    }
                }
                Rectangle {
                    width: 26; height: 26; color: bma2.containsMouse ? "#0d2a1e" : "transparent"
                    border.color: "#00cc88"; border.width: 1; radius: 2
                    Text { anchors.centerIn: parent; text: "…"; color: "#00cc88"; font.pixelSize: 14 }
                    MouseArea { id: bma2; anchors.fill: parent; hoverEnabled: true; onClicked: if (settingsCtrl) settingsCtrl.browseRecordDir() }
                }
            }

            // 截图格式
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("截图格式"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Repeater {
                    model: ["PNG", "JPG", "BMP"]
                    delegate: Rectangle {
                        width: 60; height: 26; radius: 2
                        color: (settingsCtrl && settingsCtrl.captureType === index) ? "#0d2a1e" : "transparent"
                        border.color: (settingsCtrl && settingsCtrl.captureType === index) ? "#00ff99" : "#00cc88"
                        border.width: 1
                        Text { anchors.centerIn: parent; text: modelData; color: (settingsCtrl && settingsCtrl.captureType === index) ? "#00ff99" : "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
                        MouseArea { anchors.fill: parent; onClicked: if (settingsCtrl) settingsCtrl.captureType = index }
                    }
                }
            }

            // 录像格式
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("录像格式"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Repeater {
                    model: ["MP4", "AVI"]
                    delegate: Rectangle {
                        width: 60; height: 26; radius: 2
                        color: (settingsCtrl && settingsCtrl.recordType === index) ? "#0d2a1e" : "transparent"
                        border.color: (settingsCtrl && settingsCtrl.recordType === index) ? "#00ff99" : "#00cc88"
                        border.width: 1
                        Text { anchors.centerIn: parent; text: modelData; color: (settingsCtrl && settingsCtrl.recordType === index) ? "#00ff99" : "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
                        MouseArea { anchors.fill: parent; onClicked: if (settingsCtrl) settingsCtrl.recordType = index }
                    }
                }
            }

            // 叠加信息
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("录像叠加信息"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Rectangle {
                    width: 26; height: 26; radius: 2
                    color: (settingsCtrl && settingsCtrl.overlayEnabled) ? "#0d2a1e" : "transparent"
                    border.color: (settingsCtrl && settingsCtrl.overlayEnabled) ? "#00ff99" : "#00cc88"
                    border.width: 1
                    Text { anchors.centerIn: parent; text: "✓"; color: "#00ff99"; font.pixelSize: 14; visible: settingsCtrl && settingsCtrl.overlayEnabled }
                    MouseArea { anchors.fill: parent; onClicked: if (settingsCtrl) settingsCtrl.overlayEnabled = !settingsCtrl.overlayEnabled }
                }
                Text { text: qsTr("录像/截图时保存叠加时间信息"); color: "#9aa0a6"; font.pixelSize: 11; font.family: "Microsoft YaHei UI" }
            }

            // 语言切换
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("界面语言"); color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; width: 100 }
                Repeater {
                    model: [
                        { label: "中文",    locale: "zh_CN" },
                        { label: "English", locale: "en_US" },
                        { label: "한국어",  locale: "ko_KR" }
                    ]
                    delegate: Rectangle {
                        width: 72; height: 26; radius: 2
                        color: (settingsCtrl && settingsCtrl.language === modelData.locale) ? "#0d2a1e" : "transparent"
                        border.color: (settingsCtrl && settingsCtrl.language === modelData.locale) ? "#00ff99" : "#00cc88"
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: (settingsCtrl && settingsCtrl.language === modelData.locale) ? "#00ff99" : "#9aa0a6"
                            font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: if (settingsCtrl) settingsCtrl.setLanguage(modelData.locale)
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }

            // 确定 / 取消
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [
                        { t: qsTr("确定"), act: "ok"     },
                        { t: qsTr("取消"), act: "cancel" }
                    ]
                    delegate: Rectangle {
                        width: 72; height: 28; radius: 2
                        color: bma.containsMouse ? "#0d2a1e" : "transparent"
                        border.color: bma.containsMouse ? "#00ff99" : "#00cc88"
                        border.width: 1
                        Text { anchors.centerIn: parent; text: modelData.t; color: bma.containsMouse ? "#00ff99" : "#00cc88"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
                        MouseArea {
                            id: bma; anchors.fill: parent; hoverEnabled: true
                            onClicked: {
                                if (!settingsCtrl) return
                                if (modelData.act === "ok") { settingsCtrl.save(); settingsCtrl.cmdClose() }
                                else settingsCtrl.cmdClose()
                            }
                        }
                    }
                }
            }
        }
    }
}
