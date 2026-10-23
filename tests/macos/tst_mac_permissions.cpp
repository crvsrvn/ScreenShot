// REQ: GEN-14 NFR-16
// macOS 权限（只在 macOS 上编译运行；目前没有设备，未验证）：
// 没有屏幕录制权限时捕获返回空而不崩溃；权限查询接口可调用。
#include "platform/macos/MacPermissions.h"
#include "platform/macos/MacScreenCapture.h"

#include <QTest>

class TestMacPermissions : public QObject {
    Q_OBJECT
private slots:
    void queriesDoNotCrash();
    void captureWithoutPermissionFailsGracefully();
};

void TestMacPermissions::queriesDoNotCrash()
{
    const bool screen = ss::mac::hasScreenRecordingPermission();
    const bool ax = ss::mac::hasAccessibilityPermission();
    qInfo("screen recording=%d accessibility=%d", screen, ax);
}

void TestMacPermissions::captureWithoutPermissionFailsGracefully()
{
    ss::mac::MacScreenCapture capture;
    const auto screens = capture.screens();
    QVERIFY(!screens.isEmpty());
    const auto snap = capture.capture(screens.first());
    if (!ss::mac::hasScreenRecordingPermission())
        QVERIFY(!snap.has_value());
    else
        QVERIFY(snap.has_value());
}

QTEST_GUILESS_MAIN(TestMacPermissions)
#include "tst_mac_permissions.moc"
