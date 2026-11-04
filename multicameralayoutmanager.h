#pragma once

#include <QObject>
#include <QRect>
#include <QList>
#include <QString>

// 布局模式枚举
enum class LayoutMode {
    Single,      // 1台相机：单窗口
    Dual,        // 2台相机：左右双窗口
    Quad,        // 3-4台相机：2x2四宫格
    Grid_3x2,    // 5-6台相机：3列x2行
    Grid_4x2,    // 7-8台相机：4列x2行
    GridAuto     // 8台以上：自动计算网格
};

// 单个视频窗口的布局信息
struct VideoWindowLayout {
    QString sn;           // 相机SN号
    QRect geometry;       // 窗口几何位置
    bool visible;         // 是否可见
};

// 多相机布局管理器：根据活跃相机数量自动计算布局
class MultiCameraLayoutManager : public QObject
{
    Q_OBJECT
public:
    explicit MultiCameraLayoutManager(QObject* parent = nullptr);

    // 设置可用的显示区域
    void setDisplayArea(const QRect& area);
    QRect displayArea() const { return displayArea_; }

    // 计算布局（根据活跃相机数量）
    QList<VideoWindowLayout> calculateLayout(const QList<QString>& activeCameraSns);

    // 获取当前布局模式
    LayoutMode currentMode() const { return currentMode_; }

    // 设置窗口间距和边距
    void setSpacing(int spacing) { spacing_ = spacing; }
    void setMargin(int margin) { margin_ = margin; }

private:
    LayoutMode selectLayoutMode(int cameraCount) const;
    QList<VideoWindowLayout> calculateSingleLayout(const QList<QString>& sns);
    QList<VideoWindowLayout> calculateDualLayout(const QList<QString>& sns);
    QList<VideoWindowLayout> calculateQuadLayout(const QList<QString>& sns);
    QList<VideoWindowLayout> calculateGrid3x2Layout(const QList<QString>& sns);
    QList<VideoWindowLayout> calculateGrid4x2Layout(const QList<QString>& sns);
    QList<VideoWindowLayout> calculateGridAutoLayout(const QList<QString>& sns);

    // 辅助函数：计算网格布局
    QList<VideoWindowLayout> calculateGridLayout(const QList<QString>& sns, int cols, int rows);

    QRect displayArea_;
    LayoutMode currentMode_ = LayoutMode::Single;
    int spacing_ = 4;   // 窗口间距
    int margin_ = 0;    // 边距
};
