#pragma once

#include <QColor>
#include <QImage>

namespace ss {

// 输出装饰（CAP-32 圆角、CAP-33 阴影 / 边框）。屏幕显示与导出共用。
struct DecorateOptions {
    int cornerRadius = 0;          // 像素；0 为直角
    bool border = false;
    QColor borderColor = QColor(40, 120, 220);
    int borderWidth = 2;
    bool shadow = false;
    QColor shadowColor = QColor(0, 0, 0, 140);
    int shadowRadius = 12;         // 模糊半径
    int shadowOffsetY = 4;
    int shadowMargin() const { return shadow ? shadowRadius * 2 + shadowOffsetY : 0; }
    bool any() const { return cornerRadius > 0 || border || shadow; }
};

// 返回装饰后的图像：有阴影时尺寸外扩 shadowMargin()；圆角角外透明（Format_ARGB32）
QImage decorateImage(const QImage& source, const DecorateOptions& options);

// 把鼠标指针合成到图像上（CAP-30）：cursorTopLeft 相对图像左上角
QImage composeCursor(const QImage& source, const QImage& cursor, QPoint cursorTopLeft);

} // namespace ss
