// REQ: NFR-05 ANN-29
// 覆盖 R-06（CPU 高斯模糊）：正确性与 4K 耗时
#include "core/fx/Blur.h"
#include "core/util/Clock.h"
#include "support/TestSupport.h"

#include <QPainter>
#include <QTest>

using namespace ss;
using namespace ss::testing;

namespace {

QImage noise(int w, int h, quint32 seed = 7)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            seed = seed * 1664525u + 1013904223u;
            line[x] = 0xFF000000u | (seed >> 8);
        }
    }
    return img;
}

} // namespace

class TestBlur : public QObject {
    Q_OBJECT
private slots:
    void constantImageStaysConstant();
    void boxBlurMatchesNaive();
    void gaussianCloseToReference();
    void alphaIsPreserved();
    void fourKWithinBudget();
};

void TestBlur::constantImageStaysConstant()
{
    QImage img(200, 120, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(10, 200, 30));
    const QImage out = fx::gaussianBlur(img, 15);
    SS_VERIFY_IMAGE(out, img, CompareOptions{}, "blur_constant");
}

void TestBlur::boxBlurMatchesNaive()
{
    const QImage src = noise(64, 40);
    const int r = 3;
    const QImage out = fx::boxBlur(src, r);
    // 朴素二维盒式平均（边缘重复），每通道四舍五入两次与分离实现的差 ≤ 1
    QImage expected(src.size(), src.format());
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            int sum[4] = {0, 0, 0, 0};
            for (int dy = -r; dy <= r; ++dy) {
                const int sy = std::clamp(y + dy, 0, src.height() - 1);
                const auto* line = reinterpret_cast<const quint32*>(src.constScanLine(sy));
                for (int dx = -r; dx <= r; ++dx) {
                    const quint32 p = line[std::clamp(x + dx, 0, src.width() - 1)];
                    sum[0] += p & 0xFF;
                    sum[1] += (p >> 8) & 0xFF;
                    sum[2] += (p >> 16) & 0xFF;
                    sum[3] += p >> 24;
                }
            }
            const int n = (2 * r + 1) * (2 * r + 1);
            const quint32 v = static_cast<quint32>((sum[0] + n / 2) / n) | (static_cast<quint32>((sum[1] + n / 2) / n) << 8)
                | (static_cast<quint32>((sum[2] + n / 2) / n) << 16) | (static_cast<quint32>((sum[3] + n / 2) / n) << 24);
            reinterpret_cast<quint32*>(expected.scanLine(y))[x] = v;
        }
    }
    SS_VERIFY_IMAGE(out, expected, (CompareOptions{1, 0}), "box_vs_naive");
}

void TestBlur::gaussianCloseToReference()
{
    const QImage src = noise(96, 64);
    const int radius = 6;
    const QImage fast = fx::gaussianBlur(src, radius);
    const QImage ref = fx::referenceGaussianBlur(src, radius / 2.0);
    // 三次盒式模糊是近似，允许每通道差 ≤ 12，且超差像素 ≤ 1%
    SS_VERIFY_IMAGE(fast, ref, (CompareOptions{12, 96 * 64 / 100}), "gauss_vs_reference");
}

void TestBlur::alphaIsPreserved()
{
    QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.fillRect(30, 30, 40, 40, QColor(255, 0, 0, 255));
    p.end();
    const QImage out = fx::gaussianBlur(img, 8);
    QCOMPARE(qAlpha(out.pixel(0, 0)), 0);
    QVERIFY(qAlpha(out.pixel(50, 50)) > 250);
    QVERIFY(qAlpha(out.pixel(30, 50)) > 60 && qAlpha(out.pixel(30, 50)) < 200); // 边缘过渡
}

void TestBlur::fourKWithinBudget()
{
    const QImage src = noise(3840, 2160);
    fx::gaussianBlur(src, 10); // 预热
    double best = 1e9;
    for (int i = 0; i < 3; ++i) {
        const qint64 t0 = monotonicNs();
        const QImage out = fx::gaussianBlur(src, 10);
        best = std::min(best, nsToMs(monotonicNs() - t0));
        QVERIFY(!out.isNull());
    }
    qInfo("4K 高斯模糊（半径 10）最好成绩 %.1f ms", best);
#ifdef NDEBUG
    QVERIFY2(best <= 100.0, qPrintable(QStringLiteral("NFR-05 要求 ≤ 100 ms，实际 %1 ms").arg(best)));
#else
    qInfo("Debug 构建不检查耗时阈值");
#endif
}

QTEST_GUILESS_MAIN(TestBlur)
#include "tst_blur.moc"
