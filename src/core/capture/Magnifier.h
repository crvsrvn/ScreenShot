#pragma once

#include "core/color/ColorFormat.h"
#include "core/geometry/VirtualDesktop.h"

#include <QPainter>
#include <QPoint>
#include <QRect>

namespace ss {

// 放大镜（CAP-21）：显示光标周围像素的放大图、坐标、选区尺寸、像素颜色。
// 纯绘制函数，屏幕显示和测试渲染共用。
struct MagnifierOptions {
    int zoom = 8;              // 倍数
    int pixels = 23;           // 放大区域边长（像素数，奇数使光标居中）
    bool grid = true;
    ColorFormat colorFormat = ColorFormat::Hex;
    bool hexUppercase = true;
    bool showHint = true;      // 快捷键提示
};

// 放大镜面板的建议尺寸（逻辑像素）
QSize magnifierSize(const MagnifierOptions& options);

// 在 painter 上以 topLeft 为左上角绘制放大镜。cursor 为物理像素位置；selection 为当前选区（可空）。
void paintMagnifier(QPainter& painter, QPointF topLeft, const VirtualDesktop& desktop, QPoint cursor, const QRect& selection,
                    const MagnifierOptions& options, const QString& colorText);

// 放大镜放在光标附近而不超出显示器：cursorLogical 为光标在该显示器上的逻辑坐标，screenLogical 为显示器逻辑矩形
QPointF magnifierPlacement(QPointF cursorLogical, QSize panelSize, const QRectF& screenLogical, int offset = 20);

} // namespace ss
