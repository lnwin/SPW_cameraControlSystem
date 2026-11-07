import QtQuick 2.15
import QtQuick.Layouts 1.15

// 通用消息弹窗（匹配系统设置窗口样式）
Rectangle {
    id: root
    width: 400
    height: Math.min(240, contentColumn.implicitHeight + 56)
    color: "#020806"
    border.color: "#00cc88"
    border.width: 1

    property string title: ""
    property string message: ""
    property bool showCancel: false  // 是否显示取消按钮

    signal accepted()
    signal rejected()

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

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: root.title
                color: "#00cc88"
                font.pixelSize: 12
                font.family: "Microsoft YaHei UI"
            }
        }

        // 内容区
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            spacing: 20

            // 消息文本
            Text {
                id: msgText
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: root.message
                color: "#9aa0a6"
                font.pixelSize: 12
                font.family: "Microsoft YaHei UI"
                wrapMode: Text.WordWrap
                verticalAlignment: Text.AlignVCenter
            }

            // 按钮行
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                Item { Layout.fillWidth: true }

                Repeater {
                    model: root.showCancel
                        ? [{ t: qsTr("确定"), act: "ok" }, { t: qsTr("取消"), act: "cancel" }]
                        : [{ t: qsTr("确定"), act: "ok" }]

                    delegate: Rectangle {
                        width: 72
                        height: 28
                        radius: 2
                        color: bma.containsMouse ? "#0d2a1e" : "transparent"
                        border.color: bma.containsMouse ? "#00ff99" : "#00cc88"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: modelData.t
                            color: bma.containsMouse ? "#00ff99" : "#00cc88"
                            font.pixelSize: 12
                            font.family: "Microsoft YaHei UI"
                        }

                        MouseArea {
                            id: bma
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (modelData.act === "ok") {
                                    root.accepted()
                                } else {
                                    root.rejected()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
