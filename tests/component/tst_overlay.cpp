// REQ: CAP-02 CAP-03 CAP-10 CAP-11 CAP-21 CAP-40 NFR-07 NFR-08 TST-02
// 覆盖窗口：渲染内容、逻辑→物理坐标换算（R-03）、鼠标与键盘交互、放大镜与高亮绘制
#include "app/CaptureSession.h"
#include "app/OverlayWindow.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

using namespace ss;
using namespace ss::testing;

namespace {
struct Fixture {
    FakePlatformHandles h;
    Platform p;
    Settings settings;
    CaptureHistory history{5};
    std::unique_ptr<CaptureSession> session;
    Fixture(QList<ScreenInfo> screens = defaultMixedScaleLayout())
        : p(makeFakePlatform(&h, std::move(screens)))
    {
        session = std::make_unique<CaptureSession>(p.screenCapture.get(), p.windowDetector.get(), p.elementDetector.get(), settings);
        session->setHistory(&history);
        session->prepareOverlays();
    }
};
} // namespace

class TestOverlay : public QObject {
    Q_OBJECT
private slots:
    void renderShowsSnapshotPixelsInsideSelection();
    void renderHighlightKeepsOriginalPixels();
    void renderMagnifierWhenDetecting();
    void renderHistoryImage();
    void logicalToPhysicalIsExact_data();
    void logicalToPhysicalIsExact();
    void mouseDragSelectsPhysicalRect();
    void escapeCancels();
    void enterAccepts();
    void rightClickClearsThenCancels();
};

void TestOverlay::renderShowsSnapshotPixelsInsideSelection()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(100, 100, 50, 40));
    OverlayWindow* w = f.session->overlayAt(QPoint(100, 100));
    QVERIFY(w);
    const QImage rendered = w->renderToImage();
    QCOMPARE(rendered.size(), QSize(3840, 2160));
    const QImage snap = FakeScreenCapture::patternImage(0, QSize(3840, 2160));
    // 避开边框与锚点（锚点 7 逻辑像素见方，跨在边线上）
    const QImage inside = rendered.copy(110, 110, 30, 20).convertToFormat(QImage::Format_RGB32);
    SS_VERIFY_IMAGE(inside, snap.copy(110, 110, 30, 20), CompareOptions{}, "overlay_inside_selection");
    const QRgb outside = rendered.pixel(500, 500);
    const QRgb original = snap.pixel(500, 500);
    QVERIFY(qRed(outside) < qRed(original) || qGreen(outside) < qGreen(original) || qBlue(outside) < qBlue(original));
    f.session->cancel();
}

void TestOverlay::renderHighlightKeepsOriginalPixels()
{
    Fixture f;
    f.h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("w"), QStringLiteral("a.exe"), QRect(300, 300, 200, 200), 0}});
    f.settings.setValue(QStringLiteral("capture.detectMode"), QStringLiteral("window"));
    f.settings.setValue(QStringLiteral("capture.magnifierAuto"), false);
    QVERIFY(f.session->start(1, false));
    f.session->hoverAt(QPoint(400, 400));
    OverlayWindow* w = f.session->overlayAt(QPoint(400, 400));
    const QImage rendered = w->renderToImage();
    const QImage snap = FakeScreenCapture::patternImage(0, QSize(3840, 2160));
    // 高亮窗口内部像素不压暗（避开 2 像素边框）
    SS_VERIFY_IMAGE(rendered.copy(310, 310, 100, 100).convertToFormat(QImage::Format_RGB32), snap.copy(310, 310, 100, 100), CompareOptions{}, "overlay_highlight");
    f.session->cancel();
}

void TestOverlay::renderMagnifierWhenDetecting()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->hoverAt(QPoint(600, 600));
    QVERIFY(f.session->magnifierVisible());
    OverlayWindow* w = f.session->overlayAt(QPoint(600, 600));
    const QImage withMagnifier = w->renderToImage();
    f.settings.setValue(QStringLiteral("capture.magnifierAuto"), false);
    QVERIFY(!f.session->magnifierVisible());
    const QImage without = w->renderToImage();
    const auto cmp = compareImages(withMagnifier, without);
    QVERIFY(cmp.differingPixels > 1000); // 放大镜面板确实画出来了
    f.session->setAltHeld(true);
    QVERIFY(f.session->magnifierVisible());
    f.session->cancel();
}

void TestOverlay::renderHistoryImage()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(200, 200, 40, 30));
    f.session->accept();
    // 换一张不同的画面后回放历史：显示的是历史像素
    f.h.screenCapture->setImage(0, QImage(3840, 2160, QImage::Format_RGB32));
    QVERIFY(f.session->start(2, false));
    f.session->historyOlder();
    QCOMPARE(f.session->state(), CaptureSession::State::History);
    OverlayWindow* w = f.session->overlayAt(QPoint(200, 200));
    const QImage rendered = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    const QImage expected = FakeScreenCapture::patternImage(0, QSize(3840, 2160)).copy(210, 210, 20, 10);
    SS_VERIFY_IMAGE(rendered.copy(210, 210, 20, 10), expected, CompareOptions{}, "overlay_history");
    f.session->cancel();
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
    Fixture f({ScreenInfo{QStringLiteral("S"), QRect(origin, QSize(2560, 1440)), scale, true, false}});
    OverlayWindow* w = f.session->overlays().first();
    for (int x = 0; x < 2560; x += 3) {
        for (int y = 0; y < 1440; y += 97) {
            const QPoint phys = w->toPhysical(QPointF(x / scale, y / scale));
            if (phys != origin + QPoint(x, y))
                QFAIL(qPrintable(QStringLiteral("(%1,%2) → (%3,%4)").arg(x).arg(y).arg(phys.x() - origin.x()).arg(phys.y() - origin.y())));
        }
    }
}

void TestOverlay::mouseDragSelectsPhysicalRect()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    OverlayWindow* w = f.session->overlayAt(QPoint(0, 0)); // 150% 屏
    w->resize(2560, 1440);
    QTest::mousePress(w, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
    QTest::mouseMove(w, QPoint(150, 130));
    QTest::mouseMove(w, QPoint(200, 160));
    QTest::mouseRelease(w, Qt::LeftButton, Qt::NoModifier, QPoint(200, 160));
    QCOMPARE(f.session->selection(), QRect(QPoint(150, 150), QPoint(300, 240)));
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    f.session->cancel();
}

void TestOverlay::escapeCancels()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    QSignalSpy cancelled(f.session.get(), &CaptureSession::cancelled);
    QTest::keyClick(f.session->overlays().first(), Qt::Key_Escape);
    QCOMPARE(cancelled.count(), 1);
}

void TestOverlay::enterAccepts()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(10, 10, 20, 20));
    QSignalSpy captured(f.session.get(), &CaptureSession::captured);
    QTest::keyClick(f.session->overlays().first(), Qt::Key_Return);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(0).value<QImage>().size(), QSize(20, 20));
}

void TestOverlay::rightClickClearsThenCancels()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    OverlayWindow* w = f.session->overlayAt(QPoint(0, 0));
    f.session->setSelection(QRect(10, 10, 20, 20));
    QTest::mouseClick(w, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(f.session->selection().isEmpty());
    QVERIFY(f.session->isActive());
    QTest::mouseClick(w, Qt::RightButton, Qt::NoModifier, QPoint(5, 5));
    QVERIFY(!f.session->isActive());
}

QTEST_MAIN(TestOverlay)
#include "tst_overlay.moc"
