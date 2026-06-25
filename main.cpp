#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QUrl>

#include "mainwindow.h"
#include "hudwindow.h"
#include "uicontroller.h"
#include "settingscontroller.h"
#include "videorecorder.h"
#include "udpserver.h"
#include "myStruct.h"
#include "languagemanager.h"
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QQmlEngine>

// ---------- 文件日志 ----------
#include <QDateTime>
#include <QTextStream>
static QFile* g_logFile = nullptr;
static void fileLogHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (!g_logFile || !g_logFile->isOpen()) return;
    const char* lv = (type==QtDebugMsg)?"D":(type==QtInfoMsg)?"I":(type==QtWarningMsg)?"W":"E";
    QTextStream(g_logFile)
        << QDateTime::currentDateTime().toString("MM-dd hh:mm:ss.zzz ")
        << "[" << lv << "] " << msg << "\n";
    g_logFile->flush();
}

static void setupGStreamerRuntime(const char* argv0)
{
#ifdef Q_OS_WIN
    const QString appDir   = QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath();
    const QString gstRoot  = QDir(appDir).filePath("gstreamer");
    const QString gstBin   = QDir(gstRoot).filePath("bin");
    const QString gstPlug  = QDir(gstRoot).filePath("lib/gstreamer-1.0");
    const QString gstScan  = QDir(gstRoot).filePath("libexec/gstreamer-1.0/gst-plugin-scanner.exe");
    const QString gstReg   = QDir(appDir).filePath("logs/gst-registry.bin");

    // 确保 logs 目录存在（GST_REGISTRY 需要可写目录）
    QDir(appDir).mkpath("logs");

    // 将 gstreamer/bin 和 gstreamer/libexec 前置加入 PATH
    QString path = qEnvironmentVariable("PATH");
    const QString inject = gstBin + ";" + QDir(gstRoot).filePath("libexec/gstreamer-1.0");
    if (!path.contains(gstBin)) path = inject + ";" + path;
    qputenv("PATH",                      path.toLocal8Bit());
    qputenv("GST_PLUGIN_PATH",           gstPlug.toLocal8Bit());
    qputenv("GST_PLUGIN_SYSTEM_PATH",    gstPlug.toLocal8Bit());
    qputenv("GST_PLUGIN_SYSTEM_PATH_1_0",gstPlug.toLocal8Bit());  // GStreamer 1.x 专用
    qputenv("GST_PLUGIN_SCANNER",        gstScan.toLocal8Bit());   // 避免扫描器找不到崩溃
    qputenv("GST_REGISTRY",              gstReg.toLocal8Bit());    // 写本地避免权限问题
    qputenv("GSETTINGS_SCHEMA_DIR",
            QDir(gstRoot).filePath("share/glib-2.0/schemas").toLocal8Bit());

    // 启动诊断（文件日志还未安装，先用 stderr，稍后 qInfo 会进文件）
    fprintf(stderr, "[STARTUP] appDir        = %s\n", appDir.toLocal8Bit().constData());
    fprintf(stderr, "[STARTUP] GST_PLUGIN_PATH     = %s\n", gstPlug.toLocal8Bit().constData());
    fprintf(stderr, "[STARTUP] GST_PLUGIN_SCANNER  = %s | exists=%d\n",
            gstScan.toLocal8Bit().constData(), QFileInfo(gstScan).exists());
    fprintf(stderr, "[STARTUP] GST_REGISTRY        = %s\n", gstReg.toLocal8Bit().constData());
    fprintf(stderr, "[STARTUP] gstreamer/bin exists = %d\n", QDir(gstBin).exists());
    fprintf(stderr, "[STARTUP] plugin dir exists    = %d\n", QDir(gstPlug).exists());
#endif
}

int main(int argc, char* argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif

    setupGStreamerRuntime(argv[0]);

    // 安装文件日志（在 QApplication 之前即可，QFile 不依赖 QApplication）
    {
        const QString appDir = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath();
        g_logFile = new QFile(appDir + "/logs/runtime.log");
        g_logFile->open(QIODevice::Append | QIODevice::Text);
        qInstallMessageHandler(fileLogHandler);

        qInfo("[STARTUP] ========== 程序启动 ==========");
        qInfo().noquote() << "[STARTUP] appDir            =" << appDir;
        qInfo().noquote() << "[STARTUP] GST_PLUGIN_PATH   =" << qEnvironmentVariable("GST_PLUGIN_PATH");
        qInfo().noquote() << "[STARTUP] GST_PLUGIN_SYSTEM_PATH_1_0 =" << qEnvironmentVariable("GST_PLUGIN_SYSTEM_PATH_1_0");
        qInfo().noquote() << "[STARTUP] GST_PLUGIN_SCANNER=" << qEnvironmentVariable("GST_PLUGIN_SCANNER");
        qInfo().noquote() << "[STARTUP] GST_REGISTRY      =" << qEnvironmentVariable("GST_REGISTRY");
        qInfo() << "[STARTUP] gstreamer/bin exists     =" << QDir(appDir + "/gstreamer/bin").exists();
        qInfo() << "[STARTUP] plugin dir exists        =" << QDir(appDir + "/gstreamer/lib/gstreamer-1.0").exists();
        qInfo() << "[STARTUP] gst-plugin-scanner exists=" << QFileInfo(appDir + "/gstreamer/libexec/gstreamer-1.0/gst-plugin-scanner.exe").exists();
        qInfo() << "[STARTUP] Qt5Core.dll exists       =" << QFileInfo(appDir + "/Qt5Core.dll").exists();
    }

    QApplication a(argc, argv);
    QFont f; f.setFamily("Microsoft YaHei UI"); f.setPointSize(9);
    a.setFont(f);

    // 加载上次保存的语言（必须在任何 UI 创建之前）
    LanguageManager::instance().loadSaved();

    // 系统弹窗（QInputDialog 等）保持可读
    a.setStyleSheet("QDialog { background:#0b1120; color:#ffffff; }"
                    "QPushButton { background:#111827; color:#ffffff; border-radius:3px; padding:4px 10px; }"
                    "QPushButton:hover { background:#1f2937; }"
                    "QLineEdit { color:#000000; background:#ffffff; }");

    qRegisterMetaType<myRecordOptions>("myRecordOptions");

    MainWindow w;
    UiController uiCtrl;
    w.bindUiController(&uiCtrl);

    const QString appIconDir = QUrl::fromLocalFile(
        QCoreApplication::applicationDirPath() + "/icons/current/").toString();
    qDebug() << "appIconDir =" << appIconDir;

    HudWindow hud(&uiCtrl, {}, appIconDir);
    hud.setWindowIcon(QIcon(":/new/prefix1/release/icons/current/Slogo.png"));
    hud.show();

    QObject::connect(&uiCtrl, &UiController::requestWinMinimize, &hud, &QWidget::showMinimized);
    QObject::connect(&uiCtrl, &UiController::requestWinMaximize, &hud, [&hud](){
        hud.isMaximized() ? hud.showNormal() : hud.showMaximized();
    });
    QObject::connect(&uiCtrl, &UiController::requestWinClose, &hud, &QWidget::close);
    QObject::connect(&uiCtrl, &UiController::requestWinDrag, &hud, [&hud](int dx, int dy){
        if (!hud.isMaximized()) hud.move(hud.x() + dx, hud.y() + dy);
    });

    if (w.videoView()) hud.embedVideoWidget(w.videoView());

    // 系统设置窗口
    SettingsController settingsCtrl;
    QQuickWidget settingsWin;
    settingsWin.setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::Tool);
    settingsWin.rootContext()->setContextProperty("settingsCtrl", &settingsCtrl);
    settingsWin.rootContext()->setContextProperty("langMgr", &LanguageManager::instance());
    settingsWin.rootContext()->setContextProperty("uiCtrl", &uiCtrl);
    settingsWin.setSource(QUrl("qrc:/qml/Settings.qml"));
    settingsWin.setResizeMode(QQuickWidget::SizeRootObjectToView);
    settingsWin.resize(480, 420);

    QObject::connect(&settingsCtrl, &SettingsController::requestDrag, &settingsWin,
                     [&settingsWin](int dx, int dy){ settingsWin.move(settingsWin.x()+dx, settingsWin.y()+dy); });
    QObject::connect(&settingsCtrl, &SettingsController::requestClose, &settingsWin, &QWidget::hide);
    QObject::connect(&settingsCtrl, &SettingsController::settingsSaved,
                     w.myVideoRecorderPublic(), &VideoRecorder::receiveRecordOptions);
    QObject::connect(&settingsCtrl, &SettingsController::settingsSaved,
                     &w, &MainWindow::applyRecordOptions);
    // 路径变化时同步给 UiController
    auto syncPaths = [&](){
        uiCtrl.setScreenshotPath(settingsCtrl.capturePath());
        uiCtrl.setRecordSavePath(settingsCtrl.recordPath());
    };
    QObject::connect(&settingsCtrl, &SettingsController::settingsSaved, &uiCtrl, [&](const myRecordOptions&){ syncPaths(); });
    syncPaths(); // 启动时初始化
    QObject::connect(&uiCtrl, &UiController::requestOpenSettings, &settingsWin, [&settingsWin, &settingsCtrl](){
        settingsCtrl.load(); settingsWin.show(); settingsWin.raise();
    });

    // 修改 IP 弹窗（QML 风格）
    QQuickWidget ipWin;
    ipWin.setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::Tool);
    ipWin.setSource(QUrl("qrc:/qml/ChangeIpDialog.qml"));
    ipWin.setResizeMode(QQuickWidget::SizeRootObjectToView);
    ipWin.resize(400, 190);

    // 连接弹窗信号（setSource 完成后 root 已存在）
    {
        QObject* root = qobject_cast<QObject*>(ipWin.rootObject());
        if (root) {
            QObject::connect(root, SIGNAL(confirmed(QString,QString)), &w,    SLOT(changeIp(QString,QString)));
            QObject::connect(root, SIGNAL(cancelled()),                &ipWin, SLOT(hide()));
            QObject::connect(root, SIGNAL(confirmed(QString,QString)), &ipWin, SLOT(hide()));
        }
    }

    QObject::connect(&uiCtrl, &UiController::requestChangeIp, &ipWin, [&ipWin, &w](const QString& sn){
        QString curIp = "192.168.0.100";
        if (auto* mgr = w.deviceManager()) {
            DeviceInfo dev;
            if (mgr->getDevice(sn, dev)) curIp = dev.ip.toString();
        }
        if (auto* root = ipWin.rootObject()) {
            root->setProperty("sn", sn);
            root->setProperty("currentIp", curIp);
        }
        ipWin.show(); ipWin.raise();
    });

    // 语言切换时刷新所有 QML 引擎中的 qsTr() 绑定
    QObject::connect(&LanguageManager::instance(), &LanguageManager::languageChanged, [&](){
        qInfo() << "[LANG] languageChanged received, retranslate trigger status";
        hud.engine()->retranslate();
        settingsWin.engine()->retranslate();
        ipWin.engine()->retranslate();
        uiCtrl.retranslateTriggerStatus();
    });

    return a.exec();
}
