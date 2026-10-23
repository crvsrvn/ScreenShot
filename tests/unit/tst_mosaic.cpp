// REQ: NFR-05 ANN-29
#include "core/fx/Mosaic.h"
#include "core/util/Clock.h"
#include "support/TestSupport.h"

#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestMosaic : public QObject {
    Q_OBJECT
private slots:
    void blockAverage();
    void partialBlocksAtEdge();
    void fourKWithinBudget();
};

void TestMosaic::blockAverage()
{
    QImage img(8, 8, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    // 左上 4×4 块：一半白一半黑 → 平均 128（四舍五入）
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 2; ++x)
            img.setPixel(x, y, qRgb(255, 255, 255));
    const QImage out = fx::mosaic(img, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            QCOMPARE(out.pixel(x, y), qRgb(128, 128, 128));
    QCOMPARE(out.pixel(7, 7), qRgb(0, 0, 0));
}

void TestMosaic::partialBlocksAtEdge()
{
    QImage img(10, 7, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgb(10, 20, 30));
    const QImage out = fx::mosaic(img, 4);
    QCOMPARE(out.size(), img.size());
    SS_VERIFY_IMAGE(out, img, CompareOptions{}, "mosaic_constant");
}

void TestMosaic::fourKWithinBudget()
{
    QImage src(3840, 2160, QImage::Format_ARGB32_Premultiplied);
    src.fill(qRgb(1, 2, 3));
    fx::mosaic(src, 12);
    double best = 1e9;
    for (int i = 0; i < 3; ++i) {
        const qint64 t0 = monotonicNs();
        const QImage out = fx::mosaic(src, 12);
        best = std::min(best, nsToMs(monotonicNs() - t0));
        QVERIFY(!out.isNull());
    }
    qInfo("4K 马赛克（块 12）最好成绩 %.1f ms", best);
#ifdef NDEBUG
    QVERIFY2(best <= 100.0, qPrintable(QStringLiteral("NFR-05 要求 ≤ 100 ms，实际 %1 ms").arg(best)));
#endif
}

QTEST_GUILESS_MAIN(TestMosaic)
#include "tst_mosaic.moc"
