import QtQuick 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#07110e"
    border.color: "#00cc88"
    border.width: 1
    height: 46

    Rectangle {
        id: tooltip
        visible: false
        z: 999
        width: tipText.implicitWidth + 14
        height: 22
        radius: 2
        color: "#0a1a12"
        border.color: "#00cc88"
        border.width: 1
        Text {
            id: tipText
            anchors.centerIn: parent
            color: "#00ff99"
            font.pixelSize: 11
            font.family: "Microsoft YaHei UI"
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 4

        Text {
            text: qsTr("TurbidCamera  浑水相机控制系统")
            color: "#00ff99"
            font.pixelSize: 14
            font.bold: true
            font.family: "Microsoft YaHei UI"
            Layout.rightMargin: 8
        }

        Rectangle { width: 1; height: 24; color: "#00cc88"; opacity: 0.4 }

        Repeater {
            model: [
                { tip: qsTr("系统设置"), cmd: "settings" }
            ]
            delegate: ToolBtn { tip: modelData.tip; cmd: modelData.cmd }
        }

        ToolBtn {
            tip:        (uiCtrl && uiCtrl.connecting) ? qsTr("连接中...") :
                        (uiCtrl && uiCtrl.rtspConnected) ? qsTr("断开连接") : qsTr("连接相机")
            cmd:        (uiCtrl && uiCtrl.rtspConnected) ? "close" : "open"
            active:     uiCtrl && uiCtrl.rtspConnected
            activeColor: "#ff3040"
            enabled:    !(uiCtrl && uiCtrl.connecting)
            opacity:    (uiCtrl && uiCtrl.connecting) ? 0.5 : 1.0
        }

        ToolBtn {
            tip:        (uiCtrl && uiCtrl.recording) ? qsTr("停止录像") : qsTr("开始录像")
            cmd:        (uiCtrl && uiCtrl.recording) ? "recStop"  : "recStart"
            active:     uiCtrl && uiCtrl.recording
            activeColor: "#ff3040"
            enabled:    uiCtrl && uiCtrl.rtspConnected
            opacity:    enabled ? 1.0 : 0.35
        }

        Repeater {
            model: [
                { tip: qsTr("截图"),         cmd: "snapshot" },
                { tip: qsTr("打开保存目录"), cmd: "folder"   }
            ]
            delegate: ToolBtn {
                tip:     modelData.tip
                cmd:     modelData.cmd
                enabled: modelData.cmd === "snapshot" ? (uiCtrl && uiCtrl.rtspConnected) : true
                opacity: enabled ? 1.0 : 0.35
            }
        }

        ToolBtn {
            tip:         (uiCtrl && uiCtrl.crosshairEnabled) ? qsTr("关闭准线") : qsTr("显示准线")
            cmd:         "crosshair"
            active:      uiCtrl && uiCtrl.crosshairEnabled
            activeColor: "#00ff99"
        }

        Rectangle { width: 1; height: 24; color: "#00cc88"; opacity: 0.4 }

        Row {
            spacing: 8
            leftPadding: 8
            visible: uiCtrl && uiCtrl.recording
            Rectangle {
                width: 10; height: 10; radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: "#ff3040"
                SequentialAnimation on opacity {
                    running: uiCtrl && uiCtrl.recording
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 500 }
                    NumberAnimation { to: 1.0; duration: 500 }
                }
            }
            Text { text: "REC"; color: "#ff3040"; font.pixelSize: 13; font.bold: true; font.family: "Microsoft YaHei UI" }
        }

        Row {
            spacing: 8
            leftPadding: 4
            Rectangle {
                width: 10; height: 10; radius: 5
                anchors.verticalCenter: parent.verticalCenter
                color: (uiCtrl && uiCtrl.rtspConnected) ? "#00ff99" : "#808080"
            }
            Text {
                text: (uiCtrl && uiCtrl.rtspConnected) ? "LIVE" : "OFF"
                color: (uiCtrl && uiCtrl.rtspConnected) ? "#00ff99" : "#808080"
                font.pixelSize: 13; font.bold: true; font.family: "Microsoft YaHei UI"
            }
        }

        Item { Layout.fillWidth: true }

        Text {
            text: uiCtrl ? uiCtrl.currentTime : ""
            color: "#00cc88"
            font.pixelSize: 12
            font.family: "Microsoft YaHei UI"
        }
    }

    component ToolBtn: Rectangle {
        property string tip: ""
        property string cmd: ""
        property bool   active: false
        property color  activeColor: "#00ff99"

        width: 34; height: 30
        radius: 2
        color: ma.containsMouse ? "#0d2a1e" : (active ? "#0a2a1a" : "transparent")
        border.color: active ? activeColor : (ma.containsMouse ? "#00ff99" : "#1a4a30")
        border.width: 1

        Canvas {
            anchors.centerIn: parent
            width: 16; height: 16
            property color ic: (active ? activeColor : (ma.containsMouse ? "#00ff99" : "#00cc88"))
            onIcChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = ic; ctx.fillStyle = ic; ctx.lineWidth = 1.5
                var c = cmd
                if (c === "settings") {
                    ctx.beginPath(); ctx.arc(8,8,5,0,Math.PI*2); ctx.stroke()
                    ctx.beginPath(); ctx.arc(8,8,2,0,Math.PI*2); ctx.fill()
                    for (var i=0;i<8;i++){
                        var a=i*Math.PI/4
                        ctx.beginPath()
                        ctx.moveTo(8+5.5*Math.cos(a),8+5.5*Math.sin(a))
                        ctx.lineTo(8+7*Math.cos(a),8+7*Math.sin(a)); ctx.stroke()
                    }
                } else if (c === "open") {
                    ctx.beginPath(); ctx.moveTo(5,2); ctx.lineTo(5,8); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(11,2); ctx.lineTo(11,8); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(3,8); ctx.lineTo(13,8); ctx.lineTo(13,11)
                    ctx.lineTo(8,14); ctx.lineTo(3,11); ctx.lineTo(3,8); ctx.stroke()
                } else if (c === "close") {
                    ctx.lineWidth = 2
                    ctx.beginPath(); ctx.moveTo(3,3); ctx.lineTo(13,13); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(13,3); ctx.lineTo(3,13); ctx.stroke()
                } else if (c === "recStart") {
                    ctx.beginPath(); ctx.arc(8,8,6,0,Math.PI*2); ctx.fill()
                } else if (c === "recStop") {
                    ctx.fillRect(3,3,10,10)
                } else if (c === "snapshot") {
                    ctx.beginPath(); ctx.rect(1,4,14,10); ctx.stroke()
                    ctx.beginPath(); ctx.arc(8,9,3,0,Math.PI*2); ctx.stroke()
                    ctx.fillRect(5,2,6,3)
                } else if (c === "folder") {
                    ctx.beginPath()
                    ctx.moveTo(1,5); ctx.lineTo(1,14); ctx.lineTo(15,14)
                    ctx.lineTo(15,6); ctx.lineTo(7,6); ctx.lineTo(5,4)
                    ctx.lineTo(1,4); ctx.closePath(); ctx.stroke()
                } else if (c === "crosshair") {
                    ctx.beginPath(); ctx.moveTo(8,1); ctx.lineTo(8,15); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(1,8); ctx.lineTo(15,8); ctx.stroke()
                    ctx.beginPath(); ctx.arc(8,8,2.5,0,Math.PI*2); ctx.fill()
                }
            }
        }

        MouseArea {
            id: ma
            anchors.fill: parent
            hoverEnabled: true
            onEntered: {
                tipText.text = tip
                var p = parent.mapToItem(root, parent.width/2, 0)
                tooltip.x = Math.min(Math.max(p.x - tooltip.width/2, 4), root.width - tooltip.width - 4)
                tooltip.y = p.y - tooltip.height - 4
                tooltip.visible = true
            }
            onExited:  tooltip.visible = false
            onClicked: {
                tooltip.visible = false
                if (!uiCtrl) return
                if      (cmd === "open")     uiCtrl.cmdOpenCamera()
                else if (cmd === "close")    uiCtrl.cmdCloseCamera()
                else if (cmd === "recStart") uiCtrl.cmdStartRecord()
                else if (cmd === "recStop")  uiCtrl.cmdStopRecord()
                else if (cmd === "snapshot") uiCtrl.cmdSnapshot()
                else if (cmd === "folder")   uiCtrl.cmdOpenFolder()
                else if (cmd === "settings") uiCtrl.cmdOpenSettings()
                else if (cmd === "crosshair")uiCtrl.cmdToggleCrosshair()
            }
        }
    }
}
