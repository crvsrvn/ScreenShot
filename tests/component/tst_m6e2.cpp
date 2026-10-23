// REQ: ACT-10 ACT-11 ACT-12 ACT-13 ACT-15
// M6 动作 P2（第二批）：修饰键双击、全局鼠标手势、屏幕热角、脚本引擎、过滤模拟按键
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "core/config/Actions.h"
#include "core/config/Settings.h"
#include "core/input/Gestures.h"
#include "core/script/ScriptEngine.h"
#include "support/TestSupport.h"
#include "testing/FakeHotkey.h"
#include "testing/FakePlatform.h"

#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestM6E2 : public QObject {
    Q_OBJECT
private slots:
    void doubleTapDetector();
    void doubleTapTriggersAction();
    void injectedFilter();
    void injectedHotkeyIgnored();
    void globalMouseGestureLogic();
    void globalMouseGestureCaptures();
    void hotCornerDetector();
    void hotCornerAction();
    void scriptEngineApi();
    void scriptErrorsAndTimeout();
    void scriptViaActionAndCli();
};

void TestM6E2::doubleTapDetector()
{
    DoubleTapDetector d(300);
    QVERIFY(!d.keyDown(true, 1000));
    QVERIFY(d.keyDown(true, 1200));   // 200 ms 内第二次
    QVERIFY(!d.keyDown(true, 1300));  // 已消费，重新计
    QVERIFY(!d.keyDown(true, 1700));  // 400 ms 超时
    QVERIFY(!d.keyDown(false, 1750)); // 中间别的键
    QVERIFY(!d.keyDown(true, 1800));
    QVERIFY(d.keyDown(true, 1900));
    d.setThreshold(100);
    QVERIFY(!d.keyDown(true, 3000));
    QVERIFY(!d.keyDown(true, 3200));
}

void TestM6E2::doubleTapTriggersAction()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("hotkey.doubleTap"), QStringLiteral("ctrl"));
    s.setValue(QStringLiteral("hotkey.doubleTapAction"), QStringLiteral("capture"));
    App app(makeFakePlatform(&h), s);
    app.onGlobalKeyDown(0xA2, false, 1000); // 左 Ctrl
    QVERIFY(!app.session().isActive());
    app.onGlobalKeyDown(0xA2, false, 1150);
    QVERIFY(app.session().isActive());
    app.session().cancel();
    // 注入的按键不算
    app.onGlobalKeyDown(0xA2, true, 2000);
    app.onGlobalKeyDown(0xA2, true, 2100);
    QVERIFY(!app.session().isActive());
    // 其他修饰键不算
    app.onGlobalKeyDown(0xA0, false, 3000);
    app.onGlobalKeyDown(0xA0, false, 3100);
    QVERIFY(!app.session().isActive());
    s.setValue(QStringLiteral("hotkey.doubleTap"), QStringLiteral("none"));
    app.onGlobalKeyDown(0xA2, false, 4000);
    app.onGlobalKeyDown(0xA2, false, 4100);
    QVERIFY(!app.session().isActive());
}

void TestM6E2::injectedFilter()
{
    InjectedFilter f(80);
    f.keyEvent(0x43, false, 1000);
    QVERIFY(!f.shouldIgnore(0x43, 1010));
    f.keyEvent(0x43, true, 1100);
    QVERIFY(f.shouldIgnore(0x43, 1150));
    QVERIFY(!f.shouldIgnore(0x44, 1150));
    QVERIFY(!f.shouldIgnore(0x43, 1300));
}

void TestM6E2::injectedHotkeyIgnored()
{
#if defined(Q_OS_WIN)
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("Ctrl+Alt+K"));
    App app(makeFakePlatform(&h), s);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    app.onGlobalKeyDown('K', true, now); // 模拟工具发出的 K
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(!app.session().isActive());
    s.setValue(QStringLiteral("hotkey.ignoreInjected"), false);
    app.onGlobalKeyDown('K', true, QDateTime::currentMSecsSinceEpoch());
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(app.session().isActive());
    app.session().cancel();
#else
    QSKIP("Windows only");
#endif
}

void TestM6E2::globalMouseGestureLogic()
{
    GlobalMouseGesture g;
    QVERIFY(!g.press(1, QPoint(10, 10))); // 没按修饰键
    g.setModifierHeld(true);
    QVERIFY(g.press(1, QPoint(10, 10)));
    QVERIFY(g.active());
    QVERIFY(g.move(QPoint(100, 60)));
    QCOMPARE(g.currentRegion(), QRect(10, 10, 91, 51));
    QVERIFY(!g.release(3, QPoint(100, 60))); // 不是同一个键
    const auto r = g.release(1, QPoint(100, 60));
    QVERIFY(r);
    QCOMPARE(r->region, QRect(10, 10, 91, 51));
    QCOMPARE(r->button, 1);
    QVERIFY(!g.active());
    // 太小的拖动不算
    QVERIFY(g.press(2, QPoint(5, 5)));
    QVERIFY(!g.release(2, QPoint(6, 6)));
}

void TestM6E2::globalMouseGestureCaptures()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("globalMouse.enabled"), true);
    s.setValue(QStringLiteral("globalMouse.modifier"), QStringLiteral("alt"));
    s.setValue(QStringLiteral("globalMouse.middle"), QStringLiteral("copy"));
    App app(makeFakePlatform(&h), s);
    QSignalSpy captured(&app.session(), &CaptureSession::captured);
    app.onGlobalKeyDown(0xA4, false, 1000); // 左 Alt 按下
    app.onGlobalMouse(2, true, QPoint(20, 20));
    QVERIFY(app.globalMouseGestureActive());
    app.onGlobalMouseMoved(QPoint(120, 80));
    app.onGlobalMouse(2, false, QPoint(120, 80));
    QVERIFY(!app.globalMouseGestureActive());
    QTRY_COMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(1).toRect(), QRect(20, 20, 101, 61));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(101, 61));
    // 左键 → 贴图
    app.onGlobalMouse(1, true, QPoint(200, 200));
    app.onGlobalMouse(1, false, QPoint(260, 250));
    QTRY_COMPARE(app.pins().size(), 1);
    // 关闭后不响应
    s.setValue(QStringLiteral("globalMouse.enabled"), false);
    app.onGlobalMouse(1, true, QPoint(10, 10));
    QVERIFY(!app.globalMouseGestureActive());
}

void TestM6E2::hotCornerDetector()
{
    HotCornerDetector d(3, 300);
    const QRect screen(0, 0, 1920, 1080);
    QCOMPARE(d.feed(QPoint(500, 500), screen, 0), HotCornerDetector::Corner::None);
    QCOMPARE(d.feed(QPoint(0, 0), screen, 1000), HotCornerDetector::Corner::None);     // 刚进入
    QCOMPARE(d.feed(QPoint(1, 2), screen, 1200), HotCornerDetector::Corner::None);     // 不够久
    QCOMPARE(d.feed(QPoint(1, 2), screen, 1400), HotCornerDetector::Corner::TopLeft);  // 触发
    QCOMPARE(d.feed(QPoint(1, 2), screen, 2000), HotCornerDetector::Corner::None);     // 不重复
    QCOMPARE(d.feed(QPoint(500, 500), screen, 2100), HotCornerDetector::Corner::None);
    QCOMPARE(d.feed(QPoint(1919, 1079), screen, 2200), HotCornerDetector::Corner::None);
    QCOMPARE(d.feed(QPoint(1919, 1079), screen, 2600), HotCornerDetector::Corner::BottomRight);
    QCOMPARE(HotCornerDetector::cornerName(HotCornerDetector::Corner::TopRight), QStringLiteral("topRight"));
}

void TestM6E2::hotCornerAction()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("hotCorner.topRight"), QStringLiteral("togglePins"));
    s.setValue(QStringLiteral("hotCorner.dwellMs"), 100);
    App app(makeFakePlatform(&h), s);
    app.pinImage(QImage(20, 20, QImage::Format_ARGB32_Premultiplied), QPoint(100, 100));
    QVERIFY(!app.pinsHidden());
    const QRect g = QGuiApplication::primaryScreen()->geometry();
    const QPoint corner(g.right(), g.top());
    app.pollHotCorners(corner, 1000);
    app.pollHotCorners(corner, 1050);
    QVERIFY(!app.pinsHidden());
    app.pollHotCorners(corner, 1200);
    QVERIFY(app.pinsHidden());
    app.pollHotCorners(corner, 1500);
    QVERIFY(app.pinsHidden()); // 不重复触发
    app.pollHotCorners(QPoint(g.center()), 1600);
    app.pollHotCorners(corner, 1700);
    app.pollHotCorners(corner, 1900);
    QVERIFY(!app.pinsHidden());
}

void TestM6E2::scriptEngineApi()
{
    QStringList commands;
    QString clip = QStringLiteral("start");
    ScriptHost host;
    host.command = [&commands](const QString& line) {
        commands.append(line);
        return QStringLiteral("ok:") + line;
    };
    host.getClipboard = [&clip] { return clip; };
    host.setClipboard = [&clip](const QString& t) { clip = t; };
    host.sleep = [](int) {};
    ScriptEngine engine(host);
    const ScriptResult r = engine.run(QStringLiteral(R"(
        const a = ss.snip("full --output copy");
        ss.pin("--text hello");
        const b = ss.command("pins hide");
        print("got", a, b);
        ss.setClipboard(ss.getClipboard() + "!");
        ss.sleep(1);
        [1, 2, 3].map(x => x * 2).join(",")
    )"));
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(commands, (QStringList{QStringLiteral("snip full --output copy"), QStringLiteral("pin --text hello"), QStringLiteral("pins hide")}));
    QCOMPARE(r.output, QStringLiteral("got ok:snip full --output copy ok:pins hide"));
    QCOMPARE(clip, QStringLiteral("start!"));
    QCOMPARE(r.value, QStringLiteral("2,4,6"));
    // 没有文件 / 网络 / 进程接口
    const ScriptResult noStd = engine.run(QStringLiteral("typeof std + ',' + typeof os + ',' + typeof require + ',' + typeof fetch"));
    QCOMPARE(noStd.value, QStringLiteral("undefined,undefined,undefined,undefined"));
}

void TestM6E2::scriptErrorsAndTimeout()
{
    ScriptEngine engine(ScriptHost{});
    const ScriptResult bad = engine.run(QStringLiteral("this is not js"));
    QVERIFY(!bad.ok);
    QVERIFY(bad.error.contains(QStringLiteral("SyntaxError")));
    const ScriptResult thrown = engine.run(QStringLiteral("throw new Error('boom')"));
    QVERIFY(!thrown.ok);
    QVERIFY(thrown.error.contains(QStringLiteral("boom")));
    engine.setTimeout(200);
    QElapsedTimer t;
    t.start();
    const ScriptResult loop = engine.run(QStringLiteral("while (true) {}"));
    QVERIFY(!loop.ok);
    QVERIFY(t.elapsed() < 5000);
}

void TestM6E2::scriptViaActionAndCli()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("actions.list"), actionsToJson({{QStringLiteral("Two pins"), QStringLiteral("script ss.pin('--text one --pos 0,0'); ss.pin('--text two --pos 50,50');"), QString(), true}}));
    App app(makeFakePlatform(&h), s);
    QVERIFY(app.runAction(QStringLiteral("Two pins")));
    QCOMPARE(app.pins().size(), 2);
    QCOMPARE(app.pins().last()->textPlain(), QStringLiteral("two"));
    // 命令行：script 文本；返回输出
    const QString out = app.runCommand({QStringLiteral("script"), QStringLiteral("print(ss.command('group list'))")});
    QVERIFY(out.contains(app.pinManager().currentGroupName()));
    QVERIFY(app.runCommand({QStringLiteral("script"), QStringLiteral("nope(")}).startsWith(QStringLiteral("script error")));
}

QTEST_MAIN(TestM6E2)
#include "tst_m6e2.moc"
