import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    property color borderColor: "#00cc88"
    property color glowColor:   "#00ff99"

    contentItem: Text {
        text: root.text
        color: root.hovered ? glowColor : borderColor
        font.pixelSize: 13
        font.family: "Microsoft YaHei UI"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment:   Text.AlignVCenter
    }

    background: Rectangle {
        color: root.pressed ? "#0a2a1e" : (root.hovered ? "#0d1f17" : "transparent")
        border.color: root.hovered ? glowColor : borderColor
        border.width: 1
        radius: 2
    }
}
