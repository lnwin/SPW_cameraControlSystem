import QtQuick 2.15

HudPanel {
    width: parent ? parent.width : 200

    Column {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        Text {
            text: qsTr("录像状态")
            color: "#00cc88"
            font.pixelSize: 12
            font.bold: true
            font.family: "Microsoft YaHei UI"
        }
        Rectangle { width: parent.width; height: 1; color: "#00cc88"; opacity: 0.4 }

        StatusItem { label: qsTr("状态");     value: (uiCtrl && uiCtrl.recording) ? qsTr("录像中") : qsTr("已停止") }
        StatusItem { label: qsTr("当前文件"); value: uiCtrl ? uiCtrl.recordFileName : "" }
        StatusItem { label: qsTr("分段编号"); value: uiCtrl ? String(uiCtrl.recordSegmentIndex) : "0" }
        StatusItem { label: qsTr("分段时长"); value: uiCtrl ? uiCtrl.recordSegmentElapsed : "00:00" }
        StatusItem { label: qsTr("总时长");   value: uiCtrl ? uiCtrl.recordTotalElapsed : "00:00" }
    }
}
