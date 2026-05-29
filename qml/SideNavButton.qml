import QtQuick 2.15

Rectangle {
    id: root
    property string label: ""
    property bool   active: false
    signal clicked()

    height: 44
    color: active ? "#0d2a1e" : "transparent"
    border.color: active ? "#00ff99" : "transparent"
    border.width: 1
    radius: 2

    Text {
        anchors.centerIn: parent
        text: root.label
        color: root.active ? "#00ff99" : "#00cc88"
        font.pixelSize: 13
        font.family: "Microsoft YaHei UI"
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: root.clicked()
        onEntered: if (!root.active) root.color = "#081a10"
        onExited:  if (!root.active) root.color = "transparent"
    }
}
