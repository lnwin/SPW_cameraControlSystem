#ifndef MYSTRUCT_H
#define MYSTRUCT_H
#include <QString>

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








#endif // MYSTRUCT_H
