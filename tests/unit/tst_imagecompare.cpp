// REQ: TST-04
#include "testing/ImageCompare.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace ss::testing;

class TestImageCompare : public QObject {
    Q_OBJECT
private slots:
    void identicalPasses();
    void sizeMismatchFails();
    void toleranceAndCount();
    void artifactsAreWritten();
};

void TestImageCompare::identicalPasses()
{
    QImage a(10, 10, QImage::Format_RGB32);
    a.fill(Qt::green);
    const CompareResult r = compareImages(a, a);
    QVERIFY(r.passed);
    QCOMPARE(r.differingPixels, 0);
}

void TestImageCompare::sizeMismatchFails()
{
    QImage a(10, 10, QImage::Format_RGB32);
    QImage b(11, 10, QImage::Format_RGB32);
    QVERIFY(compareImages(a, b).sizeMismatch);
    QVERIFY(!compareImages(a, b).passed);
}

void TestImageCompare::toleranceAndCount()
{
    QImage a(4, 4, QImage::Format_RGB32);
    a.fill(qRgb(100, 100, 100));
    QImage b = a;
    b.setPixel(1, 2, qRgb(103, 100, 100)); // 差 3
    b.setPixel(3, 3, qRgb(100, 150, 100)); // 差 50
    CompareResult r = compareImages(a, b);
    QCOMPARE(r.differingPixels, 2);
    QCOMPARE(r.maxChannelDiff, 50);
    QCOMPARE(r.firstDiff, QPoint(1, 2));
    QVERIFY(!r.passed);
    r = compareImages(a, b, CompareOptions{3, 0});
    QCOMPARE(r.differingPixels, 1);
    r = compareImages(a, b, CompareOptions{3, 1});
    QVERIFY(r.passed);
}

void TestImageCompare::artifactsAreWritten()
{
    QTemporaryDir dir;
    QImage a(5, 5, QImage::Format_RGB32);
    a.fill(Qt::red);
    QImage b = a;
    b.setPixel(0, 0, qRgb(0, 0, 255));
    QVERIFY(saveFailureArtifacts(dir.path(), QStringLiteral("case"), a, b));
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("case.actual.png"))));
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("case.expected.png"))));
    const QImage diff(QDir(dir.path()).filePath(QStringLiteral("case.diff.png")));
    QCOMPARE(diff.pixel(0, 0), qRgb(255, 0, 0));
    QVERIFY(diff.pixel(1, 1) != qRgb(255, 0, 0));
}

QTEST_GUILESS_MAIN(TestImageCompare)
#include "tst_imagecompare.moc"
