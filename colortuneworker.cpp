#include "colortuneworker.h"

ColorTuneWorker::ColorTuneWorker(QObject* parent)
    : QObject(parent)
{
    // 预热：按 corrA=corrB=0 构建一次 LUT，避免首帧 95ms 尖峰
    buildAB_LUT(abLut_, tuneParams_, 0.0f, 0.0f);
    lastCorrA_ = 0.0f;
    lastCorrB_ = 0.0f;
    lutValid_  = true;
    cv::setUseOptimized(true);
    cv::setNumThreads(std::max(1u, std::thread::hardware_concurrency()));

}

void ColorTuneWorker::setEnabled(bool en) { enable_ = en; }
void ColorTuneWorker::setParams(const LabABFixed& p) { tuneParams_ = p; lutValid_ = false; }
void ColorTuneWorker::setMeanStride(int s) { meanStride_ = s; }
void ColorTuneWorker::setCorrRebuildThr(float thr) { corrRebuildThr_ = thr; }

void ColorTuneWorker::onFrameIn(QSharedPointer<QImage> img)
{
    if (img.isNull() || img->isNull()) return;

    // bypass：仍然可以选择直接输出，但如果你“保存/显示必须1080”，那 bypass 也应升到1080
    if (!enable_) {
        // UI/录制/截图都必须保持源分辨率（1224x1024），不在这里做任何缩放
        emit frameOut(img);
        return;
    }

    // 1) 先在“源分辨率”做 ColorTune（你已有优化版）
    QImage tuned = applyColorTuneFast_locked(*img);

    // 2) 再在 worker 内“最后一步”放大到 1920x1080（只做一次）
    // 重要：worker 输出必须保持源分辨率（1224x1024），不做 1080 缩放
    emit frameOut(QSharedPointer<QImage>::create(std::move(tuned)));
    return;

}



QImage ColorTuneWorker::applyColorTuneFast_locked(const QImage& in)
{
    // 1) 输入：尽量零拷贝 wrap
    QImage rgbIn = in;
    if (rgbIn.format() != QImage::Format_RGB888) {
        rgbIn = rgbIn.convertToFormat(QImage::Format_RGB888); // 仅在格式不对时发生拷贝
    }

    const int w = rgbIn.width();
    const int h = rgbIn.height();

    cv::Mat rgb_in(h, w, CV_8UC3, (void*)rgbIn.constBits(), rgbIn.bytesPerLine());

    // 2) 复用 lab buffer
    lab_u8_.create(h, w, CV_8UC3);

    // 3) 颜色转换：RGB -> Lab（少一次 BGR 往返）
    cv::cvtColor(rgb_in, lab_u8_, cv::COLOR_RGB2Lab);

    // 4) 计算 meanA/meanB
    float meanA = 128.f, meanB = 128.f;
    meanAB_stride(lab_u8_, meanStride_, meanA, meanB);

    const float kNeutral  = 0.45f;
    const float corrClamp = 10.0f;
    float corrA = (128.0f - meanA) * kNeutral;
    float corrB = (128.0f - meanB) * kNeutral;
    corrA = std::clamp(corrA, -corrClamp, corrClamp);
    corrB = std::clamp(corrB, -corrClamp, corrClamp);

    if (!lutValid_ ||
        std::fabs(corrA - lastCorrA_) > corrRebuildThr_ ||
        std::fabs(corrB - lastCorrB_) > corrRebuildThr_)
    {
        buildAB_LUT(abLut_, tuneParams_, corrA, corrB);
        lastCorrA_ = corrA;
        lastCorrB_ = corrB;
        lutValid_  = true;
    }

    // 5) LUT：原地改 AB（效果不变）
    applyAB_LUT_inplace(lab_u8_, abLut_);

    // 6) 输出：直接创建 QImage，让 cvtColor 写进去（避免 out.copy）
    QImage out(w, h, QImage::Format_RGB888);
    cv::Mat rgb_out(h, w, CV_8UC3, (void*)out.bits(), out.bytesPerLine());

    // 7) Lab -> RGB
    cv::cvtColor(lab_u8_, rgb_out, cv::COLOR_Lab2RGB);

    return out; // out 自己持有内存，无需 copy
}
void ColorTuneWorker::ensureUpBuffersLocked(int w, int h)
{
    if (w == upBufW_ && h == upBufH_
        && upBuf_[0] && upBuf_[1] && upBuf_[2]) return;

    upBufW_ = w;
    upBufH_ = h;
    for (int i = 0; i < 3; ++i) {
        upBuf_[i] = QSharedPointer<QImage>::create(upBufW_, upBufH_, QImage::Format_RGB888);
    }
    upIdx_ = 0;
}
