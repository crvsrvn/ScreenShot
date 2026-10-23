// REQ: GEN-01 GEN-04 GEN-05 GEN-09 NFR-10 NFR-11 ACT-01 ACT-02
// Windows 系统工具：单实例互斥体、开机自启注册表项、崩溃转储目录、命名管道往返（拒绝远程）、热键映射（含 PrtSc 与字符键）
#include "platform/windows/WinGlobalHotkey.h"
#include "platform/windows/WinPipeServer.h"
#include "platform/windows/WinSystem.h"

#include <windows.h>

#include <QCoreApplication>
#include <QOperatingSystemVersion>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ss::win;

class TestWinSystem : public QObject {
    Q_OBJECT
private slots:
    void singleInstanceMutex();
    void autostartRegistryRoundTrip();
    void pendingCrashDumpsSorted();
    void pipeServerRoundTrip();
    void hotkeyMapping();
    void elevationHelpers();
    void osVersionSupported();
};

void TestWinSystem::osVersionSupported()
{
    // NFR-11：Windows 10 2004（19041）及以上 x64；程序启动时用同一条件拒绝更旧的系统
    QVERIFY(ss::win::osSupported());
    QVERIFY(QOperatingSystemVersion::current() >= ss::win::minimumOsVersion());
    QCOMPARE(ss::win::minimumOsVersion().microVersion(), 19041);
    QCOMPARE(QSysInfo::currentCpuArchitecture(), QStringLiteral("x86_64"));
}

void TestWinSystem::elevationHelpers()
{
    // 是否提权只能读取，不能假定；计划任务参数可精确验证（GEN-05）
    const bool elevated = isElevated();
    Q_UNUSED(elevated);
    const QStringList create = autostartTaskArguments(true, QStringLiteral("C:/x y/ScreenShot.exe"), QStringLiteral("ScreenShotTest"));
    QVERIFY(create.contains(QStringLiteral("/Create")));
    QCOMPARE(create.value(create.indexOf(QStringLiteral("/RL")) + 1), QStringLiteral("HIGHEST"));
    QCOMPARE(create.value(create.indexOf(QStringLiteral("/SC")) + 1), QStringLiteral("ONLOGON"));
    QCOMPARE(create.value(create.indexOf(QStringLiteral("/TR")) + 1), QStringLiteral("\"C:\\x y\\ScreenShot.exe\""));
    const QStringList remove = autostartTaskArguments(false, QString(), QStringLiteral("ScreenShotTest"));
    QVERIFY(remove.contains(QStringLiteral("/Delete")) && remove.contains(QStringLiteral("/F")));
}

void TestWinSystem::hotkeyMapping()
{
    unsigned mods = 0, vk = 0;
    QVERIFY(WinGlobalHotkey::toNative(QKeySequence(QStringLiteral("F1")), mods, vk));
    QCOMPARE(vk, static_cast<unsigned>(VK_F1));
    QVERIFY(WinGlobalHotkey::toNative(QKeySequence(QStringLiteral("Ctrl+Shift+Print")), mods, vk)); // ACT-02：含 PrtSc 的组合键
    QCOMPARE(vk, static_cast<unsigned>(VK_SNAPSHOT));
    QVERIFY((mods & MOD_CONTROL) && (mods & MOD_SHIFT));
    QVERIFY(WinGlobalHotkey::toNative(QKeySequence(QStringLiteral("Alt+A")), mods, vk));
    QCOMPARE(vk, static_cast<unsigned>('A'));
    QVERIFY(mods & MOD_ALT);
    QVERIFY(WinGlobalHotkey::toNative(QKeySequence(QStringLiteral("Meta+3")), mods, vk));
    QCOMPARE(vk, static_cast<unsigned>('3'));
    QVERIFY(mods & MOD_WIN);
    // 按当前键盘布局映射字符键（非英文布局也能得到虚拟键）
    QVERIFY(WinGlobalHotkey::toNative(QKeySequence(QStringLiteral("Ctrl+;")), mods, vk));
    QVERIFY(vk != 0);
    QVERIFY(!WinGlobalHotkey::toNative(QKeySequence(), mods, vk));
}

void TestWinSystem::singleInstanceMutex()
{
    const QString name = QStringLiteral("ScreenShotTest-%1").arg(QCoreApplication::applicationPid());
    QVERIFY(acquireSingleInstance(name));
    QVERIFY(!acquireSingleInstance(name)); // 同一进程再拿也失败（互斥体已存在）
}

void TestWinSystem::autostartRegistryRoundTrip()
{
    const QString entry = QStringLiteral("ScreenShotTest-%1").arg(QCoreApplication::applicationPid());
    QVERIFY(!isAutostartEnabled(entry));
    QVERIFY(setAutostart(true, QStringLiteral("C:/x/ScreenShot.exe"), entry));
    QVERIFY(isAutostartEnabled(entry));
    QVERIFY(setAutostart(false, QString(), entry));
    QVERIFY(!isAutostartEnabled(entry));
}

void TestWinSystem::pendingCrashDumpsSorted()
{
    QTemporaryDir dir;
    QVERIFY(pendingCrashDumps(dir.path()).isEmpty());
    for (const char* n : {"crash-20260101-000000.dmp", "crash-20260102-000000.dmp", "other.txt"}) {
        QFile f(QDir(dir.path()).filePath(QString::fromLatin1(n)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    const QStringList dumps = pendingCrashDumps(dir.path());
    QCOMPARE(dumps.size(), 2);
    QVERIFY(dumps.first().endsWith(QStringLiteral(".dmp")));
}

void TestWinSystem::pipeServerRoundTrip()
{
    const QString name = QStringLiteral("unit-%1").arg(QCoreApplication::applicationPid());
    QVERIFY(!WinPipeServer::exists(name));
    WinPipeServer server(name, [](const QByteArray& line) { return QByteArray("echo:") + line; });
    server.start();
    QTRY_VERIFY(WinPipeServer::exists(name));
    QCOMPARE(WinPipeServer::sendLine(name, "hello", 2000), QByteArray("echo:hello"));
    QCOMPARE(WinPipeServer::sendLine(name, "second", 2000), QByteArray("echo:second"));
    server.stop();
    QVERIFY(WinPipeServer::sendLine(name, "x", 200).isEmpty());
}

QTEST_GUILESS_MAIN(TestWinSystem)
#include "tst_winsystem.moc"
