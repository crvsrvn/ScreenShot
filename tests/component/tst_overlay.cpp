// REQ: CAP-02 CAP-03 CAP-10 NFR-07 NFR-08 TST-02
// 覆盖窗口：渲染内容、逻辑→物理坐标换算（R-03）、鼠标与键盘交互
#include "app/CaptureSession.h"
#include "app/OverlayWindow.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestOverlay : public QObject {
    Q_OBJECT
private slots:
    void renderShowsSnapshotPixelsInsideSelection();
    void logicalToPhysicalIsExact_data();
    void logicalToPhysicalIsExact();
    void mouseDragSelectsPhysicalRect();
    void escapeCancels();
    void enterAccepts();
    void rightClickClearsThenCancels();
};

void TestOverlay::renderShowsSnapshotPixelsInsideSelection()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    s.setSelection(QRect(100, 100, 50, 40));
    OverlayWindow* w = s.overlayAt(QPoint(100, 100));
    QVERIFY(w);
    const QImage rendered = w->renderToImage();
    QCOMPARE(rendered.size(), QSize(3840, 2160));
    // 选区内部像素与快照一致，选区外被压暗
    const QImage snap = h.screenCapture->patternImage(0, QSize(3840, 2160));
    const QImage inside = rendered.copy(101, 101, 48, 38).convertToFormat(QImage::Format_RGB32);
    const QImage expectedInside = snap.copy(101, 101, 48, 38);
    SS_VERIFY_IMAGE(inside, expectedInside, CompareOptions{}, "overlay_inside_selection");
    const QRgb outside = rendered.pixel(500, 500);
    const QRgb original = snap.pixel(500, 500);
    QVERIFY(qRed(outside) < qRed(original) || qGreen(outside) < qGreen(original) || qBlue(outside) < qBlue(original));
    s.cancel();
}

void TestOverlay::logicalToPhysicalIsExact_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<QPoint>("origin");
    QTest::newRow("100% 原点") << 1.0 << QPoint(0, 0);
    QTest::newRow("125% 负坐标") << 1.25 << QPoint(-2560, -300);
    QTest::newRow("150%") << 1.5 << QPoint(3840, 0);
    QTest::newRow("175%") << 1.75 << QPoint(1000, 2000);
    QTest::newRow("200%") << 2.0 << QPoint(0, 0);
}

void TestOverlay::logicalToPhysicalIsExact()
{
    QFETCH(double, scale);
    QFETCH(QPoint, origin);
    Platform p = makeFakePlatform(nullptr, {ScreenInfo{QStringLiteral("S"), QRect(origin, QSize(2560, 1440)), scale, true, false}});
    CaptureSession s(p.screenCapture.get());
    s.prepareOverlays();
    OverlayWindow* w = s.overlays().first();
    // Qt 把物理像素 (x,y) 交给控件时是 (x/scale, y/scale)；换算回去必须零偏差
    for (int x = 0; x < 2560; x += 3) {
        for (int y = 0; y < 1440; y += 97) {
            const QPointF logical(x / scale, y / scale);
            const QPoint phys = w->toPhysical(logical);
            if (phys != origin + QPoint(x, y))
                QFAIL(qPrintable(QStringLiteral("(%1,%2) → (%3,%4)").arg(x).arg(y).arg(phys.x() - origin.x()).arg(phys.y() - origin.y())));
        }
    }
}

void TestOverlay::mouseDragSelectsPhysicalRect()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    OverlayWindow* w = s.overlayAt(QPoint(0, 0)); // 150% 屏
    QVERIFY(w);
    w->resize(2560, 1440);
    // 逻辑 (100, 100) → 物理 (150, 150)；逻辑 (200, 160) → 物理 (300, 240)
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseMove(w, QPoint(200, 160));
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(200, 160));
    QCOMPARE(s.selection(), QRect(QPoint(150, 150), QPoint(300, 240)));
    QVERIFY(!s.dragging());
    s.cancel();
}

void TestOverlay::escapeCancels()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    QSignalSpy cancelled(&s, &CaptureSession::cancelled);
    QTest::keyClick(s.overlays().first(), Qt::Key_Escape);
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!s.isActive());
}

void TestOverlay::enterAccepts()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    s.setSelection(QRect(10, 10, 20, 20));
    QSignalSpy captured(&s, &CaptureSession::captured);
    QTest::keyClick(s.overlays().first(), Qt::Key_Return);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(0).value<QImage>().size(), QSize(20, 20));
}

void TestOverlay::rightClickClearsThenCancels()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    OverlayWindow* w = s.overlayAt(QPoint(0, 0));
    s.setSelection(QRect(10, 10, 20, 20));
    QTest::mouseClick(w, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(s.selection().isEmpty());
    QVERIFY(s.isActive());
    QTest::mouseClick(w, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(!s.isActive());
}

QTEST_MAIN(TestOverlay)
#include "tst_overlay.moc"
