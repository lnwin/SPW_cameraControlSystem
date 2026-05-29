import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    width: 1280
    height: 800
    color: "#000000"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 自绘标题栏
        Rectangle {
            id: titleBar
            Layout.fillWidth: true
            height: 32
            color: "#020806"
            border.color: "#00cc88"
            border.width: 1

            MouseArea {
                anchors.fill: parent
                anchors.rightMargin: 96
                property point startPos
                onPressed: startPos = Qt.point(mouseX, mouseY)
                onPositionChanged: if (pressed && uiCtrl) uiCtrl.cmdWinDrag(mouseX - startPos.x, mouseY - startPos.y)
            }

            // Logo + 公司名
            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Image {
                    source: "qrc:/new/prefix1/release/icons/current/Slogo.png"
                    height: 20
                    width: height * (547/379)
                    fillMode: Image.PreserveAspectFit
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    text: "舟山渊视科技有限公司  V4.0"
                    color: "#00cc88"
                    font.pixelSize: 12
                    font.family: "Microsoft YaHei UI"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 窗口控制按钮（Canvas 风格，与工具栏一致）
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Repeater {
                    model: [
                        { act: "min",   tip: "最小化" },
                        { act: "max",   tip: "最大化" },
                        { act: "close", tip: "关闭"   }
                    ]
                    delegate: Rectangle {
                        width: 32; height: 32
                        radius: 2
                        color: wma.containsMouse ? (modelData.act === "close" ? "#3a0808" : "#0d2a1e") : "transparent"
                        border.color: wma.containsMouse ? (modelData.act === "close" ? "#ff3040" : "#00ff99") : "transparent"
                        border.width: 1

                        Canvas {
                            anchors.centerIn: parent
                            width: 14; height: 14
                            property color ic: wma.containsMouse
                                ? (modelData.act === "close" ? "#ff3040" : "#00ff99")
                                : "#00cc88"
                            onIcChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0,0,width,height)
                                ctx.strokeStyle = ic; ctx.fillStyle = ic; ctx.lineWidth = 1.5
                                var a = modelData.act
                                if (a === "min") {
                                    ctx.beginPath(); ctx.moveTo(1,10); ctx.lineTo(13,10); ctx.stroke()
                                } else if (a === "max") {
                                    ctx.beginPath(); ctx.rect(1,1,12,12); ctx.stroke()
                                } else {
                                    ctx.lineWidth = 2
                                    ctx.beginPath(); ctx.moveTo(2,2); ctx.lineTo(12,12); ctx.stroke()
                                    ctx.beginPath(); ctx.moveTo(12,2); ctx.lineTo(2,12); ctx.stroke()
                                }
                            }
                        }

                        MouseArea {
                            id: wma
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (!uiCtrl) return
                                if      (modelData.act === "min")   uiCtrl.cmdWinMinimize()
                                else if (modelData.act === "max")   uiCtrl.cmdWinMaximize()
                                else if (modelData.act === "close") uiCtrl.cmdWinClose()
                            }
                        }
                    }
                }
            }
        }

        // 顶部工具栏
        TopToolBar {
            Layout.fillWidth: true
            height: 46
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 左侧设备面板
            DevicePanel {
                width: 200
                Layout.fillHeight: true
            }

            // 中央视频区（ZoomPanImageView 由 C++ 嵌入此处）
            Rectangle {
                id: videoArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#000000"
                border.color: "#00cc88"
                border.width: 1
                objectName: "videoArea"

                Text {
                    anchors.centerIn: parent
                    text: "视频显示区"
                    color: "#00cc88"
                    font.pixelSize: 13
                    font.family: "Microsoft YaHei UI"
                    opacity: 0.3
                    visible: !(uiCtrl && uiCtrl.rtspConnected)
                }
            }

            // 右侧状态面板（只显示状态，不放按钮）
            HudPanel {
                width: 190
                Layout.fillHeight: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5

                    Text {
                        text: "系统状态"
                        color: "#00cc88"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.4 }

                    StatusItem { label: "分辨率"; value: uiCtrl ? uiCtrl.resolution : "1920x1080" }
                    StatusItem { label: "帧率";   value: uiCtrl ? (uiCtrl.currentFps + " fps") : "0 fps" }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.2 }

                    Text {
                        text: "录像状态"
                        color: "#00cc88"
                        font.pixelSize: 12
                        font.bold: true
                        font.family: "Microsoft YaHei UI"
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.4 }

                    StatusItem { label: "状态";   value: (uiCtrl && uiCtrl.recording) ? "录像中" : "已停止" }
                    // 文件名两行显示
                    Column {
                        spacing: 2
                        Layout.fillWidth: true
                        Text { text: "文件名"; color: "#9aa0a6"; font.pixelSize: 12; font.family: "Microsoft YaHei UI" }
                        Text {
                            text: uiCtrl ? uiCtrl.recordFileName.replace(/^.*[/\\]/, '').replace(/\.[^.]+$/, '') : ""
                            color: "#00ff99"; font.pixelSize: 11; font.family: "Microsoft YaHei UI"
                            width: parent.width; wrapMode: Text.WrapAnywhere
                        }
                    }
                    StatusItem { label: "分段";   value: uiCtrl ? String(uiCtrl.recordSegmentIndex) : "0" }
                    StatusItem { label: "分段时长"; value: uiCtrl ? uiCtrl.recordSegmentElapsed : "00:00" }
                    StatusItem { label: "总时长"; value: uiCtrl ? uiCtrl.recordTotalElapsed : "00:00" }

                    Rectangle { Layout.fillWidth: true; height: 1; color: "#00cc88"; opacity: 0.2 }

                    PathStatusItem { label: "截图路径"; path: uiCtrl ? uiCtrl.screenshotPath : "" }
                    PathStatusItem { label: "录像路径"; path: uiCtrl ? uiCtrl.recordSavePath : "" }

                    Item { Layout.fillHeight: true }
                }
            }
        }

        // 底部日志区
        Rectangle {
            Layout.fillWidth: true
            height: 110
            color: "#07110e"
            border.color: "#00cc88"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                Text {
                    text: "系统日志"
                    color: "#00cc88"
                    font.pixelSize: 11
                    font.bold: true
                    font.family: "Microsoft YaHei UI"
                }

                ListView {
                    id: logView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: logModel
                    clip: true

                    delegate: Text {
                        width: logView.width
                        text: modelData
                        color: "#9aa0a6"
                        font.pixelSize: 11
                        font.family: "Microsoft YaHei UI"
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    // 日志数据模型（C++ 通过 uiCtrl.appendLog 追加）
    ListModel { id: logModel }

    // 自动刷新设备列表（每 5 秒）
    Timer {
        interval: 5000
        running: true
        repeat: true
        onTriggered: if (uiCtrl) uiCtrl.cmdRefreshDevices()
    }

    Connections {
        target: uiCtrl
        function onLogAppended(msg) {
            if (logModel.count >= 200) logModel.remove(0)
            logModel.append({ "modelData": msg })
            logView.positionViewAtEnd()
        }
    }

    // Toast 提示（属性绑定驱动）
    Rectangle {
        visible: uiCtrl && uiCtrl.toastVisible
        z: 200
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 120
        width: toastText.implicitWidth + 32
        height: 40
        radius: 4
        color: (uiCtrl && uiCtrl.toastSuccess) ? "#0a2a1a" : "#2a0a0a"
        border.color: (uiCtrl && uiCtrl.toastSuccess) ? "#00ff99" : "#ff3040"
        border.width: 1
        Text {
            id: toastText
            anchors.centerIn: parent
            text: uiCtrl ? uiCtrl.toastMsg : ""
            color: (uiCtrl && uiCtrl.toastSuccess) ? "#00ff99" : "#ff3040"
            font.pixelSize: 13
            font.family: "Microsoft YaHei UI"
        }
    }

    // IP 修改等待遮罩（属性绑定驱动）
    Rectangle {
        visible: uiCtrl && uiCtrl.ipWaiting
        z: 200
        anchors.fill: parent
        color: "#80000000"
        Rectangle {
            anchors.centerIn: parent
            width: 320; height: 90
            color: "#020806"
            border.color: "#00cc88"
            border.width: 1
            radius: 4
            Row {
                anchors.centerIn: parent
                spacing: 10
                Rectangle {
                    width: 10; height: 10; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: "#00ff99"
                    SequentialAnimation on opacity {
                        running: uiCtrl && uiCtrl.ipWaiting
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.2; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }
                Text {
                    text: uiCtrl ? uiCtrl.ipWaitingMsg : ""
                    color: "#00ff99"
                    font.pixelSize: 13
                    font.family: "Microsoft YaHei UI"
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
