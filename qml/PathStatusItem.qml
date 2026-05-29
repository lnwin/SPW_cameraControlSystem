import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    property string label: ""
    property string path:  ""
    width: parent ? parent.width : 180
    height: lbl.height + val.height + 4

    Text {
        id: lbl
        text: label
        color: "#9aa0a6"
        font.pixelSize: 12
        font.family: "Microsoft YaHei UI"
        anchors.left: parent.left
        anchors.right: parent.right
    }
    Text {
        id: val
        text: path.length ? path : "--"
        color: "#00ff99"
        font.pixelSize: 11
        font.family: "Microsoft YaHei UI"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: lbl.bottom
        anchors.topMargin: 4
        elide: Text.ElideMiddle
        wrapMode: Text.NoWrap
    }
    MouseArea {
        id: hoverArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: lbl.bottom
        anchors.bottom: parent.bottom
        hoverEnabled: true
    }
    ToolTip {
        visible: hoverArea.containsMouse && path.length > 0
        text: path
        delay: 400
    }
}
