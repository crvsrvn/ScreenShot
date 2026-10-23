#pragma once

#include <QImage>

namespace ss::fx {

// 马赛克（ANN-29、NFR-05）：按 blockSize 分块求平均色填充。输出 Format_ARGB32_Premultiplied。
QImage mosaic(const QImage& source, int blockSize);

} // namespace ss::fx
