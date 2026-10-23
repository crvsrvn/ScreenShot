// REQ: CAP-34
// HDR 色彩校正：半精度转换、sRGB 编码、保高光 / 保亮度两种策略
#include "core/fx/HdrToneMap.h"

#include <QTest>
#include <cmath>
#include <vector>

using namespace ss::fx;

namespace {

uint16_t floatToHalf(float f)
{
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof bits);
    const uint32_t sign = (bits >> 16) & 0x8000;
    int exp = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = bits & 0x7FFFFF;
    if (exp <= 0)
        return static_cast<uint16_t>(sign);
    if (exp >= 31)
        return static_cast<uint16_t>(sign | 0x7C00);
    return static_cast<uint16_t>(sign | (exp << 10) | (mant >> 13));
}

// 一行 width 个像素，每像素 RGBA 半精度
std::vector<uint16_t> row(std::initializer_list<float> rgbPerPixel)
{
    std::vector<uint16_t> out;
    int i = 0;
    for (float v : rgbPerPixel) {
        out.push_back(floatToHalf(v));
        if (++i % 3 == 0)
            out.push_back(floatToHalf(1.0f));
    }
    return out;
}

} // namespace

class TestHdr : public QObject {
    Q_OBJECT
private slots:
    void halfConversion();
    void srgbEncoding();
    void sdrContentUnchanged();
    void highlightsCompressedNotClipped();
    void brightnessStrategyClips();
    void sdrWhiteLevelScales();
};

void TestHdr::halfConversion()
{
    QCOMPARE(halfToFloat(floatToHalf(1.0f)), 1.0f);
    QCOMPARE(halfToFloat(floatToHalf(0.5f)), 0.5f);
    QCOMPARE(halfToFloat(floatToHalf(0.0f)), 0.0f);
    QVERIFY(std::abs(halfToFloat(floatToHalf(3.25f)) - 3.25f) < 0.01f);
    QVERIFY(std::abs(halfToFloat(0x0001) - 5.96e-8f) < 1e-9f); // 非规格化
}

void TestHdr::srgbEncoding()
{
    QCOMPARE(linearToSrgb(0.0f), 0.0f);
    QCOMPARE(linearToSrgb(1.0f), 1.0f);
    QVERIFY(std::abs(linearToSrgb(0.2140f) - 0.5f) < 0.01f); // 中灰
    QCOMPARE(linearToSrgb(5.0f), 1.0f);
}

void TestHdr::sdrContentUnchanged()
{
    // SDR 范围内（≤1.0）的像素两种策略都应接近直接 sRGB 编码；纯白仍是 255
    const auto px = row({1.0f, 1.0f, 1.0f, 0.2140f, 0.2140f, 0.2140f, 0.0f, 0.0f, 0.0f});
    for (HdrStrategy s : {HdrStrategy::PreserveHighlights, HdrStrategy::PreserveBrightness}) {
        const QImage img = toneMapScRgb(px.data(), 3, 1, static_cast<int>(px.size() * 2), 80.0f, s);
        QCOMPARE(img.pixel(2, 0), qRgb(0, 0, 0));
        const int gray = qRed(img.pixel(1, 0));
        QVERIFY2(gray >= 118 && gray <= 136, qPrintable(QString::number(gray)));
        const int white = qRed(img.pixel(0, 0));
        QVERIFY2(white >= 225, qPrintable(QString::number(white))); // 保高光会把纯白稍压暗，但不发灰
    }
}

void TestHdr::highlightsCompressedNotClipped()
{
    // 1.0、2.0、3.5 倍 SDR 白：保高光时单调递增且不全部饱和（不过曝）
    const auto px = row({1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f, 3.5f, 3.5f, 3.5f});
    const QImage img = toneMapScRgb(px.data(), 3, 1, static_cast<int>(px.size() * 2), 80.0f, HdrStrategy::PreserveHighlights);
    const int a = qRed(img.pixel(0, 0)), b = qRed(img.pixel(1, 0)), c = qRed(img.pixel(2, 0));
    QVERIFY(a < b && b < c);
    QVERIFY(c <= 255 && b < 255);
    // 彩色高光保持色相：红多于蓝
    const auto colored = row({3.0f, 1.0f, 0.5f});
    const QImage ci = toneMapScRgb(colored.data(), 1, 1, 8, 80.0f, HdrStrategy::PreserveHighlights);
    QVERIFY(qRed(ci.pixel(0, 0)) > qGreen(ci.pixel(0, 0)) && qGreen(ci.pixel(0, 0)) > qBlue(ci.pixel(0, 0)));
}

void TestHdr::brightnessStrategyClips()
{
    const auto px = row({1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f});
    const QImage img = toneMapScRgb(px.data(), 2, 1, static_cast<int>(px.size() * 2), 80.0f, HdrStrategy::PreserveBrightness);
    QCOMPARE(img.pixel(0, 0), qRgb(255, 255, 255));
    QCOMPARE(img.pixel(1, 0), qRgb(255, 255, 255)); // 超出部分截断
}

void TestHdr::sdrWhiteLevelScales()
{
    // SDR 白电平 160 尼特时，2.0 的像素才是 SDR 白
    const auto px = row({2.0f, 2.0f, 2.0f, 1.0f, 1.0f, 1.0f});
    const QImage img = toneMapScRgb(px.data(), 2, 1, static_cast<int>(px.size() * 2), 160.0f, HdrStrategy::PreserveBrightness);
    QCOMPARE(img.pixel(0, 0), qRgb(255, 255, 255));
    const int half = qRed(img.pixel(1, 0));
    QVERIFY2(half >= 180 && half <= 195, qPrintable(QString::number(half))); // 线性 0.5 → sRGB 188
}

QTEST_APPLESS_MAIN(TestHdr)
#include "tst_hdr.moc"
