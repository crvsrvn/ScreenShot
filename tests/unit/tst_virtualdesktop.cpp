// REQ: CAP-02 NFR-07 NFR-08 TST-08
// 覆盖 R-03（混合缩放坐标精度）与 R-16（合成多屏）的核心算法部分
#include "core/geometry/VirtualDesktop.h"
#include "support/TestSupport.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakePlatform.h"

#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestVirtualDesktop : public QObject {
    Q_OBJECT
private slots:
    void cropWithinOneScreen();
    void cropAcrossMixedScaleScreens();
    void cropOutsideIsBlack();
    void pixelAtAndScreenAt();
    void logicalPhysicalRoundTrip_data();
    void logicalPhysicalRoundTrip();
    void negativeCoordinates();

private:
    VirtualDesktop makeDesktop(QList<ScreenInfo> screens)
    {
        FakeScreenCapture capture(std::move(screens));
        return VirtualDesktop(capture.captureAll());
    }
    QImage expectedPattern(const VirtualDesktop& d, const QRect& region)
    {
        QImage img(region.size(), QImage::Format_ARGB32);
        img.fill(Qt::black);
        const auto& snaps = d.snapshots();
        for (int y = 0; y < region.height(); ++y) {
            for (int x = 0; x < region.width(); ++x) {
                const QPoint p = region.topLeft() + QPoint(x, y);
                for (int i = 0; i < snaps.size(); ++i) {
                    const QRect g = snaps[i].screen.physicalGeometry;
                    if (g.contains(p))
                        img.setPixel(x, y, FakeScreenCapture::patternPixel(i, p.x() - g.x(), p.y() - g.y()));
                }
            }
        }
        return img;
    }
};

void TestVirtualDesktop::cropWithinOneScreen()
{
    const VirtualDesktop d = makeDesktop(defaultMixedScaleLayout());
    const QRect region(100, 200, 300, 150);
    const QImage actual = d.crop(region);
    QCOMPARE(actual.size(), region.size());
    QCOMPARE(actual.devicePixelRatio(), 1.0);
    SS_VERIFY_IMAGE(actual, expectedPattern(d, region), CompareOptions{}, "crop_one_screen");
}

void TestVirtualDesktop::cropAcrossMixedScaleScreens()
{
    // 左屏 150%、右屏 100%，选区横跨接缝：输出必须与两屏原始像素逐像素一致（AC-01 的算法部分）
    const VirtualDesktop d = makeDesktop(defaultMixedScaleLayout());
    const QRect region(3700, 1000, 300, 200);
    const QImage actual = d.crop(region);
    SS_VERIFY_IMAGE(actual, expectedPattern(d, region), CompareOptions{}, "crop_across_screens");
    // 接缝两侧像素来自不同显示器
    QCOMPARE(actual.pixel(139, 0), FakeScreenCapture::patternPixel(0, 3839, 1000));
    QCOMPARE(actual.pixel(140, 0), FakeScreenCapture::patternPixel(1, 0, 1000));
}

void TestVirtualDesktop::cropOutsideIsBlack()
{
    const VirtualDesktop d = makeDesktop(defaultMixedScaleLayout());
    const QRect region(3800, 2100, 100, 100); // 右下角超出两屏
    const QImage actual = d.crop(region);
    SS_VERIFY_IMAGE(actual, expectedPattern(d, region), CompareOptions{}, "crop_outside");
    QCOMPARE(actual.pixel(99, 99), qRgb(0, 0, 0));
}

void TestVirtualDesktop::pixelAtAndScreenAt()
{
    const VirtualDesktop d = makeDesktop(defaultMixedScaleLayout());
    QVERIFY(d.screenAt(QPoint(0, 0)).has_value());
    QCOMPARE(d.screenAt(QPoint(0, 0))->id, QStringLiteral("FAKE-A"));
    QCOMPARE(d.screenAt(QPoint(3840, 0))->id, QStringLiteral("FAKE-B"));
    QVERIFY(!d.screenAt(QPoint(-1, 0)).has_value());
    QVERIFY(!d.screenAt(QPoint(7680, 0)).has_value());
    QCOMPARE(*d.pixelAt(QPoint(3839, 2159)), FakeScreenCapture::patternPixel(0, 3839, 2159));
    QCOMPARE(*d.pixelAt(QPoint(3840, 5)), FakeScreenCapture::patternPixel(1, 0, 5));
    QCOMPARE(d.bounds(), QRect(0, 0, 7680, 2160));
}

void TestVirtualDesktop::logicalPhysicalRoundTrip_data()
{
    QTest::addColumn<double>("scale");
    QTest::newRow("100%") << 1.0;
    QTest::newRow("125%") << 1.25;
    QTest::newRow("150%") << 1.5;
    QTest::newRow("175%") << 1.75;
    QTest::newRow("200%") << 2.0;
    QTest::newRow("300%") << 3.0;
}

void TestVirtualDesktop::logicalPhysicalRoundTrip()
{
    QFETCH(double, scale);
    ScreenInfo s{QStringLiteral("S"), QRect(-2560, -100, 2560, 1440), scale, true, false};
    const QPointF origin(-2560, -100);
    // 每个物理像素 → 逻辑 → 物理 必须回到自身（零偏差）
    for (int x = -2560; x < 0; x += 7) {
        for (int y = -100; y < 1340; y += 13) {
            const QPoint phys(x, y);
            const QPointF logical = VirtualDesktop::physicalToLogical(phys, s, origin);
            const QPointF rel = (logical - origin) * scale;
            const QPoint back = s.physicalGeometry.topLeft() + QPoint(qRound(rel.x()), qRound(rel.y()));
            if (back != phys)
                QFAIL(qPrintable(QStringLiteral("round trip (%1,%2) → (%3,%4)").arg(x).arg(y).arg(back.x()).arg(back.y())));
        }
    }
}

void TestVirtualDesktop::negativeCoordinates()
{
    QList<ScreenInfo> screens{
        ScreenInfo{QStringLiteral("L"), QRect(-2560, 0, 2560, 1440), 1.25, false, false},
        ScreenInfo{QStringLiteral("R"), QRect(0, 0, 2560, 1440), 1.25, true, false},
    };
    const VirtualDesktop d = makeDesktop(screens);
    QCOMPARE(d.bounds(), QRect(-2560, 0, 5120, 1440));
    const QRect region(-50, 10, 100, 20);
    SS_VERIFY_IMAGE(d.crop(region), expectedPattern(d, region), CompareOptions{}, "crop_negative");
}

QTEST_MAIN(TestVirtualDesktop)
#include "tst_virtualdesktop.moc"
