#pragma once

#include <QImage>

namespace ss::fx {

// 高斯模糊（NFR-05）。
// 用三次盒式模糊近似高斯，每像素耗时与半径无关；按行带分给线程池；
// 半径大于 downscaleThreshold 时先缩小、模糊、再放大。
// 输入任意格式，输出 Format_ARGB32_Premultiplied（不透明输入时等价于 RGB32）。
QImage gaussianBlur(const QImage& source, int radius, int downscaleThreshold = 12);

// 单次盒式模糊（水平 + 垂直），radius 为半窗口宽度。供测试与参考实现使用。
QImage boxBlur(const QImage& source, int radius);

// 参考实现：真正的高斯卷积（可分离），很慢，只用于测试比对。
QImage referenceGaussianBlur(const QImage& source, double sigma);

// 高斯半径 → 三次盒式模糊的窗口大小（Kutskir 公式）。
void boxSizesForGaussian(double sigma, int sizes[3]);

} // namespace ss::fx
