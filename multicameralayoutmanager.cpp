#include "multicameralayoutmanager.h"
#include <QtMath>

MultiCameraLayoutManager::MultiCameraLayoutManager(QObject* parent)
    : QObject(parent)
{
}

void MultiCameraLayoutManager::setDisplayArea(const QRect& area)
{
    displayArea_ = area;
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateLayout(const QList<QString>& activeCameraSns)
{
    int count = activeCameraSns.count();
    if (count == 0) {
        return QList<VideoWindowLayout>();
    }

    // 选择布局模式
    currentMode_ = selectLayoutMode(count);

    // 根据模式计算布局
    switch (currentMode_) {
    case LayoutMode::Single:
        return calculateSingleLayout(activeCameraSns);
    case LayoutMode::Dual:
        return calculateDualLayout(activeCameraSns);
    case LayoutMode::Quad:
        return calculateQuadLayout(activeCameraSns);
    case LayoutMode::Grid_3x2:
        return calculateGrid3x2Layout(activeCameraSns);
    case LayoutMode::Grid_4x2:
        return calculateGrid4x2Layout(activeCameraSns);
    case LayoutMode::GridAuto:
        return calculateGridAutoLayout(activeCameraSns);
    default:
        return calculateSingleLayout(activeCameraSns);
    }
}

LayoutMode MultiCameraLayoutManager::selectLayoutMode(int cameraCount) const
{
    if (cameraCount == 1) {
        return LayoutMode::Single;
    } else if (cameraCount == 2) {
        return LayoutMode::Dual;
    } else if (cameraCount >= 3 && cameraCount <= 4) {
        return LayoutMode::Quad;
    } else if (cameraCount >= 5 && cameraCount <= 6) {
        return LayoutMode::Grid_3x2;
    } else if (cameraCount >= 7 && cameraCount <= 8) {
        return LayoutMode::Grid_4x2;
    } else {
        return LayoutMode::GridAuto;
    }
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateSingleLayout(const QList<QString>& sns)
{
    QList<VideoWindowLayout> layouts;
    if (sns.isEmpty()) return layouts;

    VideoWindowLayout layout;
    layout.sn = sns.first();
    layout.geometry = displayArea_.adjusted(margin_, margin_, -margin_, -margin_);
    layout.visible = true;

    layouts.append(layout);
    return layouts;
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateDualLayout(const QList<QString>& sns)
{
    // 左右双窗口：横向1行2列
    return calculateGridLayout(sns, 2, 1);
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateQuadLayout(const QList<QString>& sns)
{
    // 2x2 四宫格
    return calculateGridLayout(sns, 2, 2);
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateGrid3x2Layout(const QList<QString>& sns)
{
    // 3列 x 2行
    return calculateGridLayout(sns, 3, 2);
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateGrid4x2Layout(const QList<QString>& sns)
{
    // 4列 x 2行
    return calculateGridLayout(sns, 4, 2);
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateGridAutoLayout(const QList<QString>& sns)
{
    int count = sns.count();

    // 自动计算行列数：优先横向排列
    // 计算列数：向上取整的平方根
    int cols = qCeil(qSqrt(count));
    // 计算行数
    int rows = qCeil(static_cast<double>(count) / cols);

    return calculateGridLayout(sns, cols, rows);
}

QList<VideoWindowLayout> MultiCameraLayoutManager::calculateGridLayout(const QList<QString>& sns, int cols, int rows)
{
    QList<VideoWindowLayout> layouts;
    if (sns.isEmpty() || cols <= 0 || rows <= 0) {
        return layouts;
    }

    // 可用区域（减去边距）
    QRect availableArea = displayArea_.adjusted(margin_, margin_, -margin_, -margin_);

    // 计算每个格子的尺寸（包括间距）
    int totalWidth = availableArea.width();
    int totalHeight = availableArea.height();

    // 每个格子的宽度和高度（减去间距）
    int cellWidth = (totalWidth - spacing_ * (cols - 1)) / cols;
    int cellHeight = (totalHeight - spacing_ * (rows - 1)) / rows;

    // 为每台相机分配位置
    for (int i = 0; i < sns.count(); ++i) {
        int row = i / cols;
        int col = i % cols;

        if (row >= rows) {
            break; // 超出行数限制
        }

        VideoWindowLayout layout;
        layout.sn = sns[i];

        // 计算位置
        int x = availableArea.left() + col * (cellWidth + spacing_);
        int y = availableArea.top() + row * (cellHeight + spacing_);

        layout.geometry = QRect(x, y, cellWidth, cellHeight);
        layout.visible = true;

        layouts.append(layout);
    }

    return layouts;
}
