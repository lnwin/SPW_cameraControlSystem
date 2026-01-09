#pragma once

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>

#include <opencv2/opencv.hpp>
#include <vector>
#include <cmath>
#include <algorithm>
#include <QElapsedTimer>
#include <QDebug>

// 你原来的参数结构体（若在别处已有定义，就删掉这里并 include 原头文件）
struct LabABFixed {
    float ga = 1.0f, gb = 1.0f;
    float da = 0.0f, db = 0.0f;
    float chromaGain = 1.0f;
    float chromaGamma = 1.0f;
    float chromaMax = 128.0f;
    float abShiftClamp = 55.0f;
    bool  keepL = true;
};

class ColorTuneWorker : public QObject
{
    Q_OBJECT
public:
    explicit ColorTuneWorker(QObject* parent = nullptr);

    // 线程安全的参数更新（UI线程调用也安全）
    void setEnabled(bool en);
    void setParams(const LabABFixed& p);
    void setMeanStride(int s);
    void setCorrRebuildThr(float thr);

public slots:
    // 输入帧：UI线程发过来（QueuedConnection）
    void onFrameIn(QSharedPointer<QImage> img);

signals:
    // 输出帧：发回 UI 线程（QueuedConnection）
    void frameOut(QSharedPointer<QImage> img);
private:
    bool force1080_ = true;
    int  outW_ = 1920;
    int  outH_ = 1080;

    QSharedPointer<QImage> upBuf_[3];
    int upIdx_ = 0;
    int upBufW_ = -1;
    int upBufH_ = -1;

    void ensureUpBuffersLocked(int w, int h);

private:
    // ====== 你原来的 helper，整体搬进来 ======
    static inline cv::Mat QImageToBgr8(const QImage& img)
    {
        QImage im = img.convertToFormat(QImage::Format_RGB888);
        cv::Mat rgb(im.height(), im.width(), CV_8UC3, (void*)im.bits(), im.bytesPerLine());
        cv::Mat bgr;
        cv::cvtColor(rgb, bgr, cv::COLOR_RGB2BGR);
        return bgr.clone();
    }

    static inline QImage Bgr8ToQImage(const cv::Mat& bgr)
    {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        QImage out((const uchar*)rgb.data, rgb.cols, rgb.rows, (int)rgb.step, QImage::Format_RGB888);
        return out.copy();
    }

    static inline void meanAB_stride(const cv::Mat& lab_u8, int stride, float& meanA, float& meanB)
    {
        const int rows = lab_u8.rows, cols = lab_u8.cols;
        uint64_t sumA = 0, sumB = 0, cnt = 0;
        if (stride < 1) stride = 1;

        for (int y = 0; y < rows; y += stride) {
            const cv::Vec3b* row = lab_u8.ptr<cv::Vec3b>(y);
            for (int x = 0; x < cols; x += stride) {
                sumA += row[x][1];
                sumB += row[x][2];
                cnt++;
            }
        }
        if (!cnt) { meanA = 128.f; meanB = 128.f; return; }
        meanA = (float)sumA / (float)cnt;
        meanB = (float)sumB / (float)cnt;
    }

    static inline void buildAB_LUT(std::vector<uint16_t>& lut,
                                   const LabABFixed& p,
                                   float corrA, float corrB)
    {
        lut.resize(65536);
        const float clampShift = p.abShiftClamp;
        const bool doChroma = (p.chromaGain != 1.0f || p.chromaGamma != 1.0f);

        for (int au=0; au<256; ++au) {
            for (int bu=0; bu<256; ++bu) {
                const float a = (float)au;
                const float b = (float)bu;

                float a0 = (a - 128.0f) + corrA;
                float b0 = (b - 128.0f) + corrB;

                float a_lin = a0 * p.ga + p.da;
                float b_lin = b0 * p.gb + p.db;

                if (doChroma) {
                    const float eps = 1e-6f;
                    float r = std::sqrt(a_lin*a_lin + b_lin*b_lin) + eps;
                    float rn = std::min(r / 128.0f, 1.5f);
                    float rn2 = std::pow(std::max(rn, 0.0f), p.chromaGamma) * p.chromaGain;
                    float r2 = rn2 * 128.0f;
                    r2 = std::min(r2, p.chromaMax);
                    float scale = r2 / r;
                    a_lin *= scale;
                    b_lin *= scale;
                }

                float a2 = a_lin + 128.0f;
                float b2 = b_lin + 128.0f;

                float da2 = std::clamp(a2 - a, -clampShift, clampShift);
                float db2 = std::clamp(b2 - b, -clampShift, clampShift);
                a2 = a + da2;
                b2 = b + db2;

                int ai = (int)std::lround(std::clamp(a2, 0.f, 255.f));
                int bi = (int)std::lround(std::clamp(b2, 0.f, 255.f));

                lut[(au<<8) | bu] = (uint16_t)((ai & 255) | ((bi & 255) << 8));
            }
        }
    }

    static inline void applyAB_LUT_inplace(cv::Mat& lab_u8, const std::vector<uint16_t>& lut)
    {
        const int rows = lab_u8.rows;
        const int cols = lab_u8.cols;

        for (int y = 0; y < rows; ++y) {
            unsigned char* p = lab_u8.ptr<unsigned char>(y); // L a b L a b ...
            int x = 0;

            // 4 像素展开（每像素 3 字节）
            for (; x + 3 < cols; x += 4) {
                unsigned char* p0 = p + (x + 0) * 3;
                unsigned char* p1 = p + (x + 1) * 3;
                unsigned char* p2 = p + (x + 2) * 3;
                unsigned char* p3 = p + (x + 3) * 3;

                uint16_t v0 = lut[(p0[1] << 8) | p0[2]];
                uint16_t v1 = lut[(p1[1] << 8) | p1[2]];
                uint16_t v2 = lut[(p2[1] << 8) | p2[2]];
                uint16_t v3 = lut[(p3[1] << 8) | p3[2]];

                p0[1] = (unsigned char)(v0 & 255); p0[2] = (unsigned char)(v0 >> 8);
                p1[1] = (unsigned char)(v1 & 255); p1[2] = (unsigned char)(v1 >> 8);
                p2[1] = (unsigned char)(v2 & 255); p2[2] = (unsigned char)(v2 >> 8);
                p3[1] = (unsigned char)(v3 & 255); p3[2] = (unsigned char)(v3 >> 8);
            }

            // tail
            for (; x < cols; ++x) {
                unsigned char* px = p + x * 3;
                uint16_t v = lut[(px[1] << 8) | px[2]];
                px[1] = (unsigned char)(v & 255);
                px[2] = (unsigned char)(v >> 8);
            }
        }
    }


    QImage applyColorTuneFast_locked(const QImage& in);

private:
    // 参数与 LUT 缓存（仅在 worker 线程内使用）
    bool enable_ = true;
    LabABFixed tuneParams_;
    int   meanStride_ = 4;
    float corrRebuildThr_ = 0.5f;

    bool  lutValid_ = false;
    float lastCorrA_ = 1e9f;
    float lastCorrB_ = 1e9f;
    std::vector<uint16_t> abLut_;
    // 复用缓冲，避免每帧分配
    cv::Mat lab_u8_;     // CV_8UC3
    cv::Mat rgb_out_;    // wrap QImage buffer（不持久拥有）
    // ===== 复用缓冲：避免每帧分配 =====


};
