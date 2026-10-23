// REQ: GEN-09
#include "core/util/Log.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace ss;

class TestLog : public QObject {
    Q_OBJECT
private slots:
    void writesFilteredLines();
    void rotatesBySize();
};

void TestLog::writesFilteredLines()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("logs/app.log"));
    log::Options o;
    o.filePath = path;
    o.minLevel = QtInfoMsg;
    log::install(o);
    qDebug("debug hidden");
    qInfo("info shown %d", 1);
    qWarning("warn shown");
    log::uninstall();
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY(!text.contains(QStringLiteral("debug hidden")));
    QVERIFY(text.contains(QStringLiteral("INFO ")) && text.contains(QStringLiteral("info shown 1")));
    QVERIFY(text.contains(QStringLiteral("WARN ")) && text.contains(QStringLiteral("warn shown")));
}

void TestLog::rotatesBySize()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("app.log"));
    log::Options o;
    o.filePath = path;
    o.maxBytes = 2000;
    o.flushIntervalMs = 0;
    log::install(o);
    for (int i = 0; i < 100; ++i)
        qInfo("line %03d padding padding padding padding padding", i);
    log::uninstall();
    QVERIFY(QFile::exists(path + QStringLiteral(".1")));
    QVERIFY(QFileInfo(path + QStringLiteral(".1")).size() >= 2000);
    QVERIFY(QFileInfo(path).size() < 2000 + 200);
}

QTEST_GUILESS_MAIN(TestLog)
#include "tst_log.moc"
