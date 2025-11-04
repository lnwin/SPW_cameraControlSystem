#ifndef RTSPVIEWERQT_H
#define RTSPVIEWERQT_H

#include <QObject>

class RtspViewerQt : public QObject
{
    Q_OBJECT
public:
    explicit RtspViewerQt(QObject *parent = nullptr);

signals:
};

#endif // RTSPVIEWERQT_H
