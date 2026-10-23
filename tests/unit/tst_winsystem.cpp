// REQ: GEN-01 GEN-04 GEN-09 NFR-10
// Windows 系统工具：单实例互斥体、开机自启注册表项、崩溃转储目录、命名管道往返（拒绝远程）
#include "platform/windows/WinPipeServer.h"
#include "platform/windows/WinSystem.h"

#include <QCoreApplication>
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
};

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
