import QtQuick 2.15
import QtQuick.Layouts 1.15

HudPanel {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: qsTr("设备列表")
            color: "#00cc88"
            font.pixelSize: 12
            font.bold: true
            font.family: "Microsoft YaHei UI"
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.4 }

        Repeater {
            model: uiCtrl ? uiCtrl.deviceList : []
            delegate: Rectangle {
                Layout.fillWidth: true
                height: 38
                radius: 2
                color: (uiCtrl && uiCtrl.selectedSn === modelData) ? "#0d2a1e" : "transparent"
                border.color: (uiCtrl && uiCtrl.selectedSn === modelData) ? "#00ff99" : "#1a3a2a"
                border.width: 1

                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    spacing: 8

                    Rectangle {
                        width: 7; height: 7; radius: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#00ff99"
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        color: (uiCtrl && uiCtrl.selectedSn === modelData) ? "#00ff99" : "#b0b0b0"
                        font.pixelSize: 12
                        font.family: "Microsoft YaHei UI"
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: if (uiCtrl) uiCtrl.cmdSelectDevice(modelData)
                }
            }
        }

        // 空列表提示
        Text {
            visible: !uiCtrl || uiCtrl.deviceList.length === 0
            text: qsTr("未发现设备") + "\n" + qsTr("请检查网络连接")
            color: "#808080"
            font.pixelSize: 11
            font.family: "Microsoft YaHei UI"
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            topPadding: 10
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.2 }

        Text {
            text: qsTr("设备信息")
            color: "#00cc88"
            font.pixelSize: 12
            font.bold: true
            font.family: "Microsoft YaHei UI"
            visible: uiCtrl && uiCtrl.selectedSn !== ""
        }

        StatusItem { label: "SN"; value: uiCtrl ? uiCtrl.selectedSn : ""; visible: uiCtrl && uiCtrl.selectedSn !== "" }
        StatusItem { label: "IP"; value: uiCtrl ? uiCtrl.deviceIp : "--" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("状态"); Layout.preferredWidth: 90; color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; elide: Text.ElideRight }
            Text {
                Layout.fillWidth: true
                text: (uiCtrl && uiCtrl.deviceOnline) ? qsTr("在线") : qsTr("离线")
                color: (uiCtrl && uiCtrl.deviceOnline) ? "#00ff99" : "#ff3040"
                font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                elide: Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text { text: qsTr("网络流"); Layout.preferredWidth: 90; color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"; elide: Text.ElideRight }
            Text {
                Layout.fillWidth: true
                text: (uiCtrl && uiCtrl.rtspConnected) ? qsTr("已连接") : qsTr("未连接")
                color: (uiCtrl && uiCtrl.rtspConnected) ? "#00ff99" : "#ff3040"
                font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                elide: Text.ElideRight
            }
        }

        Item { Layout.fillHeight: true }

        Rectangle {
            Layout.fillWidth: true
            height: 28
            radius: 2
            visible: uiCtrl && uiCtrl.selectedSn !== ""
            color: ipma.containsMouse ? "#0d2a1e" : "transparent"
            border.color: ipma.containsMouse ? "#00ff99" : "#00cc88"
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: qsTr("修改 IP")
                color: ipma.containsMouse ? "#00ff99" : "#00cc88"
                font.pixelSize: 12; font.family: "Microsoft YaHei UI"
            }
            MouseArea {
                id: ipma; anchors.fill: parent; hoverEnabled: true
                onClicked: if (uiCtrl) uiCtrl.cmdChangeIp(uiCtrl.selectedSn)
            }
        }
    }
}
