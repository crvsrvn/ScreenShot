#include "core/fx/HdrToneMap.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ss::fx {

float halfToFloat(uint16_t h)
{
    const uint32_t sign = (h >> 15) & 1;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t mant = h & 0x3FF;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign << 31;
        } else {
            // 非规格化：归一化
            uint32_t e = 127 - 15 + 1;
            uint32_t m = mant;
            while (!(m & 0x400)) {
                m <<= 1;
                --e;
            }
            bits = (sign << 31) | (e << 23) | ((m & 0x3FF) << 13);
        }
    } else if (exp == 31) {
        bits = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        bits = (sign << 31) | ((exp - 15 + 127) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof f);
    return f;
}

float linearToSrgb(float v)
{
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

namespace {

// 膝点以下保持线性（SDR 内容不变），以上按指数曲线平滑压向 1.0，在膝点处斜率连续
float compressHighlight(float v, float whitePoint)
{
    constexpr float knee = 0.8f;
    if (v <= knee)
        return v;
    return knee + (1.0f - knee) * (1.0f - std::exp(-(v - knee) / (whitePoint - knee)));
}

} // namespace

QImage toneMapScRgb(const uint16_t* pixels, int width, int height, int rowPitchBytes, float sdrWhiteNits, HdrStrategy strategy)
{
    QImage out(width, height, QImage::Format_RGB32);
    const float sdrScale = 80.0f / std::max(1.0f, sdrWhiteNits); // SDR 白 → 1.0
    constexpr float kWhitePoint = 4.0f;                          // 4 倍 SDR 白以上全部压到纯白
    for (int y = 0; y < height; ++y) {
        const uint16_t* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const char*>(pixels) + static_cast<size_t>(y) * rowPitchBytes);
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            float rgb[3];
            for (int c = 0; c < 3; ++c)
                rgb[c] = std::max(0.0f, halfToFloat(row[x * 4 + c]) * sdrScale);
            if (strategy == HdrStrategy::PreserveHighlights) {
                // 按亮度压缩，保持色相：三通道同比例缩放
                const float lum = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
                if (lum > 1e-6f) {
                    const float k = compressHighlight(lum, kWhitePoint) / lum;
                    for (float& v : rgb)
                        v *= k;
                }
            }
            dst[x] = qRgb(static_cast<int>(std::lround(linearToSrgb(rgb[0]) * 255.0f)), static_cast<int>(std::lround(linearToSrgb(rgb[1]) * 255.0f)),
                          static_cast<int>(std::lround(linearToSrgb(rgb[2]) * 255.0f)));
        }
    }
    return out;
}

} // namespace ss::fx
