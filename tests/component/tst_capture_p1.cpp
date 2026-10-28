// REQ: CAP-04 CAP-06 CAP-13 CAP-16 CAP-17 CAP-18 CAP-30 CAP-31 CAP-32 CAP-33
// M5 截图 P1：截图后动作、快捷截图、层级元素、Space 移动、尺寸输入、固定比例、指针、刷新、圆角、阴影 / 边框
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "core/config/Settings.h"
#include "core/output/Decorate.h"
#include "support/TestSupport.h"
#include "testing/FakeElementDetector.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

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
    void key(const QString& seq)
    {
        const QKeyCombination c = QKeySequence(seq)[0];
        QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
        session->handleKeyPress(&e);
    }
    void keyRelease(int k)
    {
        QKeyEvent e(QEvent::KeyRelease, k, Qt::NoModifier);
        session->handleKeyRelease(&e);
    }
};
} // namespace

class TestCaptureP1 : public QObject {
    Q_OBJECT
private slots:
    void decorateRoundedShadowBorder();
    void cursorIncludedAndToggle();
    void refreshKeepsSelection();
    void spaceMovesWhileResizing();
    void sizeInputAndRatioLock();
    void elementChainWheel();
    void enterActionsOrder();
    void quickCaptures();
};

void TestCaptureP1::decorateRoundedShadowBorder()
{
    QImage src(40, 30, QImage::Format_RGB32);
    src.fill(Qt::red);
    DecorateOptions o;
    o.cornerRadius = 10;
    QImage rounded = decorateImage(src, o);
    QCOMPARE(rounded.size(), src.size());
    QCOMPARE(qAlpha(rounded.pixel(0, 0)), 0);           // 角外透明
    QCOMPARE(rounded.pixel(20, 15), qRgba(255, 0, 0, 255));
    o.shadow = true;
    o.border = true;
    o.borderColor = Qt::blue;
    o.borderWidth = 4;
    QImage full = decorateImage(src, o);
    QCOMPARE(full.width(), 40 + 2 * o.shadowMargin());
    const int m = o.shadowMargin();
    QVERIFY(qAlpha(full.pixel(m + 20, m + 30 + 6)) > 0);  // 阴影在下方
    QCOMPARE(full.pixel(m + 20, m + 1), qRgba(0, 0, 255, 255)); // 边框在内侧
    QCOMPARE(decorateImage(src, DecorateOptions{}).cacheKey(), src.cacheKey()); // 无装饰原样返回
}

void TestCaptureP1::cursorIncludedAndToggle()
{
    Fixture f;
    QImage cursor(8, 8, QImage::Format_ARGB32);
    cursor.fill(qRgba(0, 0, 255, 255));
    f.h.screenCapture->setCursor(CursorImage{cursor, QPoint(110, 110)});
    f.settings.setValue(QStringLiteral("capture.includeCursor"), true);
    QVERIFY(f.session->start(1, false));
    QVERIFY(f.session->includeCursor());
    f.session->setSelection(QRect(100, 100, 50, 50));
    QImage out = f.session->finalImage();
    QCOMPARE(out.pixel(12, 12), qRgb(0, 0, 255));
    f.key(QStringLiteral("`"));
    QVERIFY(!f.session->includeCursor());
    out = f.session->finalImage();
    QCOMPARE(out.pixel(12, 12), FakeScreenCapture::patternPixel(0, 112, 112));
    f.session->cancel();
}

void TestCaptureP1::refreshKeepsSelection()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(10, 10, 20, 20));
    QImage changed(3840, 2160, QImage::Format_RGB32);
    changed.fill(qRgb(1, 2, 3));
    f.h.screenCapture->setImage(0, changed);
    f.key(QStringLiteral("F5"));
    QCOMPARE(f.session->selection(), QRect(10, 10, 20, 20));
    QCOMPARE(f.session->currentImage().pixel(0, 0), qRgb(1, 2, 3));
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    f.session->cancel();
}

void TestCaptureP1::spaceMovesWhileResizing()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(100, 100, 200, 100));
    f.session->pressAt(QPoint(299, 199), Qt::LeftButton); // 右下角锚点
    f.session->dragTo(QPoint(319, 219));
    QCOMPARE(f.session->selection(), QRect(100, 100, 220, 120));
    f.key(QStringLiteral("Space"));
    f.session->dragTo(QPoint(329, 229)); // 按住 Space 后：整体移动 10,10
    QCOMPARE(f.session->selection(), QRect(110, 110, 220, 120));
    f.session->releaseAt(QPoint(329, 229), Qt::LeftButton);
    f.keyRelease(Qt::Key_Space);
    f.session->cancel();
}

void TestCaptureP1::sizeInputAndRatioLock()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(100, 100, 200, 100));
    f.session->setSelectionSize(300, 150);
    QCOMPARE(f.session->selection(), QRect(100, 100, 300, 150));
    QVERIFY(f.session->toolbar()->button(QStringLiteral("ratio")));
    f.session->setRatioLocked(true);
    f.session->pressAt(QPoint(399, 249), Qt::LeftButton);
    f.session->dragTo(QPoint(499, 249)); // 宽 +100 → 高按 2:1 跟随
    f.session->releaseAt(QPoint(499, 249), Qt::LeftButton);
    QCOMPARE(f.session->selection(), QRect(100, 100, 400, 200));
    f.session->setRatioLocked(false);
    // Shift 拖拽新选区为正方形
    f.session->setSelection(QRect());
    f.key(QStringLiteral("Shift"));
    f.session->pressAt(QPoint(500, 500), Qt::LeftButton);
    f.session->dragTo(QPoint(600, 530));
    f.session->releaseAt(QPoint(600, 530), Qt::LeftButton);
    QCOMPARE(f.session->selection().size(), QSize(101, 101));
    f.keyRelease(Qt::Key_Shift);
    f.session->cancel();
}

void TestCaptureP1::elementChainWheel()
{
    Fixture f;
    f.h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("w"), QStringLiteral("a.exe"), QRect(0, 0, 1000, 1000), 0}});
    f.h.elementDetector->setElements({QRect(100, 100, 400, 300), QRect(150, 150, 100, 40)});
    QVERIFY(f.session->start(1, false));
    f.session->hoverAt(QPoint(160, 160));
    QTRY_COMPARE_WITH_TIMEOUT(f.session->highlight().value_or(QRect()), QRect(150, 150, 100, 40), 2000);
    f.session->cycleElement(1); // 父元素
    QCOMPARE(*f.session->highlight(), QRect(100, 100, 400, 300));
    f.session->cycleElement(1); // 窗口
    QCOMPARE(*f.session->highlight(), QRect(0, 0, 1000, 1000));
    f.session->cycleElement(1); // 到顶不变
    QCOMPARE(*f.session->highlight(), QRect(0, 0, 1000, 1000));
    f.session->cycleElement(-2); // 回到子元素
    QCOMPARE(*f.session->highlight(), QRect(150, 150, 100, 40));
    f.session->cancel();
}

void TestCaptureP1::enterActionsOrder()
{
    Fixture f;
    QTemporaryDir dir;
    f.output.setDialogOverride(dir.filePath(QStringLiteral("out.png")));
    f.settings.setValue(QStringLiteral("capture.enterActions"), QStringLiteral("save,pin,copy"));
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(QRect(0, 0, 10, 10));
    QSignalSpy pin(f.session.get(), &CaptureSession::pinRequested);
    QSignalSpy done(&f.output, &OutputActions::done);
    f.key(QStringLiteral("Return"));
    // 固定顺序：复制 → 贴图 → 保存
    QCOMPARE(done.count(), 2);
    QCOMPARE(done.at(0).at(0).value<OutputActions::Kind>(), OutputActions::Kind::Copy);
    QCOMPARE(done.at(1).at(0).value<OutputActions::Kind>(), OutputActions::Kind::Save);
    QCOMPARE(pin.count(), 1);
    QVERIFY(!f.session->isActive());
    QCOMPARE(f.history.size(), 1);
}

void TestCaptureP1::quickCaptures()
{
    Settings s;
    FakePlatformHandles h;
    App app(makeFakePlatform(&h), s);
    h.windowDetector->setActiveWindow(WindowInfo{1, QStringLiteral("w"), QStringLiteral("a.exe"), QRect(4000, 100, 300, 200), 0});
    QSignalSpy captured(&app.session(), &CaptureSession::captured);
    QVERIFY(app.quickCapture(QStringLiteral("full")));
    QCOMPARE(captured.last().at(1).toRect(), QRect(0, 0, 7680, 2160));
    QVERIFY(app.quickCapture(QStringLiteral("activeWindow")));
    QCOMPARE(captured.last().at(1).toRect(), QRect(4000, 100, 300, 200));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(300, 200));
    QVERIFY(app.quickCapture(QStringLiteral("activeScreen")));
    QCOMPARE(captured.last().at(1).toRect(), QRect(3840, 0, 3840, 2160));
    QVERIFY(app.quickCapture(QStringLiteral("last"))); // 上次区域
    QCOMPARE(captured.last().at(1).toRect(), QRect(3840, 0, 3840, 2160));
    QCOMPARE(captured.count(), 4);
    // 命令行形式
    app.runCommand({QStringLiteral("snip"), QStringLiteral("10,20,30,40")});
    QCOMPARE(captured.last().at(1).toRect(), QRect(10, 20, 30, 40));
    QVERIFY(!app.quickCapture(QStringLiteral("bogus")));
}

QTEST_MAIN(TestCaptureP1)
#include "tst_capture_p1.moc"
