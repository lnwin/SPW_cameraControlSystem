#ifndef MYSTRUCT_H
#define MYSTRUCT_H
#include <QString>

#define mp4 101
#define avi 102
#define jpg 201
#define png 202
#define bmp 203

struct myRecordOptions {

    QString capturePath;
    QString recordPath;
    int  capturType;
    int  recordType;

};

enum class VideoContainer {
    MP4,
    AVI
};

enum class ImageFormat {
    PNG,
    JPG,
    BMP
};






#endif // MYSTRUCT_H
