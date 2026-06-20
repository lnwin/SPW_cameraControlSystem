#include "languagemanager.h"
#include "themedmessagedialog.h"
#include <QApplication>
#include <QSettings>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

LanguageManager::LanguageManager() = default;

LanguageManager& LanguageManager::instance()
{
    static LanguageManager inst;
    return inst;
}

void LanguageManager::loadSaved()
{
    QSettings s("SPwater", "CameraControl");
    switchLanguage(s.value("language/locale", "zh_CN").toString(), false);
}

bool LanguageManager::switchLanguage(const QString& locale, bool showErrors)
{
    qApp->removeTranslator(&m_translator);

    bool loaded = false;
    if (locale != "zh_CN") {
        // embed_translations 生成的 qrc 路径为 :/i18n/<filename>.qm
        const QString qrcPath  = QString(":/i18n/app_%1.qm").arg(locale);
        // 备用：exe 同级 translations/ 目录
        const QString filePath = QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QString("translations/app_%1.qm").arg(locale));

        qDebug() << "[LanguageManager] switchLanguage:" << locale;
        qDebug() << "[LanguageManager] trying qrc path :" << qrcPath;
        loaded = m_translator.load(qrcPath);
        qDebug() << "[LanguageManager] qrc load result :" << loaded;
        if (!loaded) {
            qDebug() << "[LanguageManager] trying file path:" << filePath;
            loaded = m_translator.load(filePath);
            qDebug() << "[LanguageManager] file load result:" << loaded;
        }
        if (loaded)
            qApp->installTranslator(&m_translator);
        else if (showErrors)
            ThemedMessageDialog::warning(nullptr, "Error",
                "Language file load failed.\n"
                "Path: " + qrcPath + "\n"
                "Please rebuild the project so .qm files are generated.");
    } else {
        qDebug() << "[LanguageManager] switchLanguage: zh_CN (source language, no qm needed)";
        loaded = true;
    }

    m_locale = locale;
    QSettings("SPwater", "CameraControl").setValue("language/locale", locale);
    emit languageChanged();
    return loaded;
}
