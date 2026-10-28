// REQ: GEN-10 CAP-07 CAP-14 CAP-19 CAP-20 CAP-22 CAP-25 CAP-42 CAP-43 CAP-71
// M6 通用 / 截图 P2：进程优先级、模式轮换、检测排除列表、尺寸单位、外观、放大镜外观、辅助线、
// 取消也进历史、历史目录、滚动区域控制
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OverlayWindow.h"
#include "app/PinWindow.h"
#include "app/ScrollCaptureSession.h"
#include "core/capture/Magnifier.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakeElementDetector.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#if defined(Q_OS_WIN)
#include "platform/windows/WinSystem.h"
#include <windows.h>
#endif

using namespace ss;
using namespace ss::testing;

namespace {

void key(CaptureSession& s, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    s.handleKeyPress(&e);
}

OverlayWindow* overlayAt(CaptureSession& s, QPoint physical)
{
    return s.overlayAt(physical);
}

int countColor(const QImage& img, QRgb color)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixel(x, y) == color)
                ++n;
    return n;
}

} // namespace

class TestM6B : public QObject {
    Q_OBJECT
private slots:
    void processPriority();
    void modeCycling();
    void detectExcludeAndConservative();
    void sizeUnitToggle();
    void appearanceSettings();
    void magnifierAppearance();
    void guides();
    void cancelledCapturesInHistory();
    void historyDirectory();
    void scrollRegionControl();
};

void TestM6B::processPriority()
{
#if defined(Q_OS_WIN)
    QVERIFY(win::setProcessPriority(QStringLiteral("aboveNormal")));
    QCOMPARE(GetPriorityClass(GetCurrentProcess()), static_cast<DWORD>(ABOVE_NORMAL_PRIORITY_CLASS));
    QVERIFY(win::setProcessPriority(QStringLiteral("normal")));
    QCOMPARE(GetPriorityClass(GetCurrentProcess()), static_cast<DWORD>(NORMAL_PRIORITY_CLASS));
#else
    QSKIP("Windows only");
#endif
}

void TestM6B::modeCycling()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QSignalSpy scroll(&session, &CaptureSession::scrollRequested);
    QSignalSpy record(&session, &CaptureSession::recordRequested);
    QSignalSpy captured(&session, &CaptureSession::captured);
    QVERIFY(session.start(1, false));
    QCOMPARE(session.mode(), CaptureSession::Mode::Static);
    key(session, QStringLiteral("Ctrl+Tab"));
    QCOMPARE(session.mode(), CaptureSession::Mode::Scroll);
    QVERIFY(session.toolbar()->button(QStringLiteral("mode"))->toolTip().startsWith(QStringLiteral("Scroll"))); // 模式按钮是图标，名称在提示里
    QVERIFY(!session.toolbar()->button(QStringLiteral("mode"))->icon().isNull());
    key(session, QStringLiteral("Ctrl+Tab"));
    QCOMPARE(session.mode(), CaptureSession::Mode::Record);
    key(session, QStringLiteral("Ctrl+Tab"));
    QCOMPARE(session.mode(), CaptureSession::Mode::Static);
    // 录制模式确认 → recordRequested；滚动模式确认 → scrollRequested；静态 → captured
    session.setSelection(QRect(10, 10, 100, 100));
    session.setMode(CaptureSession::Mode::Record);
    key(session, QStringLiteral("Return"));
    QCOMPARE(record.count(), 1);
    QCOMPARE(record.at(0).at(0).toRect(), QRect(10, 10, 100, 100));
    QVERIFY(!session.isActive());
    QVERIFY(session.start(1, false));
    QCOMPARE(session.mode(), CaptureSession::Mode::Static); // 结束后复位
    session.setSelection(QRect(10, 10, 100, 100));
    session.toolbar()->button(QStringLiteral("mode"))->click();
    QCOMPARE(session.mode(), CaptureSession::Mode::Scroll);
    key(session, QStringLiteral("Return"));
    QCOMPARE(scroll.count(), 1);
    app.scrollSession()->cancel();
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(10, 10, 100, 100));
    key(session, QStringLiteral("Return"));
    QCOMPARE(captured.count(), 1);
}

void TestM6B::detectExcludeAndConservative()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("Slow"), QStringLiteral("slowapp.exe"), QRect(0, 0, 400, 400), 0},
                                  WindowInfo{2, QStringLiteral("Other"), QStringLiteral("other.exe"), QRect(500, 0, 400, 400), 1}});
    h.elementDetector->setElements({QRect(50, 50, 100, 100), QRect(550, 50, 100, 100)});
    // 排除：只给窗口框
    s.setValue(QStringLiteral("capture.detectExcludeProcesses"), QStringLiteral("slowapp"));
    QVERIFY(session.start(1, false));
    session.hoverAt(QPoint(80, 80));
    QTRY_VERIFY(session.highlight().has_value());
    QTest::qWait(50);
    QCOMPARE(*session.highlight(), QRect(0, 0, 400, 400));
    session.hoverAt(QPoint(580, 80));
    QTRY_COMPARE(session.highlight().value_or(QRect()), QRect(550, 50, 100, 100));
    session.cancel();
    // 保守：短超时；卡住的进程退回窗口框
    s.setValue(QStringLiteral("capture.detectExcludeProcesses"), QString());
    s.setValue(QStringLiteral("capture.detectConservativeProcesses"), QStringLiteral("slowapp"));
    h.elementDetector->setHangMs(200); // 超过保守超时 100 ms，低于常规 300 ms
    QVERIFY(session.start(1, false));
    session.hoverAt(QPoint(80, 80));
    QTRY_VERIFY(session.highlight().has_value());
    QTest::qWait(300);
    QCOMPARE(*session.highlight(), QRect(0, 0, 400, 400));
    session.hoverAt(QPoint(580, 80)); // 常规进程：300 ms 内能返回
    QTRY_COMPARE(session.highlight().value_or(QRect()), QRect(550, 50, 100, 100));
    session.cancel();
}

void TestM6B::sizeUnitToggle()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(0, 0, 300, 150)); // 150% 屏上
    QCOMPARE(session.sizeText(session.selection()), QStringLiteral("300 × 150"));
    key(session, QStringLiteral("Ctrl+U"));
    QCOMPARE(s.stringValue(QStringLiteral("capture.sizeUnit")), QStringLiteral("logical"));
    QCOMPARE(session.sizeText(session.selection()), QStringLiteral("200 × 100"));
    key(session, QStringLiteral("Ctrl+U"));
    QCOMPARE(session.sizeText(session.selection()), QStringLiteral("300 × 150"));
}

void TestM6B::appearanceSettings()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("capture.maskColor"), QStringLiteral("#ffff0000")); // 不透明红遮罩便于计数
    s.setValue(QStringLiteral("capture.borderColor"), QStringLiteral("#ff00ff00"));
    s.setValue(QStringLiteral("capture.borderWidth"), 3);
    s.setValue(QStringLiteral("capture.magnifierAuto"), false);
    s.setValue(QStringLiteral("capture.guides"), QStringLiteral("off"));
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(300, 300, 150, 150));
    OverlayWindow* w = overlayAt(session, QPoint(300, 300));
    QVERIFY(w);
    QImage img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(img.pixel(10, 10), qRgb(255, 0, 0)); // 遮罩颜色
    // 边框颜色与宽度（3 物理像素）；取离锚点远的位置
    QCOMPARE(img.pixel(300, 340), qRgb(0, 255, 0));
    QCOMPARE(img.pixel(301, 340), qRgb(0, 255, 0)); // 3 像素宽（居中于边线）
    QVERIFY(img.pixel(305, 340) != qRgb(0, 255, 0));
    // 锚点：8 个时边中点有白块；4 个时没有；无时角上也没有；小选区自动隐藏
    QCOMPARE(img.pixel(375, 300), qRgb(255, 255, 255));
    s.setValue(QStringLiteral("capture.anchors"), QStringLiteral("four"));
    img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(img.pixel(375, 300) != qRgb(255, 255, 255));
    QCOMPARE(img.pixel(300, 300), qRgb(255, 255, 255));
    s.setValue(QStringLiteral("capture.anchors"), QStringLiteral("none"));
    img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(img.pixel(300, 300) != qRgb(255, 255, 255));
    s.setValue(QStringLiteral("capture.anchors"), QStringLiteral("eight"));
    s.setValue(QStringLiteral("capture.anchorHideBelow"), 200);
    img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(img.pixel(375, 300) != qRgb(255, 255, 255));
    // 检测动画：关闭时高亮立即到位；开启时中间帧在过渡
    session.cancel();
    h.elementDetector->setElements({QRect(100, 100, 200, 200), QRect(400, 100, 200, 200)});
    s.setValue(QStringLiteral("capture.detectAnimation"), false);
    QVERIFY(session.start(1, false));
    session.hoverAt(QPoint(150, 150));
    QTRY_COMPARE(session.highlight().value_or(QRect()), QRect(100, 100, 200, 200));
    w->renderToImage();
    session.hoverAt(QPoint(450, 150));
    QTRY_COMPARE(session.highlight().value_or(QRect()), QRect(400, 100, 200, 200));
    w->renderToImage();
    QCOMPARE(w->animatedHighlight(), QRect(400, 100, 200, 200));
    s.setValue(QStringLiteral("capture.detectAnimation"), true);
    session.hoverAt(QPoint(150, 150));
    QTRY_COMPARE(session.highlight().value_or(QRect()), QRect(100, 100, 200, 200));
    w->renderToImage();
    const QRect mid = w->animatedHighlight();
    QVERIFY(mid.left() > 100 && mid.left() <= 400);
    QTRY_COMPARE(w->animatedHighlight(), QRect(100, 100, 200, 200));
    session.cancel();
}

void TestM6B::magnifierAppearance()
{
    MagnifierOptions o;
    o.zoom = 4;
    o.pixels = 11;
    const QSize full = magnifierSize(o);
    o.showHint = false;
    o.showCoords = false;
    o.showColor = false;
    const QSize bare = magnifierSize(o);
    QVERIFY(bare.height() < full.height());
    QCOMPARE(full.height() - bare.height(), 3 * (full.height() - magnifierSize([&] { MagnifierOptions t = o; t.showHint = true; t.showCoords = true; return t; }()).height()));
    // 设置驱动：禁用放大镜后 Alt 也不显示；网格颜色
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("capture.detectAnimation"), false); // 画面不随时间变化，便于逐像素比较
    s.setValue(QStringLiteral("capture.magnifierAuto"), false);   // 只在按 Alt 时显示
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QVERIFY(session.start(1, false));
    session.hoverAt(QPoint(200, 200));
    session.setAltHeld(true);
    OverlayWindow* w = overlayAt(session, QPoint(200, 200));
    s.setValue(QStringLiteral("capture.magnifierGrid"), true);
    s.setValue(QStringLiteral("capture.magnifierGridColor"), QStringLiteral("#ffff00ff"));
    const QImage with = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(countColor(with, qRgb(255, 0, 255)) > 50);
    s.setValue(QStringLiteral("capture.magnifierGrid"), false);
    const QImage without = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QCOMPARE(countColor(without, qRgb(255, 0, 255)), 0);
    s.setValue(QStringLiteral("capture.guides"), QStringLiteral("off"));
    session.setAltHeld(false);
    const QImage noAlt = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    session.setAltHeld(true);
    s.setValue(QStringLiteral("capture.magnifierEnabled"), false);
    const QImage disabled = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(disabled != without);
    QCOMPARE(disabled, noAlt); // 禁用后按 Alt 也不显示
}

void TestM6B::guides()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("capture.magnifierAuto"), false);
    s.setValue(QStringLiteral("capture.magnifierEnabled"), false);
    s.setValue(QStringLiteral("capture.maskColor"), QStringLiteral("#00000000"));
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(200, 200, 200, 100));
    session.hoverAt(QPoint(600, 600));
    OverlayWindow* w = overlayAt(session, QPoint(200, 200));
    s.setValue(QStringLiteral("capture.guides"), QStringLiteral("off"));
    const QImage base = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    // 与无辅助线的画面相比，某几行上有多少像素不同
    auto changedRows = [&](const QImage& img, int y0, int y1) {
        int n = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = 0; x < img.width(); ++x)
                if (img.pixel(x, y) != base.pixel(x, y))
                    ++n;
        return n;
    };
    // alt 策略：没按 Alt 不显示，按下显示十字线与选区中心线
    s.setValue(QStringLiteral("capture.guides"), QStringLiteral("alt"));
    QImage img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(changedRows(img, 598, 603) < 20);
    session.setAltHeld(true);
    img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(changedRows(img, 598, 603) > 100);  // 光标行
    QVERIFY(changedRows(img, 248, 252) > 50);   // 选区中心线（y=250）
    session.setAltHeld(false);
    // always + 只开边框指示线：选区上边延长到全屏，中心线与十字线不画
    s.setValue(QStringLiteral("capture.guides"), QStringLiteral("always"));
    s.setValue(QStringLiteral("capture.guideCrosshair"), false);
    s.setValue(QStringLiteral("capture.guideCenterLines"), false);
    s.setValue(QStringLiteral("capture.guideEdgeLines"), true);
    img = w->renderToImage().convertToFormat(QImage::Format_RGB32);
    QVERIFY(changedRows(img, 598, 603) < 20);
    QVERIFY(changedRows(img, 248, 252) < 20);
    QVERIFY(changedRows(img, 198, 202) > 100);
}

void TestM6B::cancelledCapturesInHistory()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(10, 10, 50, 50));
    session.cancel();
    QCOMPARE(app.history().size(), 0);
    s.setValue(QStringLiteral("capture.historyIncludeCancelled"), true);
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(10, 10, 50, 50));
    session.cancel();
    QCOMPARE(app.history().size(), 1);
    QCOMPARE(app.history().at(0).physicalRegion, QRect(10, 10, 50, 50));
    // 没有选区的取消不记录
    QVERIFY(session.start(1, false));
    session.cancel();
    QCOMPARE(app.history().size(), 1);
}

void TestM6B::historyDirectory()
{
    QTemporaryDir dir;
    Settings s;
    s.setValue(QStringLiteral("capture.historyDir"), dir.path());
    s.setValue(QStringLiteral("capture.historyLimit"), 2);
    {
        FakePlatformHandles h;
        App app(makeFakePlatform(&h), s);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(app.session().start(1, false));
            app.session().setSelection(QRect(10 * i, 10, 20 + i, 20));
            app.session().accept();
            QTest::qWait(5);
        }
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("history_*.png")}, QDir::Files).size(), 2); // 按上限清理
    }
    FakePlatformHandles h;
    App app2(makeFakePlatform(&h), s);
    QCOMPARE(app2.history().size(), 2);
    QCOMPARE(app2.history().at(0).image.size(), QSize(22, 20)); // 最新的在前
}

void TestM6B::scrollRegionControl()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("scroll.autoScroll"), false);
    Platform p = makeFakePlatform(&h);
    ScrollCaptureSession scroll(p.screenCapture.get(), p.input.get(), s);
    // 未开始：任意调整
    QVERIFY(scroll.adjustRegion(QRect(0, 0, 50, 50)));
    QVERIFY(scroll.begin(QRect(100, 100, 300, 200), false));
    QSignalSpy status(&scroll, &ScrollCaptureSession::statusChanged);
    // 进行中：只能沿纵向移动、尺寸不变
    QVERIFY(!scroll.adjustRegion(QRect(100, 100, 300, 250)));
    QVERIFY(!scroll.adjustRegion(QRect(120, 100, 300, 200)));
    QVERIFY(scroll.adjustRegion(QRect(100, 160, 300, 200)));
    QCOMPARE(scroll.region(), QRect(100, 160, 300, 200));
    QVERIFY(status.count() >= 1);
    scroll.finish();
    QVERIFY(scroll.adjustRegion(QRect(0, 0, 10, 10))); // 结束后可任意调整
}

QTEST_MAIN(TestM6B)
#include "tst_m6b.moc"
