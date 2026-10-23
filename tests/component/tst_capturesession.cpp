// REQ: CAP-01 CAP-02 CAP-03 CAP-10 CAP-11 CAP-12 CAP-15 CAP-23 CAP-24 CAP-40 OUT-01 OUT-03 OUT-08 PIN-02 NFR-07 NFR-08 NFR-14 TST-02 TST-03
// 组件测试（无显示器）：会话在合成多屏布局上的完整交互
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakeElementDetector.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ss;
using namespace ss::testing;

namespace {

struct Fixture {
    FakePlatformHandles h;
    Platform p;
    Settings settings;
    CaptureHistory history{10};
    OutputActions output{settings};
    std::unique_ptr<CaptureSession> session;

    Fixture()
        : p(makeFakePlatform(&h))
    {
        session = std::make_unique<CaptureSession>(p.screenCapture.get(), p.windowDetector.get(), p.elementDetector.get(), settings);
        session->setHistory(&history);
        session->setOutput(&output);
        session->prepareOverlays();
    }
    bool start() { return session->start(1, false); }
    void key(const QString& seq, Qt::KeyboardModifiers extra = Qt::NoModifier)
    {
        const QKeyCombination c = QKeySequence(seq)[0];
        QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers() | extra);
        session->handleKeyPress(&e);
    }
    void keyRelease(int k)
    {
        QKeyEvent e(QEvent::KeyRelease, k, Qt::NoModifier);
        session->handleKeyRelease(&e);
    }
    void drag(QPoint from, QPoint to)
    {
        session->pressAt(from, Qt::LeftButton);
        session->dragTo(from + QPoint(5, 5));
        session->dragTo(to);
        session->releaseAt(to, Qt::LeftButton);
    }
    QImage expectedCrop(const QRect& region)
    {
        QImage img(region.size(), QImage::Format_ARGB32);
        for (int y = 0; y < region.height(); ++y)
            for (int x = 0; x < region.width(); ++x) {
                const int px = region.x() + x, py = region.y() + y;
                img.setPixel(x, y, px < 3840 ? FakeScreenCapture::patternPixel(0, px, py) : FakeScreenCapture::patternPixel(1, px - 3840, py));
            }
        return img;
    }
};

} // namespace

class TestCaptureSession : public QObject {
    Q_OBJECT
private slots:
    void startFreezesAllScreens();
    void dragCreatesSelectionAndCopies();
    void selectionAcrossScreensCropsExactly();
    void clickSelectsDetectedWindow();
    void elementDetectionAndTabToggle();
    void anchorsMoveAndResize();
    void keyboardNudge();
    void fullScreenToggle();
    void colorPick();
    void historyRecall();
    void quickSaveWritesFile();
    void saveWithOverriddenDialog();
    void pinRequest();
    void cancelAndRightClick();
    void focusLossSetting();
    void layoutChangeRecreatesOverlays();
};

void TestCaptureSession::startFreezesAllScreens()
{
    Fixture f;
    QCOMPARE(f.session->overlays().size(), 2);
    QSignalSpy started(f.session.get(), &CaptureSession::started);
    QVERIFY(f.start());
    QVERIFY(f.session->isActive());
    QCOMPARE(f.session->state(), CaptureSession::State::Detecting);
    QCOMPARE(started.count(), 1);
    QCOMPARE(f.session->desktop().snapshots().size(), 2);
    QVERIFY(!f.start());
    f.session->cancel();
    QVERIFY(f.session->desktop().isEmpty());
}

void TestCaptureSession::dragCreatesSelectionAndCopies()
{
    Fixture f;
    QVERIFY(f.start());
    QSignalSpy changed(f.session.get(), &CaptureSession::selectionChanged);
    f.session->pressAt(QPoint(500, 400), Qt::LeftButton);
    QVERIFY(!f.session->dragging()); // 还没超过单击容差
    f.session->dragTo(QPoint(300, 450));
    QVERIFY(f.session->dragging());
    QCOMPARE(f.session->state(), CaptureSession::State::Selecting);
    f.session->releaseAt(QPoint(280, 470), Qt::LeftButton); // 松开点也计入
    QCOMPARE(f.session->selection(), QRect(QPoint(280, 400), QPoint(500, 470)));
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    QVERIFY(changed.count() >= 2);
    QSignalSpy captured(f.session.get(), &CaptureSession::captured);
    f.key(QStringLiteral("Return"));
    QCOMPARE(captured.count(), 1);
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(221, 71));
    QVERIFY(!f.session->isActive());
    QCOMPARE(f.history.size(), 1);
}

void TestCaptureSession::selectionAcrossScreensCropsExactly()
{
    Fixture f;
    QVERIFY(f.start());
    const QRect region(3800, 100, 80, 60);
    f.session->setSelection(region);
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    const QImage img = f.session->accept();
    SS_VERIFY_IMAGE(img, f.expectedCrop(region), CompareOptions{}, "session_cross_screen");
}

void TestCaptureSession::clickSelectsDetectedWindow()
{
    Fixture f;
    f.h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("w"), QStringLiteral("a.exe"), QRect(100, 100, 400, 300), 0}});
    f.settings.setValue(QStringLiteral("capture.detectMode"), QStringLiteral("window"));
    QVERIFY(f.start());
    f.session->hoverAt(QPoint(200, 200));
    QVERIFY(f.session->highlight().has_value());
    QCOMPARE(*f.session->highlight(), QRect(100, 100, 400, 300));
    f.session->hoverAt(QPoint(900, 900));
    QVERIFY(!f.session->highlight().has_value());
    f.session->hoverAt(QPoint(200, 200));
    f.session->pressAt(QPoint(200, 200), Qt::LeftButton);
    f.session->releaseAt(QPoint(201, 200), Qt::LeftButton);
    QCOMPARE(f.session->selection(), QRect(100, 100, 400, 300));
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    // 双击窗口直接复制
    f.session->setSelection(QRect());
    f.session->hoverAt(QPoint(200, 200));
    QSignalSpy captured(f.session.get(), &CaptureSession::captured);
    f.session->doubleClickAt(QPoint(200, 200));
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(1).toRect(), QRect(100, 100, 400, 300));
}

void TestCaptureSession::elementDetectionAndTabToggle()
{
    Fixture f;
    f.h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("w"), QStringLiteral("a.exe"), QRect(0, 0, 1000, 1000), 0}});
    f.h.elementDetector->setElements({QRect(100, 100, 50, 20)});
    QVERIFY(f.start());
    QCOMPARE(f.session->detectMode(), CaptureSession::DetectMode::Element);
    f.session->hoverAt(QPoint(110, 110));
    QTRY_COMPARE_WITH_TIMEOUT(f.session->highlight().value_or(QRect()), QRect(100, 100, 50, 20), 2000);
    f.session->toggleDetectMode();
    QCOMPARE(f.session->detectMode(), CaptureSession::DetectMode::Window);
    QCOMPARE(*f.session->highlight(), QRect(0, 0, 1000, 1000));
    f.key(QStringLiteral("Tab"));
    QCOMPARE(f.session->detectMode(), CaptureSession::DetectMode::Element);
    // 挂起的元素检测：退回窗口框，不阻塞
    f.h.elementDetector->setHangMs(5000);
    f.session->hoverAt(QPoint(120, 110));
    QCOMPARE(*f.session->highlight(), QRect(0, 0, 1000, 1000));
    f.session->cancel();
}

void TestCaptureSession::anchorsMoveAndResize()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->setSelection(QRect(100, 100, 200, 100));
    QCOMPARE(f.session->handleAt(QPoint(300, 200)), SelectionHandle::BottomRight);
    // 拖右下角
    f.session->pressAt(QPoint(299, 199), Qt::LeftButton);
    f.session->dragTo(QPoint(319, 209));
    f.session->releaseAt(QPoint(319, 209), Qt::LeftButton);
    QCOMPARE(f.session->selection(), QRect(100, 100, 220, 110));
    // 内部拖动移动
    f.session->pressAt(QPoint(200, 150), Qt::LeftButton);
    f.session->dragTo(QPoint(250, 170));
    f.session->releaseAt(QPoint(250, 170), Qt::LeftButton);
    QCOMPARE(f.session->selection(), QRect(150, 120, 220, 110));
    // 选区外按下并拖动：重新框选
    f.drag(QPoint(600, 600), QPoint(650, 640));
    QCOMPARE(f.session->selection(), QRect(QPoint(600, 600), QPoint(650, 640)));
    f.session->cancel();
}

void TestCaptureSession::keyboardNudge()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->setSelection(QRect(100, 100, 20, 20));
    f.key(QStringLiteral("Right"));
    QCOMPARE(f.session->selection(), QRect(101, 100, 20, 20));
    f.key(QStringLiteral("Ctrl+Down"));
    QCOMPARE(f.session->selection(), QRect(101, 100, 20, 21));
    f.key(QStringLiteral("Shift+Left"));
    QCOMPARE(f.session->selection(), QRect(102, 100, 19, 21));
    // WASD 移动光标
    QSignalSpy moved(f.session.get(), &CaptureSession::cursorMoveRequested);
    f.session->hoverAt(QPoint(50, 50));
    f.key(QStringLiteral("D"));
    QCOMPARE(f.session->cursor(), QPoint(51, 50));
    QCOMPARE(moved.count(), 1);
    // 互换后方向键动光标
    f.settings.setValue(QStringLiteral("capture.swapWasdArrows"), true);
    f.key(QStringLiteral("Up"));
    QCOMPARE(f.session->cursor(), QPoint(51, 49));
    f.key(QStringLiteral("S"));
    QCOMPARE(f.session->selection(), QRect(102, 101, 19, 21));
    f.session->cancel();
}

void TestCaptureSession::fullScreenToggle()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->hoverAt(QPoint(4000, 100)); // 右屏
    f.key(QStringLiteral("Ctrl+A"));
    QCOMPARE(f.session->selection(), QRect(3840, 0, 3840, 2160));
    f.key(QStringLiteral("Ctrl+A"));
    QCOMPARE(f.session->selection(), QRect(0, 0, 7680, 2160));
    f.key(QStringLiteral("Ctrl+A"));
    QCOMPARE(f.session->selection(), QRect(3840, 0, 3840, 2160));
    f.session->cancel();
}

void TestCaptureSession::colorPick()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->hoverAt(QPoint(10, 20));
    const QRgb px = FakeScreenCapture::patternPixel(0, 10, 20);
    QCOMPARE(f.session->colorTextAtCursor(), QColor(px).name().toUpper());
    QSignalSpy copied(f.session.get(), &CaptureSession::colorCopied);
    f.key(QStringLiteral("C"));
    QCOMPARE(copied.count(), 1);
    QCOMPARE(QApplication::clipboard()->text(), QColor(px).name().toUpper());
    // 单独按 Shift 轮换格式
    f.key(QStringLiteral("Shift"));
    f.keyRelease(Qt::Key_Shift);
    QCOMPARE(f.session->colorFormat(), ColorFormat::Hsv);
    QCOMPARE(f.settings.stringValue(QStringLiteral("capture.colorFormat")), QStringLiteral("hsv"));
    QVERIFY(f.session->isActive());
    f.session->cancel();
}

void TestCaptureSession::historyRecall()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->setSelection(QRect(10, 10, 30, 20));
    f.session->accept();
    QVERIFY(f.start());
    f.session->setSelection(QRect(50, 50, 40, 40));
    f.session->accept();
    QCOMPARE(f.history.size(), 2);
    QVERIFY(f.start());
    f.key(QStringLiteral(","));
    QCOMPARE(f.session->state(), CaptureSession::State::History);
    QCOMPARE(f.session->selection(), QRect(50, 50, 40, 40));
    QCOMPARE(f.session->historyImage().size(), QSize(40, 40));
    f.key(QStringLiteral(","));
    QCOMPARE(f.session->selection(), QRect(10, 10, 30, 20));
    f.key(QStringLiteral("."));
    QCOMPARE(f.session->selection(), QRect(50, 50, 40, 40));
    f.key(QStringLiteral("."));
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    // 查看历史时复制的是历史图，且不重复写入历史
    f.key(QStringLiteral(","));
    f.key(QStringLiteral("Return"));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(40, 40));
    QCOMPARE(f.history.size(), 2);
    // 上限 0 关闭历史
    f.settings.setValue(QStringLiteral("capture.historyLimit"), 0);
    f.history.setLimit(0);
    QVERIFY(f.history.isEmpty());
}

void TestCaptureSession::quickSaveWritesFile()
{
    Fixture f;
    QTemporaryDir dir;
    f.settings.setValue(QStringLiteral("output.quickSaveDir"), dir.path());
    f.settings.setValue(QStringLiteral("output.fileNamePattern"), QStringLiteral("shot_{n}"));
    QVERIFY(f.start());
    f.session->setSelection(QRect(0, 0, 8, 4));
    QSignalSpy done(&f.output, &OutputActions::done);
    f.key(QStringLiteral("Ctrl+Shift+S"));
    QCOMPARE(done.count(), 1);
    const QString path = done.at(0).at(1).toString();
    QVERIFY(path.endsWith(QStringLiteral("shot_1.png")));
    QImage saved(path);
    SS_VERIFY_IMAGE(saved, f.expectedCrop(QRect(0, 0, 8, 4)), CompareOptions{}, "quick_save");
    QVERIFY(!f.session->isActive());
}

void TestCaptureSession::saveWithOverriddenDialog()
{
    Fixture f;
    QTemporaryDir dir;
    f.settings.setValue(QStringLiteral("output.saveFormat"), QStringLiteral("png"));
    const QString path = QDir(dir.path()).filePath(QStringLiteral("a.webp"));
    f.output.setDialogOverride(path);
    QVERIFY(f.start());
    f.session->setSelection(QRect(0, 0, 16, 16));
    f.key(QStringLiteral("Ctrl+S"));
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path).size(), QSize(16, 16));
    QCOMPARE(f.settings.stringValue(QStringLiteral("output.saveFormat")), QStringLiteral("webp")); // 记住格式
    QCOMPARE(f.settings.stringValue(QStringLiteral("output.saveDir")), QDir(dir.path()).absolutePath());
}

void TestCaptureSession::pinRequest()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->setSelection(QRect(5, 5, 10, 10));
    QSignalSpy pin(f.session.get(), &CaptureSession::pinRequested);
    f.key(QStringLiteral("Ctrl+T"));
    QCOMPARE(pin.count(), 1);
    QCOMPARE(pin.at(0).at(1).toRect(), QRect(5, 5, 10, 10));
    QVERIFY(!f.session->isActive());
    QCOMPARE(f.history.size(), 1);
}

void TestCaptureSession::cancelAndRightClick()
{
    Fixture f;
    QVERIFY(f.start());
    f.session->setSelection(QRect(10, 10, 20, 20));
    f.session->pressAt(QPoint(500, 500), Qt::RightButton);
    QVERIFY(f.session->selection().isEmpty());
    QCOMPARE(f.session->state(), CaptureSession::State::Detecting);
    QSignalSpy cancelled(f.session.get(), &CaptureSession::cancelled);
    f.session->pressAt(QPoint(500, 500), Qt::RightButton);
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!f.session->isActive());
    QVERIFY(f.start());
    f.key(QStringLiteral("Escape"));
    QCOMPARE(cancelled.count(), 2);
}

void TestCaptureSession::focusLossSetting()
{
    Fixture f;
    QVERIFY(f.start());
    f.settings.setValue(QStringLiteral("capture.exitOnFocusLoss"), false);
    f.session->onFocusLost();
    QVERIFY(f.session->isActive());
    f.settings.setValue(QStringLiteral("capture.exitOnFocusLoss"), true);
    f.session->onFocusLost();
    QVERIFY(!f.session->isActive());
}

void TestCaptureSession::layoutChangeRecreatesOverlays()
{
    Fixture f;
    QCOMPARE(f.session->overlays().size(), 2);
    f.h.screenCapture->setScreens({ScreenInfo{QStringLiteral("ONLY"), QRect(0, 0, 1920, 1080), 1.0, true, false}});
    QVERIFY(f.start());
    QCOMPARE(f.session->overlays().size(), 1);
    QCOMPARE(f.session->overlays().first()->screenInfo().id, QStringLiteral("ONLY"));
    f.session->cancel();
}

QTEST_MAIN(TestCaptureSession)
#include "tst_capturesession.moc"
