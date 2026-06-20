import QtQuick 2.15
import QtQuick.Layouts 1.15

RowLayout {
    property string label: ""
    property string value: ""
    property int    labelWidth: 110
    spacing: 8

    Text {
        text: label
        Layout.preferredWidth: labelWidth
        color: "#9aa0a6"
        font.pixelSize: 12
        font.family: "Microsoft YaHei UI"
        elide: Text.ElideRight
    }
    Text {
        text: value
        Layout.fillWidth: true
        color: "#00ff99"
        font.pixelSize: 12
        font.family: "Microsoft YaHei UI"
        elide: Text.ElideRight
    }
}
