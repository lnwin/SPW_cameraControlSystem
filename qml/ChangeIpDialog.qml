import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    width: 400
    height: 190
    color: "#020806"
    border.color: "#00cc88"
    border.width: 1

    property string sn: ""
    property string currentIp: ""

    signal confirmed(string sn, string newIp)
    signal cancelled()

    // 每次 currentIp 变化时同步输入框
    onCurrentIpChanged: ipInput.text = currentIp

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 标题栏
        Rectangle {
            Layout.fillWidth: true
            height: 30
            color: "#07110e"
            border.color: "#00cc88"
            border.width: 1

            Text {
                anchors.left: parent.left; anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: "修改相机 IP"
                color: "#00cc88"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"
            }
            Rectangle {
                anchors.right: parent.right; width: 32; height: 30
                color: xma.containsMouse ? "#3a0808" : "transparent"
                Text { anchors.centerIn: parent; text: "✕"; color: xma.containsMouse ? "#ff3040" : "#00cc88"; font.pixelSize: 12 }
                MouseArea { id: xma; anchors.fill: parent; hoverEnabled: true; onClicked: root.cancelled() }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16
            spacing: 10

            Text {
                text: "设备：" + root.sn + "　当前 IP：" + root.currentIp
                color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI"
            }

            Rectangle {
                Layout.fillWidth: true; height: 28
                color: "#0a1a12"
                border.color: ipInput.activeFocus ? "#00ff99" : "#00cc88"
                border.width: 1; radius: 2
                TextInput {
                    id: ipInput
                    anchors.fill: parent; anchors.margins: 6
                    text: root.currentIp
                    color: "#00ff99"; font.pixelSize: 13; font.family: "Microsoft YaHei UI"
                    selectByMouse: true
                    onAccepted: root.confirmed(root.sn, ipInput.text)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Repeater {
                    model: [{ t: "确定", act: "ok" }, { t: "取消", act: "cancel" }]
                    delegate: Rectangle {
                        width: 72; height: 28; radius: 2
                        color: bma.containsMouse ? "#0d2a1e" : "transparent"
                        border.color: bma.containsMouse ? "#00ff99" : "#00cc88"; border.width: 1
                        Text {
                            anchors.centerIn: parent; text: modelData.t
                            color: bma.containsMouse ? "#00ff99" : "#00cc88"
                            font.pixelSize: 12; font.family: "Microsoft YaHei UI"
                        }
                        MouseArea {
                            id: bma; anchors.fill: parent; hoverEnabled: true
                            onClicked: {
                                if (modelData.act === "ok") root.confirmed(root.sn, ipInput.text)
                                else root.cancelled()
                            }
                        }
                    }
                }
            }
        }
    }
}
