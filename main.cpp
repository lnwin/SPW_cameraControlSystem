#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QApplication>
#include "mainwindow.h"

static void applySimpleDarkBlueTheme(QApplication &app)
{
    QPalette pal = app.palette();

    // 整体背景色（窗口）
    pal.setColor(QPalette::Window,      QColor("#0b1120"));
    pal.setColor(QPalette::Base,        QColor("#020617"));
    pal.setColor(QPalette::AlternateBase, QColor("#020617"));

    // 文字颜色
    pal.setColor(QPalette::WindowText,  Qt::white);
    pal.setColor(QPalette::Text,        Qt::white);
    pal.setColor(QPalette::ButtonText,  Qt::white);

    // 按钮/高亮
    pal.setColor(QPalette::Button,      QColor("#111827"));
    pal.setColor(QPalette::Highlight,   QColor("#2563eb"));
    pal.setColor(QPalette::HighlightedText, Qt::white);

    app.setPalette(pal);

    // ★★★ 在这里追加 QSS ★★★
    const char *qss = R"(

        /* 下拉列表项的文字颜色 */
        QComboBox QAbstractItemView {
            color: #000000;             /* 黑色字体 */
            background-color: #ffffff;  /* 白色背景（可选） */
        }
        QComboBox {
            color: #000000;             /* 黑色字体 */
            background-color: #ffffff;  /* 白色背景（可选） */
        }

        /* 当前选中项 */
        QComboBox QAbstractItemView::item:selected {
            background-color: #d1d5db;  /* 深一点的灰 */
            color: #000000;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 10px;
            padding: 2px 4px;
            color: #ffffff;              /* ★ 标题白色 */
            font-weight: bold;           /* 可选：标题加粗 */
        }
        /* TableView 表头颜色 */
        QHeaderView::section {
            background-color: #1a2335;   /* 深蓝 */
            color: #ffffff;
            padding: 4px;
            border: none;
        }
        QTableCornerButton::section {
            background-color: #1a2335;
            border: none;
        }

        /* 弹窗背景 */
        QDialog, QMessageBox {
            background-color: #0b1120;
            color: #ffffff;
        }

        /* 按钮统一风格 */
        QPushButton {
            background-color: #111827;
            color: #ffffff;
            border-radius: 4px;
            padding: 4px 10px;
        }
        QPushButton:hover {
            background-color: #1f2937;
        }
        QPushButton:pressed {
            background-color: #020617;
        }
    )";

    app.setStyleSheet(qss);
}


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
     qRegisterMetaType<myRecordOptions>("myRecordOptions");
    applySimpleDarkBlueTheme(a);   // ★ 一句话启用主题
    MainWindow w; w.show();
    return a.exec();
}
