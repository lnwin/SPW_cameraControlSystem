import QtQuick 2.15

Rectangle {
    color: "#020806"
    border.color: "#00cc88"
    border.width: 1
    height: 44

    Row {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 0

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("SPW 工业相机控制系统")
            color: "#00ff99"
            font.pixelSize: 15
            font.bold: true
            font.family: "Microsoft YaHei UI"
            width: 260
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 24

            Text {
                text: qsTr("设备：") + (uiCtrl ? uiCtrl.deviceName : "--")
                color: "#b0b0b0"
                font.pixelSize: 12
                font.family: "Microsoft YaHei UI"
            }
            Text {
                text: "IP：" + (uiCtrl ? uiCtrl.deviceIp : "--")
                color: "#b0b0b0"
                font.pixelSize: 12
                font.family: "Microsoft YaHei UI"
            }
            Row {
                spacing: 6
                Rectangle {
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: (uiCtrl && uiCtrl.rtspConnected) ? "#00ff99" : "#ff3040"
                }
                Text {
                    text: (uiCtrl && uiCtrl.rtspConnected) ? qsTr("RTSP 已连接") : qsTr("RTSP 未连接")
                    color: (uiCtrl && uiCtrl.rtspConnected) ? "#00ff99" : "#ff3040"
                    font.pixelSize: 12
                    font.family: "Microsoft YaHei UI"
                }
            }
            Row {
                spacing: 6
                visible: uiCtrl && uiCtrl.recording
                Rectangle {
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#ff3040"
                    SequentialAnimation on opacity {
                        running: uiCtrl && uiCtrl.recording
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    text: qsTr("录像中")
                    color: "#ff3040"
                    font.pixelSize: 12
                    font.family: "Microsoft YaHei UI"
                }
            }
        }

        Item { width: 1 }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: uiCtrl ? uiCtrl.currentTime : ""
            color: "#00cc88"
            font.pixelSize: 12
            font.family: "Microsoft YaHei UI"
        }
    }
}
