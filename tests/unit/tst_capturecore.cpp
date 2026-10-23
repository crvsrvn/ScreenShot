// REQ: CAP-10 CAP-11 CAP-21 CAP-23 CAP-24 CAP-40 OUT-08
// 截图核心的纯逻辑：选区几何、窗口查找、历史游标、颜色格式、文件命名、放大镜绘制
#include "core/capture/CaptureHistory.h"
#include "core/capture/Magnifier.h"
#include "core/capture/SelectionGeometry.h"
#include "core/capture/WindowFinder.h"
#include "core/color/ColorFormat.h"
#include "core/output/FileNaming.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QTemporaryDir>
#include <QTest>

using namespace ss;
using namespace ss::testing;

class TestCaptureCore : public QObject {
    Q_OBJECT
private slots:
    void hitTestHandles();
    void resizeAcrossOppositeEdge();
    void moveClampsToBounds();
    void nudgeMoveExpandShrink();
    void windowFinderPicksTopmostAndClips();
    void historyLimitAndCursor();
    void colorFormats();
    void fileNamePattern();
    void uniquePath();
    void magnifierRendersCenterPixel();
};

void TestCaptureCore::hitTestHandles()
{
    const QRect s(100, 100, 200, 100);
    QCOMPARE(hitTestSelection(s, QPoint(100, 100), 6), SelectionHandle::TopLeft);
    QCOMPARE(hitTestSelection(s, QPoint(200, 98), 6), SelectionHandle::Top);
    QCOMPARE(hitTestSelection(s, QPoint(299, 199), 6), SelectionHandle::BottomRight);
    QCOMPARE(hitTestSelection(s, QPoint(102, 150), 6), SelectionHandle::Left);
    QCOMPARE(hitTestSelection(s, QPoint(150, 150), 6), SelectionHandle::Inside);
    QCOMPARE(hitTestSelection(s, QPoint(10, 10), 6), SelectionHandle::Outside);
    QCOMPARE(hitTestSelection(QRect(), QPoint(10, 10), 6), SelectionHandle::Outside);
    QCOMPARE(selectionHandlePoints(s).size(), 8);
}

void TestCaptureCore::resizeAcrossOppositeEdge()
{
    const QRect s(100, 100, 50, 50); // right=149, bottom=149
    QCOMPARE(resizeSelection(s, SelectionHandle::Right, QPoint(10, 0)), QRect(100, 100, 60, 50));
    QCOMPARE(resizeSelection(s, SelectionHandle::TopLeft, QPoint(-5, -5)), QRect(95, 95, 55, 55));
    // 把右边拖过左边：规范化
    QCOMPARE(resizeSelection(s, SelectionHandle::Right, QPoint(-60, 0)), QRect(QPoint(89, 100), QPoint(100, 149)));
    QCOMPARE(resizeSelection(s, SelectionHandle::Inside, QPoint(5, 5)), s);
}

void TestCaptureCore::moveClampsToBounds()
{
    const QRect bounds(0, 0, 1000, 500);
    QCOMPARE(moveSelection(QRect(10, 10, 100, 100), QPoint(-50, -50), bounds), QRect(0, 0, 100, 100));
    QCOMPARE(moveSelection(QRect(950, 10, 100, 100), QPoint(30, 0), bounds), QRect(900, 10, 100, 100));
    QCOMPARE(moveSelection(QRect(10, 10, 100, 100), QPoint(5, 5), QRect()), QRect(15, 15, 100, 100));
}

void TestCaptureCore::nudgeMoveExpandShrink()
{
    const QRect bounds(0, 0, 100, 100);
    const QRect s(10, 10, 20, 20);
    QCOMPARE(nudgeSelection(s, Qt::Key_Right, NudgeKind::Move, bounds), QRect(11, 10, 20, 20));
    QCOMPARE(nudgeSelection(s, Qt::Key_Right, NudgeKind::Expand, bounds), QRect(10, 10, 21, 20));
    QCOMPARE(nudgeSelection(s, Qt::Key_Left, NudgeKind::Expand, bounds), QRect(9, 10, 21, 20));
    QCOMPARE(nudgeSelection(s, Qt::Key_Up, NudgeKind::Shrink, bounds), QRect(10, 11, 20, 19));
    QCOMPARE(nudgeSelection(QRect(10, 10, 1, 1), Qt::Key_Down, NudgeKind::Shrink, bounds), QRect(10, 10, 1, 1));
    QCOMPARE(nudgeSelection(QRect(0, 0, 10, 10), Qt::Key_Left, NudgeKind::Expand, bounds), QRect(0, 0, 10, 10));
}

void TestCaptureCore::windowFinderPicksTopmostAndClips()
{
    QList<WindowInfo> windows{
        WindowInfo{1, QStringLiteral("back"), QStringLiteral("a.exe"), QRect(0, 0, 500, 500), 1},
        WindowInfo{2, QStringLiteral("front"), QStringLiteral("b.exe"), QRect(100, 100, 600, 100), 0},
    };
    WindowFinder f(windows, QRect(0, 0, 640, 480));
    QCOMPARE(f.windowAt(QPoint(150, 150))->title, QStringLiteral("front"));
    QCOMPARE(f.windowAt(QPoint(150, 150))->frame, QRect(100, 100, 540, 100)); // 裁到桌面
    QCOMPARE(f.windowAt(QPoint(50, 50))->title, QStringLiteral("back"));
    QVERIFY(!f.windowAt(QPoint(600, 400)).has_value());
}

void TestCaptureCore::historyLimitAndCursor()
{
    CaptureHistory h(2);
    QImage img(2, 2, QImage::Format_RGB32);
    h.add({img, QRect(0, 0, 2, 2), QDateTime::currentDateTime()});
    h.add({img, QRect(1, 1, 2, 2), QDateTime::currentDateTime()});
    h.add({img, QRect(2, 2, 2, 2), QDateTime::currentDateTime()});
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.at(0).physicalRegion, QRect(2, 2, 2, 2)); // 最新在前
    HistoryCursor c(h);
    QVERIFY(!c.viewingHistory());
    QVERIFY(c.older());
    QCOMPARE(c.index(), 0);
    QVERIFY(c.older());
    QCOMPARE(c.index(), 1);
    QVERIFY(!c.older());
    QVERIFY(c.newer());
    QVERIFY(c.newer());
    QVERIFY(!c.viewingHistory());
    QVERIFY(!c.newer());
    CaptureHistory off(0);
    off.add({img, QRect(), QDateTime::currentDateTime()});
    QVERIFY(off.isEmpty());
}

void TestCaptureCore::colorFormats()
{
    const QColor c(255, 128, 0);
    QCOMPARE(formatColor(c, ColorFormat::Rgb), QStringLiteral("rgb(255, 128, 0)"));
    QCOMPARE(formatColor(c, ColorFormat::Hex, true), QStringLiteral("#FF8000"));
    QCOMPARE(formatColor(c, ColorFormat::Hex, false), QStringLiteral("#ff8000"));
    QCOMPARE(formatColor(c, ColorFormat::Hsv), QStringLiteral("hsv(30, 100%, 100%)"));
    QCOMPARE(formatColor(c, ColorFormat::Hsl), QStringLiteral("hsl(30, 100%, 50%)"));
    QCOMPARE(formatColor(c, ColorFormat::Hsi), QStringLiteral("hsi(30, 100%, 50%)"));
    QCOMPARE(formatColor(QColor(128, 128, 128), ColorFormat::Hsi), QStringLiteral("hsi(0, 0%, 50%)"));
    QCOMPARE(nextColorFormat(ColorFormat::Hsi), ColorFormat::Rgb);
    QCOMPARE(colorFormatFromName(QStringLiteral("HSL")), ColorFormat::Hsl);
    QCOMPARE(colorFormatName(colorFormatFromName(QStringLiteral("bogus"))), QStringLiteral("hex"));
}

void TestCaptureCore::fileNamePattern()
{
    FileNameContext ctx;
    ctx.time = QDateTime(QDate(2026, 10, 23), QTime(20, 30, 45));
    ctx.imageSize = QSize(640, 480);
    ctx.sequence = 7;
    QCOMPARE(expandFileNamePattern(QStringLiteral("Shot_{date}_{time}"), ctx), QStringLiteral("Shot_2026-10-23_20-30-45"));
    QCOMPARE(expandFileNamePattern(QStringLiteral("{date:yyyyMMdd}-{width}x{height}-{n}"), ctx), QStringLiteral("20261023-640x480-7"));
    QCOMPARE(expandFileNamePattern(QStringLiteral("a/b:c*{unknown}"), ctx), QStringLiteral("a/b_c_{unknown}")); // 模板里的 / 是子目录（OUT-10）
    QCOMPARE(expandFileNamePattern(QStringLiteral("   "), ctx), QStringLiteral("Screenshot"));
    QCOMPARE(expandFileNamePattern(QStringLiteral("{timestamp}"), ctx), QString::number(ctx.time.toSecsSinceEpoch()));
}

void TestCaptureCore::uniquePath()
{
    QTemporaryDir dir;
    const QString first = uniqueFilePath(dir.path(), QStringLiteral("x"), QStringLiteral("png"));
    QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("x.png"));
    QFile f(first);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QCOMPARE(QFileInfo(uniqueFilePath(dir.path(), QStringLiteral("x"), QStringLiteral("png"))).fileName(), QStringLiteral("x (2).png"));
}

void TestCaptureCore::magnifierRendersCenterPixel()
{
    FakePlatformHandles h;
    Platform p = makeFakePlatform(&h);
    VirtualDesktop desktop(p.screenCapture->captureAll());
    MagnifierOptions o;
    o.zoom = 4;
    o.pixels = 5;
    o.showHint = false;
    const QSize size = magnifierSize(o);
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter painter(&img);
        paintMagnifier(painter, QPointF(0, 0), desktop, QPoint(100, 100), QRect(0, 0, 10, 10), o, QStringLiteral("#000000"));
    }
    // 放大区域中心像素块的颜色等于光标处像素（块内部不含网格线的位置）
    const QRgb expected = FakeScreenCapture::patternPixel(0, 100, 100);
    const int pad = 6;
    const int cx = pad + 2 * o.zoom + 1, cy = pad + 2 * o.zoom + 1;
    QCOMPARE(qRed(img.pixel(cx, cy)), qRed(expected));
    QCOMPARE(qGreen(img.pixel(cx, cy)), qGreen(expected));
    // 放置：靠近右下角时翻到左上
    const QPointF place = magnifierPlacement(QPointF(790, 590), size, QRectF(0, 0, 800, 600), 20);
    QVERIFY(place.x() + size.width() <= 800 && place.y() + size.height() <= 600);
}

QTEST_MAIN(TestCaptureCore)
#include "tst_capturecore.moc"
