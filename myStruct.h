#ifndef MYSTRUCT_H
#define MYSTRUCT_H
#include <QString>
#include <QMetaType>

#define mp4 101
#define avi 102
#define jpg 201
#define png 202
#define bmp 203
enum class VideoContainer {
    MP4,
    AVI
};

enum class ImageFormat {
    PNG,
    JPG,
    BMP
};
struct myRecordOptions {

    QString capturePath;
    QString recordPath;
    ImageFormat  capturType;
    VideoContainer  recordType;

};

Q_DECLARE_METATYPE(myRecordOptions)






#endif // MYSTRUCT_H
