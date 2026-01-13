#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

#include "mainwindow.h"

// ---------------- theme ----------------
static void applySimpleDarkBlueTheme(QApplication &app)
{
    QPalette pal = app.palette();

    pal.setColor(QPalette::Window,      QColor("#0b1120"));
    pal.setColor(QPalette::Base,        QColor("#020617"));
    pal.setColor(QPalette::AlternateBase, QColor("#020617"));

    pal.setColor(QPalette::WindowText,  Qt::white);
    pal.setColor(QPalette::Text,        Qt::white);
    pal.setColor(QPalette::ButtonText,  Qt::white);

    pal.setColor(QPalette::Button,      QColor("#111827"));
    pal.setColor(QPalette::Highlight,   QColor("#2563eb"));
    pal.setColor(QPalette::HighlightedText, Qt::white);

    app.setPalette(pal);

    const char *qss = R"(
        QComboBox QAbstractItemView {
            color: #000000;
            background-color: #ffffff;
        }
        QComboBox {
            color: #000000;
            background-color: #ffffff;
        }
        QComboBox QAbstractItemView::item:selected {
            background-color: #d1d5db;
            color: #000000;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 2px 4px;
            color: #ffffff;
            font-weight: bold;
        }
        QHeaderView::section {
            background-color: #1a2335;
            color: #ffffff;
            padding: 4px;
            border: none;
        }
        QTableCornerButton::section {
            background-color: #1a2335;
            border: none;
        }
        QDialog, QMessageBox {
            background-color: #0b1120;
            color: #ffffff;
        }
        QPushButton {
            background-color: #111827;
            color: #ffffff;
            border-radius: 4px;
            padding: 4px 10px;
        }
        QPushButton:hover { background-color: #1f2937; }
        QPushButton:pressed { background-color: #020617; }
    )";
    app.setStyleSheet(qss);
}

// ---------------- gstreamer env ----------------
static void setupGStreamerRuntime(const char* argv0)
{
#ifdef Q_OS_WIN
    // 不依赖 QApplication/QCoreApplication，避免 “Please instantiate QApplication first”
    const QString appDir = QFileInfo(QString::fromLocal8Bit(argv0)).absolutePath();

    // 你打包的相对目录：appDir/gstreamer/...
    const QString gstRoot = QDir(appDir).filePath("gstreamer");

    const QString gstBin   = QDir(gstRoot).filePath("bin");
    const QString gstLib   = QDir(gstRoot).filePath("lib");
    const QString gstPlug  = QDir(gstLib).filePath("gstreamer-1.0");
    const QString gstShare = QDir(gstRoot).filePath("share");
    const QString glibSchemas = QDir(gstShare).filePath("glib-2.0/schemas");

    // 1) PATH：让 Windows 找到 gstreamer 的 dll
    QString path = qEnvironmentVariable("PATH");
    if (!path.startsWith(gstBin + ";"))
        path = gstBin + ";" + path;
    qputenv("PATH", path.toLocal8Bit());

    // 2) 插件路径（关键）
    qputenv("GST_PLUGIN_PATH", gstPlug.toLocal8Bit());
    qputenv("GST_PLUGIN_SYSTEM_PATH", gstPlug.toLocal8Bit());

    // 3) glib schema（可选但推荐）
    qputenv("GSETTINGS_SCHEMA_DIR", glibSchemas.toLocal8Bit());

    // 4) 调试时可打开：看插件扫描到底去哪了
    // qputenv("GST_DEBUG", "2");

    // 5) 基础存在性检查（强烈建议保留一段时间）
    if (!QDir(gstBin).exists())
        qWarning().noquote() << "[GST-ENV] missing:" << gstBin;
    if (!QDir(gstPlug).exists())
        qWarning().noquote() << "[GST-ENV] missing:" << gstPlug;
    if (!QFileInfo(QDir(glibSchemas).filePath("gschemas.compiled")).exists())
        qWarning().noquote() << "[GST-ENV] missing: gschemas.compiled under" << glibSchemas;

    qInfo().noquote() << "[GST-ENV] appDir =" << appDir;
    qInfo().noquote() << "[GST-ENV] gstBin =" << gstBin;
    qInfo().noquote() << "[GST-ENV] gstPlug=" << gstPlug;
#endif
}

int main(int argc, char *argv[])
{
    setupGStreamerRuntime(argv[0]);

    QApplication a(argc, argv);

    qRegisterMetaType<myRecordOptions>("myRecordOptions");

    applySimpleDarkBlueTheme(a);

    MainWindow w;
    w.show();

    return a.exec();
}
