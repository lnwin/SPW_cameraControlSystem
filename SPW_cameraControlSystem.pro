QT += core gui widgets network qml quick quickwidgets
CONFIG += c++17 lrelease embed_translations
QMAKE_CFLAGS += -utf-8
QMAKE_CXXFLAGS += -utf-8

win32 {
    RC_ICONS = $$PWD/release/icons/current/Slogo.ico
}

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    rtspviewerqt.cpp \
    udpserver.cpp \
    videorecorder.cpp \
    restipclient.cpp \
    uicontroller.cpp \
    hudwindow.cpp \
    settingscontroller.cpp \
    languagemanager.cpp \
    themedmessagedialog.cpp

HEADERS += \
    mainwindow.h \
    rtspviewerqt.h \
    udpserver.h \
    videorecorder.h \
    restipclient.h \
    myStruct.h \
    ZoomPanImageView.h \
    uicontroller.h \
    hudwindow.h \
    settingscontroller.h \
    languagemanager.h \
    themedmessagedialog.h

FORMS += mainwindow.ui

RESOURCES += icons.qrc qml.qrc

TRANSLATIONS += \
    translations/app_zh_CN.ts \
    translations/app_en_US.ts \
    translations/app_ko_KR.ts

# lupdate 扫描 QML 文件
DISTFILES += \
    qml/Main.qml \
    qml/Settings.qml \
    qml/TopToolBar.qml \
    qml/DevicePanel.qml \
    qml/TopStatusBar.qml \
    qml/HudPanel.qml \
    qml/StatusItem.qml \
    qml/PathStatusItem.qml \
    qml/HudButton.qml \
    qml/SideNavButton.qml \
    qml/ChangeIpDialog.qml \
    qml/RecordStatusPanel.qml

# ===================== System =====================
LIBS += -lIphlpapi -lWs2_32
DEFINES += _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX

# ===================== OpenCV =====================
OPENCV_ROOT = E:/ThirdParty/opencv-4.7.0-NoCudda/opencv
INCLUDEPATH += $$OPENCV_ROOT/build/include
LIBS += -L$$OPENCV_ROOT/build/x64/vc16/lib -lopencv_world470

# ===================== FFmpeg =====================
FFMPEG_ROOT = E:/ThirdParty/ffmpeg-8.0-full_build-shared
INCLUDEPATH += $$FFMPEG_ROOT/include
LIBS += -L$$FFMPEG_ROOT/lib \
    -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale

# ===================== GStreamer (MSVC x86_64) =====================
GST_ROOT = E:/ThirdParty/Gstreamer/1.0/msvc_x86_64
INCLUDEPATH += \
    $$GST_ROOT/include \
    $$GST_ROOT/include/gstreamer-1.0 \
    $$GST_ROOT/include/glib-2.0 \
    $$GST_ROOT/lib/glib-2.0/include
LIBS += \
    "$$GST_ROOT/lib/gstreamer-1.0.lib" \
    "$$GST_ROOT/lib/gstapp-1.0.lib" \
    "$$GST_ROOT/lib/gstbase-1.0.lib" \
    "$$GST_ROOT/lib/gstvideo-1.0.lib" \
    "$$GST_ROOT/lib/gstrtsp-1.0.lib" \
    "$$GST_ROOT/lib/gstsdp-1.0.lib" \
    "$$GST_ROOT/lib/glib-2.0.lib" \
    "$$GST_ROOT/lib/gobject-2.0.lib"
