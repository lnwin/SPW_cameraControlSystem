#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>

static void initGstEnv() {
    const char* GST_ROOT = "E:\\ThirdParty\\Gstreamer\\1.0\\msvc_x86_64";
    qputenv("PATH", QByteArray(GST_ROOT) + "\\bin;" + qgetenv("PATH"));
    qputenv("GST_PLUGIN_PATH",        QByteArray(GST_ROOT) + "\\lib\\gstreamer-1.0");
    qputenv("GST_PLUGIN_SYSTEM_PATH", QByteArray(GST_ROOT) + "\\lib\\gstreamer-1.0");
    qputenv("GST_PLUGIN_SCANNER",     QByteArray(GST_ROOT) + "\\libexec\\gstreamer-1.0\\gst-plugin-scanner.exe");
    // 可暂时打开：
    // qputenv("GST_DEBUG", "rtspserver:3,rtpmanager:3");
}


int main(int argc, char *argv[]) {
   // installFileLogger();       // <<< 先装日志
    initGstEnv();              // <<< 再设环境
    QApplication a(argc, argv);
    MainWindow w; w.show();
    return a.exec();
}
