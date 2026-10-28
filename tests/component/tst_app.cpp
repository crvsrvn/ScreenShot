// REQ: GEN-01 GEN-02 GEN-03 CAP-01 CFG-07 PIN-52 ACT-01
// 程序层：托盘菜单内容与点击映射、热键注册与重注册、命令行命令、贴图显示 / 隐藏
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "testing/FakeHotkey.h"
#include "testing/FakePlatform.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestApp : public QObject {
    Q_OBJECT
private slots:
    void trayMenuHasRequiredEntries();
    void preferencesSingleWindow();
    void trayClickMapping();
    void hotkeysRegisteredAndReapplied();
    void hotkeyTriggersCapture();
    void pinHotkeyWhileCapturingPinsSelection();
    void commandLineCommands();
    void pinsHideShowAndNewPinReveals();
};

namespace {
QStringList actionTexts(QMenu* menu)
{
    QStringList out;
    for (QAction* a : menu->actions())
        if (!a->isSeparator())
            out << a->text().section(QLatin1Char('\t'), 0, 0);
    return out;
}
} // namespace

void TestApp::trayMenuHasRequiredEntries()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    QVERIFY(app.trayMenu());
    const QStringList texts = actionTexts(app.trayMenu());
    // GEN-03：右键菜单只有设置和退出
    QCOMPARE(texts, QStringList({QStringLiteral("Settings"), QStringLiteral("Quit")}));
}

void TestApp::preferencesSingleWindow()
{
    // 多次点“设置”只有一个设置窗口
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    app.showPreferences();
    app.showPreferences();
    int count = 0;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (w->inherits("ss::PreferencesDialog") && w->isVisible())
            ++count;
    QCOMPARE(count, 1);
}

void TestApp::trayClickMapping()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    // 左键截图
    app.trayActivated(QSystemTrayIcon::Trigger);
    QVERIFY(app.session().isActive());
    app.session().cancel();
    // 中键贴图：剪贴板为空时不创建
    app.trayActivated(QSystemTrayIcon::MiddleClick);
    QCOMPARE(app.pins().size(), 0);
}

void TestApp::hotkeysRegisteredAndReapplied()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    QVERIFY(h.hotkey->isRegistered(App::HotkeyCapture));
    QVERIFY(h.hotkey->isRegistered(App::HotkeyPin));
    QVERIFY(h.hotkey->isRegistered(App::HotkeyTogglePins));
    QCOMPARE(app.hotkeyStatus().value(App::HotkeyCapture), true);
    // 设置改变立即重注册（CFG-07）；冲突的热键报告失败
    h.hotkey->setConflicting(QKeySequence(Qt::Key_F7));
    settings.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("F7"));
    QCOMPARE(app.hotkeyStatus().value(App::HotkeyCapture), false);
    settings.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("F1"));
    QCOMPARE(app.hotkeyStatus().value(App::HotkeyCapture), true);
}

void TestApp::hotkeyTriggersCapture()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(app.session().isActive());
    QVERIFY(app.session().timings().triggerNs > 0);
    app.session().cancel();
}

void TestApp::pinHotkeyWhileCapturingPinsSelection()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    QApplication::clipboard()->clear();
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(app.session().isActive());
    app.session().setSelection(QRect(100, 100, 80, 60));
    // 截图中按贴图热键：贴出当前选区并结束截图，而不是去贴剪贴板
    h.hotkey->trigger(App::HotkeyPin);
    QVERIFY(!app.session().isActive());
    QCOMPARE(app.pins().size(), 1);
    QCOMPARE(app.pins().first()->originalImage().size(), QSize(80, 60));
}

void TestApp::commandLineCommands()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    app.runCommand({QStringLiteral("snip")});
    QVERIFY(app.session().isActive());
    app.session().cancel();
    app.runCommand({QStringLiteral("toggle-pins")});
    QVERIFY(app.pinsHidden());
    app.runCommand({QStringLiteral("toggle-pins")});
    QVERIFY(!app.pinsHidden());
    app.runCommand({QStringLiteral("bogus")}); // 未知命令只记日志
    QVERIFY(!app.session().isActive());
}

void TestApp::pinsHideShowAndNewPinReveals()
{
    FakePlatformHandles h;
    Settings settings;
    App app(makeFakePlatform(&h), settings);
    QImage img(50, 40, QImage::Format_RGB32);
    img.fill(Qt::red);
    PinWindow* a = app.pinImage(img, QPoint(10, 10));
    QVERIFY(a->isVisible());
    app.togglePinsHidden();
    QVERIFY(!a->isVisible());
    QVERIFY(app.pinsHidden());
    app.togglePinsHidden();
    QVERIFY(a->isVisible());
    app.togglePinsHidden();
    QApplication::clipboard()->setImage(img);
    app.pinFromClipboard(); // 新建贴图时自动重新显示（PIN-52）
    QCOMPARE(app.pins().size(), 2);
    QVERIFY(!app.pinsHidden());
    QVERIFY(a->isVisible());
    app.closeAllPins();
    QCOMPARE(app.pins().size(), 0);
}

QTEST_MAIN(TestApp)
#include "tst_app.moc"
