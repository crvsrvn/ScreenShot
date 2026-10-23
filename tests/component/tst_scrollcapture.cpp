// REQ: CAP-60 CAP-61 CAP-62 CAP-64 CAP-66 CAP-69 CAP-72 CAP-73 CAP-75 CAP-76
// 滚动截图会话：合成屏幕上的“可滚动文档”，手动 / 自动滚动，拼接结果与文档一致；跨缩放拒绝；取消；输出
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/PinManager.h"
#include "app/ScrollCaptureSession.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakeInputSimulator.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QPainter>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace ss;
using namespace ss::testing;

namespace {

// 合成屏幕：在 (100,100) 处有一个 400×300 的“可滚动视口”，显示长文档的一部分
struct ScrollableScreen {
    FakeScreenCapture* capture;
    QImage document;
    QRect viewport{100, 100, 400, 300};
    int offset = 0;
    QImage base;

    explicit ScrollableScreen(FakeScreenCapture* c)
        : capture(c)
    {
        document = QImage(400, 2400, QImage::Format_RGB32);
        document.fill(Qt::white);
        QRandomGenerator rng(5);
        for (int y = 0; y < document.height(); ++y) {
            if (y % 6 == 0)
                continue;
            auto* line = reinterpret_cast<QRgb*>(document.scanLine(y));
            int x = static_cast<int>(rng.bounded(8));
            while (x < 400) {
                const int len = static_cast<int>(rng.bounded(3, 30));
                const QRgb col = qRgb(static_cast<int>(rng.bounded(180)), static_cast<int>(rng.bounded(180)), static_cast<int>(rng.bounded(180)));
                for (int i = 0; i < len && x < 400; ++i, ++x)
                    line[x] = col;
                x += static_cast<int>(rng.bounded(2, 15));
            }
        }
        base = FakeScreenCapture::patternImage(0, QSize(3840, 2160));
        render();
    }
    void render()
    {
        QImage screen = base.copy();
        QPainter p(&screen);
        p.drawImage(viewport.topLeft(), document, QRect(0, offset, 400, 300));
        capture->setImage(0, screen);
    }
    void scrollBy(int rows)
    {
        offset = std::clamp(offset + rows, 0, document.height() - 300);
        render();
    }
};

struct Fixture {
    FakePlatformHandles h;
    Platform p;
    Settings settings;
    std::unique_ptr<ScrollableScreen> screen;
    std::unique_ptr<ScrollCaptureSession> session;

    Fixture()
        : p(makeFakePlatform(&h))
    {
        screen = std::make_unique<ScrollableScreen>(h.screenCapture);
        settings.setValue(QStringLiteral("scroll.stableFrames"), 2);
        session = std::make_unique<ScrollCaptureSession>(p.screenCapture.get(), p.input.get(), settings);
    }
    void ticks(int n)
    {
        for (int i = 0; i < n; ++i)
            session->tick();
    }
};

} // namespace

class TestScrollCapture : public QObject {
    Q_OBJECT
private slots:
    void manualScrollStitchesDocument();
    void autoScrollSendsWheelAtEdgeAndStops();
    void rejectsUnreliableFrameAndReports();
    void refusesRegionAcrossScales();
    void cancelAndFinishOutputs();
    void captureSessionHandsOffRegion();
};

void TestScrollCapture::manualScrollStitchesDocument()
{
    Fixture f;
    QVERIFY(f.session->begin(f.screen->viewport, false));
    QCOMPARE(f.session->state(), ScrollCaptureSession::State::Running);
    f.ticks(2); // 首帧稳定
    QCOMPARE(f.session->stitchedHeight(), 300);
    int total = 0;
    for (int step : {80, 150, 60, 200, 120}) {
        f.screen->scrollBy(step);
        total += step;
        f.ticks(2); // 一帧“在动”（与上次不同），再一帧稳定
        QVERIFY2(f.session->lastMatchOk(), qPrintable(f.session->lastReason()));
        QCOMPARE(f.session->stitchedHeight(), 300 + total);
    }
    QSignalSpy finished(f.session.get(), &ScrollCaptureSession::finished);
    const QImage out = f.session->finish();
    QCOMPARE(finished.count(), 1);
    SS_VERIFY_IMAGE(out, f.screen->document.copy(0, 0, 400, 300 + total), CompareOptions{}, "scroll_manual");
    QVERIFY(!f.session->thumbnail(100, 300).isNull());
}

void TestScrollCapture::autoScrollSendsWheelAtEdgeAndStops()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("scroll.speed"), 2);
    f.settings.setValue(QStringLiteral("scroll.endFrames"), 3);
    // 滚轮作用到合成文档：每格 60 行
    f.h.input->setOnWheel([&](QPoint, int notches, bool) { f.screen->scrollBy(-notches * 60); });
    QVERIFY(f.session->begin(f.screen->viewport, false));
    f.session->setAutoScroll(true);
    f.ticks(2); // 首帧 → 触发第一次滚动
    QVERIFY(!f.h.input->wheels().isEmpty());
    const auto& w = f.h.input->wheels().first();
    QCOMPARE(w.at, QPoint(f.screen->viewport.right() - 4, f.screen->viewport.center().y())); // CAP-69：区域边缘
    QCOMPARE(w.notches, -2);
    // 持续节拍直到到底
    for (int i = 0; i < 200 && !f.session->reachedEnd(); ++i)
        f.session->tick();
    QVERIFY(f.session->reachedEnd());
    const QImage out = f.session->finish();
    QCOMPARE(out.height(), f.screen->document.height());
    SS_VERIFY_IMAGE(out, f.screen->document, CompareOptions{}, "scroll_auto");
}

void TestScrollCapture::rejectsUnreliableFrameAndReports()
{
    Fixture f;
    QVERIFY(f.session->begin(f.screen->viewport, false));
    f.ticks(2);
    // 滚动过快：没有重叠
    f.screen->scrollBy(900);
    f.ticks(2);
    QVERIFY(!f.session->lastMatchOk());
    QVERIFY(!f.session->lastReason().isEmpty());
    QCOMPARE(f.session->rejectedFrames(), 1);
    QCOMPARE(f.session->stitchedHeight(), 300); // 没有拼出错图（CAP-66）
    // 滚回来一点再继续能恢复
    f.screen->scrollBy(-900 + 100);
    f.ticks(2);
    QVERIFY(f.session->lastMatchOk());
    QCOMPARE(f.session->stitchedHeight(), 400);
}

void TestScrollCapture::refusesRegionAcrossScales()
{
    Fixture f;
    QVERIFY(!f.session->begin(QRect(3700, 100, 300, 200), false)); // 横跨 150% 与 100% 两屏
    QVERIFY(!f.session->lastError().isEmpty());
    QCOMPARE(f.session->state(), ScrollCaptureSession::State::Idle);
    QVERIFY(!f.session->begin(QRect(9000, 9000, 10, 10), false));
}

void TestScrollCapture::cancelAndFinishOutputs()
{
    Fixture f;
    QVERIFY(f.session->begin(f.screen->viewport, false));
    QSignalSpy cancelled(f.session.get(), &ScrollCaptureSession::cancelled);
    f.session->cancel();
    QCOMPARE(cancelled.count(), 1);
    QCOMPARE(f.session->state(), ScrollCaptureSession::State::Idle);
    // App 层：完成后请求输出 → 复制到剪贴板并写入历史
    Settings s;
    FakePlatformHandles h2;
    App app(makeFakePlatform(&h2), s);
    ScrollableScreen sc(h2.screenCapture);
    QVERIFY(app.startScroll(sc.viewport, false));
    app.scrollSession()->tick();
    app.scrollSession()->tick();
    sc.scrollBy(100);
    app.scrollSession()->tick();
    app.scrollSession()->tick();
    QCOMPARE(app.scrollSession()->finish().height(), 400);
    emit app.scrollSession()->outputRequested(QStringLiteral("copy"));
    QCOMPARE(QApplication::clipboard()->image().height(), 400);
    QCOMPARE(app.history().size(), 1);
    QApplication::processEvents();
    QVERIFY(!app.scrollSession());
}

void TestScrollCapture::captureSessionHandsOffRegion()
{
    // CAP-60：截图界面选区后按钮 / Ctrl+L 把区域交给滚动截图
    Settings s;
    FakePlatformHandles h;
    App app(makeFakePlatform(&h), s);
    ScrollableScreen sc(h.screenCapture);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(sc.viewport);
    QVERIFY(app.session().toolbar()->button(QStringLiteral("scroll")));
    QSignalSpy requested(&app.session(), &CaptureSession::scrollRequested);
    QKeyEvent e(QEvent::KeyPress, Qt::Key_L, Qt::ControlModifier);
    app.session().handleKeyPress(&e);
    QCOMPARE(requested.count(), 1);
    QVERIFY(!app.session().isActive());
    QVERIFY(app.scrollSession());
    QCOMPARE(app.scrollSession()->region(), sc.viewport);
    app.scrollSession()->cancel();
}

QTEST_MAIN(TestScrollCapture)
#include "tst_scrollcapture.moc"
