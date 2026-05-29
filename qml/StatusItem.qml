import QtQuick 2.15

Row {
    property string label: ""
    property string value: ""
    property int    labelWidth: 60
    spacing: 8

    Text {
        text: label
        width: labelWidth
        color: "#9aa0a6"
        font.pixelSize: 12
        font.family: "Microsoft YaHei UI"
        elide: Text.ElideRight
    }
    Text {
        text: value
        color: "#00ff99"
        font.pixelSize: 12
        font.family: "Microsoft YaHei UI"
        elide: Text.ElideRight
    }
}
