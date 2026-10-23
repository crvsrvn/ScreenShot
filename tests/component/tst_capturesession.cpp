// REQ: CAP-02 CAP-10 OUT-01 NFR-07 NFR-08 TST-02 TST-03
// 组件测试（无显示器）：会话在合成多屏布局上冻结、框选、裁剪、复制
#include "app/CaptureSession.h"
#include "app/OverlayWindow.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QSignalSpy>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestCaptureSession : public QObject {
    Q_OBJECT
private slots:
    void startFreezesAllScreens();
    void selectionAcrossScreensCropsExactly();
    void dragNormalizesAndIncludesBothEnds();
    void cancelReleasesSnapshot();
    void acceptWithoutSelectionKeepsSession();
    void timingsAreRecorded();
    void layoutChangeRecreatesOverlays();
};

void TestCaptureSession::startFreezesAllScreens()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    s.prepareOverlays();
    QCOMPARE(s.overlays().size(), 2);
    QSignalSpy started(&s, &CaptureSession::started);
    QVERIFY(s.start(1, false));
    QVERIFY(s.isActive());
    QCOMPARE(started.count(), 1);
    QCOMPARE(s.desktop().snapshots().size(), 2);
    QVERIFY(!s.start(2, false)); // 进行中不能再开始
    s.cancel();
}

void TestCaptureSession::selectionAcrossScreensCropsExactly()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    const QRect region(3800, 100, 80, 60); // 横跨 150% 与 100% 两屏
    s.setSelection(region);
    QSignalSpy captured(&s, &CaptureSession::captured);
    const QImage img = s.accept();
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(1).toRect(), region);
    QVERIFY(!s.isActive());
    QImage expected(region.size(), QImage::Format_ARGB32);
    for (int y = 0; y < 60; ++y) {
        for (int x = 0; x < 80; ++x) {
            const int px = 3800 + x;
            expected.setPixel(x, y, px < 3840 ? FakeScreenCapture::patternPixel(0, px, 100 + y)
                                               : FakeScreenCapture::patternPixel(1, px - 3840, 100 + y));
        }
    }
    SS_VERIFY_IMAGE(img, expected, CompareOptions{}, "session_cross_screen");
}

void TestCaptureSession::dragNormalizesAndIncludesBothEnds()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    QSignalSpy changed(&s, &CaptureSession::selectionChanged);
    s.beginDrag(QPoint(500, 400));
    QVERIFY(s.dragging());
    s.dragTo(QPoint(300, 450));
    s.endDrag(QPoint(300, 450));
    QVERIFY(!s.dragging());
    QCOMPARE(s.selection(), QRect(QPoint(300, 400), QPoint(500, 450)));
    QCOMPARE(s.selection().size(), QSize(201, 51));
    QVERIFY(changed.count() >= 3);
    s.cancel();
}

void TestCaptureSession::cancelReleasesSnapshot()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    QSignalSpy cancelled(&s, &CaptureSession::cancelled);
    s.cancel();
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!s.isActive());
    QVERIFY(s.desktop().isEmpty());
    for (auto* w : s.overlays())
        QVERIFY(!w->isVisible());
}

void TestCaptureSession::acceptWithoutSelectionKeepsSession()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(1, false));
    QVERIFY(s.accept().isNull());
    QVERIFY(s.isActive());
    s.cancel();
}

void TestCaptureSession::timingsAreRecorded()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    QVERIFY(s.start(12345, false));
    const auto t = s.timings();
    QCOMPARE(t.triggerNs, 12345);
    QVERIFY(t.captureDoneNs > 0);
    QVERIFY(t.shownNs >= t.captureDoneNs);
    QVERIFY(!t.complete()); // 未显示窗口就没有首次绘制
    s.cancel();
}

void TestCaptureSession::layoutChangeRecreatesOverlays()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    CaptureSession s(p.screenCapture.get());
    s.prepareOverlays();
    QCOMPARE(s.overlays().size(), 2);
    h.screenCapture->setScreens({ScreenInfo{QStringLiteral("ONLY"), QRect(0, 0, 1920, 1080), 1.0, true, false}});
    QVERIFY(s.start(1, false));
    QCOMPARE(s.overlays().size(), 1);
    QCOMPARE(s.overlays().first()->screenInfo().id, QStringLiteral("ONLY"));
    s.cancel();
}

QTEST_MAIN(TestCaptureSession)
#include "tst_capturesession.moc"
