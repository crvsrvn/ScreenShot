// REQ: OUT-02 OUT-05 OUT-06 OUT-10
// M5 输出 P1：路径变量与修饰符、复制为文件、质量 / 透明通道、保存记忆
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/OutputActions.h"
#include "core/config/Settings.h"
#include "core/output/FileNaming.h"
#include "testing/FakePlatform.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestOutputP1 : public QObject {
    Q_OBJECT
private slots:
    void fileNameVariablesAndModifiers();
    void copyAsFile();
    void alphaDroppedAndQuality();
    void sourceWindowInFileName();
};

void TestOutputP1::fileNameVariablesAndModifiers()
{
    FileNameContext ctx;
    ctx.time = QDateTime(QDate(2026, 10, 23), QTime(21, 5, 9));
    ctx.imageSize = QSize(800, 600);
    ctx.sequence = 3;
    ctx.windowTitle = QStringLiteral("Report: Q4/2026 - Editor");
    ctx.processName = QStringLiteral("notepad.exe");
    ctx.computerName = QStringLiteral("DEV-PC");
    QCOMPARE(expandFileNamePattern(QStringLiteral("{title|trunc:6}_{process|noext}_{computer}"), ctx), QStringLiteral("Report_notepad_DEV-PC"));
    QCOMPARE(expandFileNamePattern(QStringLiteral("{date:yyyy}/{date:MM}/{time:HHmmss}_{width}x{height}_{n}"), ctx), QStringLiteral("2026/10/210509_800x600_3"));
    // 变量值里的斜杠不会变成子目录
    QCOMPARE(expandFileNamePattern(QStringLiteral("{title}"), ctx), QStringLiteral("Report_ Q4_2026 - Editor"));
    qputenv("SS_TEST_VAR", "abc");
    QCOMPARE(expandFileNamePattern(QStringLiteral("x-{env:SS_TEST_VAR}-{process|noext|upper}"), ctx), QStringLiteral("x-abc-NOTEPAD"));
    QVERIFY(expandFileNamePattern(QStringLiteral("{uuid}"), ctx).size() >= 32);
    QCOMPARE(expandFileNamePattern(QStringLiteral("{unknown}"), ctx), QStringLiteral("{unknown}"));
}

void TestOutputP1::copyAsFile()
{
    Settings s;
    OutputActions out(s);
    QImage img(10, 10, QImage::Format_ARGB32);
    img.fill(qRgba(255, 0, 0, 255));
    s.setValue(QStringLiteral("output.copyAsFile"), QStringLiteral("off"));
    QVERIFY(out.copy(img));
    QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
    s.setValue(QStringLiteral("output.copyAsFile"), QStringLiteral("auto"));
    QVERIFY(out.copy(img)); // 不透明：auto 不附带文件
    QVERIFY(!QApplication::clipboard()->mimeData()->hasUrls());
    img.setPixel(0, 0, qRgba(0, 0, 0, 0));
    QVERIFY(out.copy(img)); // 有透明像素：附带 PNG 文件
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    QVERIFY(mime->hasUrls() && mime->hasImage());
    const QString path = mime->urls().first().toLocalFile();
    QVERIFY(QFile::exists(path) && path.endsWith(QStringLiteral(".png")));
    QCOMPARE(QImage(path).size(), QSize(10, 10));
    s.setValue(QStringLiteral("output.copyAsFile"), QStringLiteral("jpg"));
    QVERIFY(out.copy(img));
    QVERIFY(QApplication::clipboard()->mimeData()->urls().first().toLocalFile().endsWith(QStringLiteral(".jpg")));
}

void TestOutputP1::alphaDroppedAndQuality()
{
    Settings s;
    OutputActions out(s);
    QTemporaryDir dir;
    QImage opaque(8, 8, QImage::Format_ARGB32);
    opaque.fill(qRgba(1, 2, 3, 255));
    QVERIFY(!OutputActions::hasTranslucentPixels(opaque));
    const QString p1 = QDir(dir.path()).filePath(QStringLiteral("a.png"));
    QVERIFY(out.writeImage(opaque, p1, QStringLiteral("png")));
    QVERIFY(!QImage(p1).hasAlphaChannel()); // 透明通道已去掉
    QImage translucent = opaque;
    translucent.setPixel(1, 1, qRgba(1, 2, 3, 100));
    QVERIFY(OutputActions::hasTranslucentPixels(translucent));
    const QString p2 = QDir(dir.path()).filePath(QStringLiteral("b.png"));
    QVERIFY(out.writeImage(translucent, p2, QStringLiteral("png")));
    QVERIFY(QImage(p2).hasAlphaChannel());
    // 质量：指定低质量的 JPEG 比自动质量的小
    QImage noisy(200, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
            noisy.setPixel(x, y, qRgb((x * 37) & 255, (y * 91) & 255, (x ^ y) & 255));
    s.setValue(QStringLiteral("output.qualityAuto"), true);
    const QString q1 = QDir(dir.path()).filePath(QStringLiteral("auto.jpg"));
    QVERIFY(out.writeImage(noisy, q1, QStringLiteral("jpg")));
    s.setValue(QStringLiteral("output.qualityAuto"), false);
    s.setValue(QStringLiteral("output.jpegQuality"), 10);
    const QString q2 = QDir(dir.path()).filePath(QStringLiteral("low.jpg"));
    QVERIFY(out.writeImage(noisy, q2, QStringLiteral("jpg")));
    QVERIFY(QFileInfo(q2).size() < QFileInfo(q1).size());
}

void TestOutputP1::sourceWindowInFileName()
{
    Settings s;
    s.setValue(QStringLiteral("output.fileNamePattern"), QStringLiteral("{process|noext}_{title|trunc:5}"));
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    h.windowDetector->setWindows({WindowInfo{1, QStringLiteral("Hello World"), QStringLiteral("app.exe"), QRect(0, 0, 500, 500), 0}});
    OutputActions out(s);
    CaptureSession session(p.screenCapture.get(), p.windowDetector.get(), p.elementDetector.get(), s);
    session.setOutput(&out);
    QVERIFY(session.start(1, false));
    session.setSelection(QRect(10, 10, 20, 20));
    session.accept(); // 输出时记下来源窗口
    QCOMPARE(out.makeFileName(QImage(20, 20, QImage::Format_RGB32)), QStringLiteral("app_Hello"));
}

QTEST_MAIN(TestOutputP1)
#include "tst_output_p1.moc"
