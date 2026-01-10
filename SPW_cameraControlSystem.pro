QT       += core gui network
QMAKE_CFLAGS  += -utf-8
QMAKE_CXXFLAGS += -utf-8

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    colortuneworker.cpp \
    main.cpp \
    mainwindow.cpp \
    restipclient.cpp \
    rtspviewerqt.cpp \
    systemsetting.cpp \
    titlebar.cpp \
    udpserver.cpp \
    videorecorder.cpp

HEADERS += \
    ZoomPanImageView.h \
    colortuneworker.h \
    mainwindow.h \
    myStruct.h \
    restipclient.h \
    rtspviewerqt.h \
    systemsetting.h \
    titlebar.h \
    udpserver.h \
    videorecorder.h
FORMS += \
    mainwindow.ui \
    systemsetting.ui \
    titlebar.ui

TRANSLATIONS += \
    SPW_cameraControlSystem_zh_CN.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
PATH=E:\ThirdParty\Gstreamer\1.0\msvc_x86_64\bin;$(PATH)

INCLUDEPATH += E:/ThirdParty/ffmpeg-8.0-full_build-shared/include
DEPENDPATH += E:/ThirdParty/ffmpeg-8.0-full_build-shared/include

win32: LIBS += -LE:/ThirdParty/ffmpeg-8.0-full_build-shared/lib/ -lavcodec -lavdevice -lavfilter -lavformat -lswresample  -lavutil -lswscale


win32: LIBS += -LE:/ThirdParty/opencv-4.7.0-NoCudda/opencv/build/x64/vc16/lib/ -lopencv_world470

INCLUDEPATH += E:/ThirdParty/opencv-4.7.0-NoCudda/opencv/build/include
DEPENDPATH += E:/ThirdParty/opencv-4.7.0-NoCudda/opencv/build/include
LIBS += -lIphlpapi -lWs2_32
DEFINES += _WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN NOMINMAX

RESOURCES += \
    icons.qrc

# ===================== GStreamer (MSVC x86_64) =====================
GST_ROOT = E:/ThirdParty/Gstreamer/development/1.0/msvc_x86_64

INCLUDEPATH += \
    $$GST_ROOT/include/gstreamer-1.0 \
    $$GST_ROOT/include/glib-2.0 \
    $$GST_ROOT/lib/glib-2.0/include \
    $$GST_ROOT/include

DEPENDPATH += $$INCLUDEPATH

# GStreamer libs (MSVC)
win32: LIBS += -L$$GST_ROOT/lib \
    -lgstreamer-1.0 \
    -lgstapp-1.0 \
    -lgstbase-1.0 \
    -lgstvideo-1.0 \
    -lgobject-2.0 \
    -lglib-2.0

# （可选）如果你用到更多 plugin / net 相关 API，才加下面：
# win32: LIBS += -lgstnet-1.0

# 运行时 DLL 搜索路径：qmake 里写 PATH=... 不会自动影响运行。
# 推荐：在 Qt Creator 的 Run Environment 里加 PATH / GST_PLUGIN_PATH（下面第4节给你）。


