#pragma once
#include <QObject>
#include <QTranslator>

class LanguageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentLocale READ currentLocale NOTIFY languageChanged)
public:
    static LanguageManager& instance();

    // 加载上次保存的语言（main() 里最早调用，静默失败）
    void loadSaved();

    // 切换语言并保存到配置，showErrors=true 时加载失败弹窗提示
    Q_INVOKABLE bool switchLanguage(const QString& locale, bool showErrors = false);

    QString currentLocale() const { return m_locale; }

signals:
    void languageChanged();

private:
    LanguageManager();
    QTranslator m_translator;
    QString     m_locale = "zh_CN";
};
