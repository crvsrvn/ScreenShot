#pragma once

#include <QImage>
#include <QPainterPath>

namespace ss::fx {

// 智能擦除（ANN-30）：按选区边界的背景修补选区内容。
// 本地纯算法（由边界向内逐层扩散、再平滑），不依赖模型；平坦或渐变背景上可抹去文字 / 图标不留痕迹。
// mask：要修补的区域（图像坐标）。返回整幅图像的副本，区域内像素被替换。
QImage inpaint(const QImage& image, const QPainterPath& mask, int smoothPasses = 3);

} // namespace ss::fx
