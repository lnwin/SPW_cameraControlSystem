#ifndef SYSTEMSETTING_H
#define SYSTEMSETTING_H

#include <QWidget>
#include <QAbstractButton>
#include <QSettings>
#include <myStruct.h>
#include <QFileDialog>
namespace Ui {
class systemsetting;
}

class systemsetting : public QWidget
{
    Q_OBJECT

public:
    explicit systemsetting(QWidget *parent = nullptr);
    ~systemsetting();

    // 供 MainWindow 启动时读取持久化的 overlayTopText
    static QString loadTopText();
    static void saveTopText(const QString& text);

private slots:

    void on_selectcapturePath_clicked();

    void on_selectrecordPath_clicked();

    void on_buttonBox_clicked(QAbstractButton *button);
    void on_buttonBox_accepted();

signals:

    void sendRecordOptions(myRecordOptions);


private:
    void loadSettings();

    Ui::systemsetting *ui;
    myRecordOptions myrecordOptions;
};

#endif // SYSTEMSETTING_H
