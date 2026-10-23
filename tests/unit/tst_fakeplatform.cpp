// REQ: TST-03 TST-08
// 测试替身自身的行为：合成显示器、可控失败、挂起模拟、热键触发
#include "testing/FakeElementDetector.h"
#include "testing/FakeHotkey.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestFakePlatform : public QObject {
    Q_OBJECT
private slots:
    void captureAllRespectsLayout();
    void failingScreenIsSkipped();
    void elementDetectorHangRespectsTimeout();
    void hotkeyRegistrationAndTrigger();
    void sixScreens();
};

void TestFakePlatform::captureAllRespectsLayout()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    const auto snaps = p.screenCapture->captureAll();
    QCOMPARE(snaps.size(), 2);
    QCOMPARE(snaps[0].image.size(), QSize(3840, 2160));
    QCOMPARE(snaps[0].image.devicePixelRatio(), 1.5);
    QCOMPARE(snaps[1].image.devicePixelRatio(), 1.0);
    QCOMPARE(snaps[0].image.pixel(5, 6), FakeScreenCapture::patternPixel(0, 5, 6));
    QCOMPARE(h.screenCapture->captureCount(), 2);
}

void TestFakePlatform::failingScreenIsSkipped()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    h.screenCapture->setFailing(0, true);
    const auto snaps = p.screenCapture->captureAll();
    QCOMPARE(snaps.size(), 1);
    QCOMPARE(snaps[0].screen.id, QStringLiteral("FAKE-B"));
}

void TestFakePlatform::elementDetectorHangRespectsTimeout()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    h.elementDetector->setElements({QRect(10, 10, 100, 50)});
    QVERIFY(p.elementDetector->elementAt(QPoint(20, 20), std::chrono::milliseconds(100)).has_value());
    h.elementDetector->setHangMs(5000);
    QElapsedTimer t;
    t.start();
    QVERIFY(!p.elementDetector->elementAt(QPoint(20, 20), std::chrono::milliseconds(50)).has_value());
    QVERIFY(t.elapsed() < 1000);
}

void TestFakePlatform::hotkeyRegistrationAndTrigger()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    h.hotkey->setConflicting(QKeySequence(Qt::Key_F2));
    QVERIFY(p.hotkey->registerHotkey(1, QKeySequence(Qt::Key_F1)));
    QVERIFY(!p.hotkey->registerHotkey(2, QKeySequence(Qt::Key_F2)));
    QSignalSpy spy(p.hotkey.get(), &IGlobalHotkey::triggered);
    h.hotkey->trigger(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QVERIFY(spy.at(0).at(1).toLongLong() > 0);
}

void TestFakePlatform::sixScreens()
{
    // NFR-07：最多 6 台显示器
    QList<ScreenInfo> screens;
    for (int i = 0; i < 6; ++i)
        screens.append(ScreenInfo{QStringLiteral("S%1").arg(i), QRect(i * 1920, 0, 1920, 1080), 1.0 + 0.25 * i, i == 0, false});
    FakeScreenCapture c(screens);
    QCOMPARE(c.captureAll().size(), 6);
}

QTEST_GUILESS_MAIN(TestFakePlatform)
#include "tst_fakeplatform.moc"
