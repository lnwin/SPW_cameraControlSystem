QT += core gui widgets network
CONFIG += c++17
QMAKE_CFLAGS += -utf-8
QMAKE_CXXFLAGS += -utf-8

win32:RC_ICONS = YS-camera-logo.ico

# ===================== Sources =====================
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    rtspviewerqt.cpp \
    udpserver.cpp \
    videorecorder.cpp \
    colortuneworker.cpp \
    restipclient.cpp \
    systemsetting.cpp \
    titlebar.cpp

HEADERS += \
    mainwindow.h \
    rtspviewerqt.h \
    udpserver.h \
    videorecorder.h \
    colortuneworker.h \
    restipclient.h \
    systemsetting.h \
    titlebar.h \
    myStruct.h \
    ZoomPanImageView.h

FORMS += \
    mainwindow.ui \
    systemsetting.ui \
    titlebar.ui

RESOURCES += icons.qrc

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
