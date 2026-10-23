#pragma once

#include <QImage>
#include <cstdint>

namespace ss::fx {

// HDR 显示器截图的色彩校正（CAP-34）：桌面复制在 HDR 模式下给出的是 scRGB 线性半精度浮点（1.0 = 80 尼特）。
// - PreserveHighlights：保高光，对超过 SDR 白的部分做软压缩（扩展 Reinhard），不过曝、不发灰
// - PreserveBrightness：保亮度，按 SDR 白电平线性映射后裁剪，画面亮度与屏幕所见一致，高光可能截断
enum class HdrStrategy { PreserveHighlights, PreserveBrightness };

// 半精度 → 单精度
float halfToFloat(uint16_t h);
// 线性 → sRGB 编码（0~1）
float linearToSrgb(float v);

// 把 scRGB FP16 像素（每像素 4 个 half：R G B A）转换为 8 位 RGB32 图像。
// sdrWhiteNits：系统的 SDR 白电平（Windows 默认 80 尼特即 1.0；用户调高“SDR 内容亮度”后更大）
QImage toneMapScRgb(const uint16_t* pixels, int width, int height, int rowPitchBytes, float sdrWhiteNits, HdrStrategy strategy);

} // namespace ss::fx
