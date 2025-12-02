QT       += core gui network
QMAKE_CFLAGS  += -utf-8
QMAKE_CXXFLAGS += -utf-8

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    restipclient.cpp \
    rtspviewerqt.cpp \
    udpserver.cpp

HEADERS += \
    mainwindow.h \
    restipclient.h \
    rtspviewerqt.h \
    udpserver.h
FORMS += \
    mainwindow.ui

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



