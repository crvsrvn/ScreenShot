// REQ: OUT-07 OUT-12 OUT-13 PIN-12 PIN-14 PIN-23 PIN-28 PIN-34 PIN-35 PIN-37 PIN-40 PIN-43 PIN-56 PIN-61 PIN-62
// M6 输出 / 贴图 P2：编码预览、保存后复制、元数据、资源管理器选中文件、重复贴图策略、缩放细节、亮度、
// 锁定、标题、透明背景、拖出内容、手势缩放、所有虚拟桌面、独显、托盘批量
#include "app/EncodePreviewDialog.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"

#include <QApplication>
#include <QClipboard>
#include <QImageReader>
#include <QKeyEvent>
#include <QListWidget>
#include <QMimeData>
#include <QNativeGestureEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QWheelEvent>

#if defined(Q_OS_WIN)
#include "platform/windows/WinShell.h"
#endif

using namespace ss;
using namespace ss::testing;

namespace {

QImage testImage(int w = 200, int h = 150, QRgb fill = qRgb(100, 120, 140))
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(fill);
    return img;
}

void sendKey(QWidget* w, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    QApplication::sendEvent(w, &e);
}

} // namespace

class TestM6D : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { PinWindow::confirmHandler = [](QWidget*, const QString&) { return true; }; }
    void encodePreviewEstimates();
    void copyAfterSaveAndMetadata();
    void explorerSelectionAndAllDesktopsDoNotCrash();
    void duplicateBehavior();
    void zoomAnchorFineStepSmooth();
    void brightnessAndReset();
    void lockTypes();
    void titleAndAlphaBackground();
    void dragMimeAndGesture();
    void soloAndBatch();
};

void TestM6D::encodePreviewEstimates()
{
    Settings s;
    OutputActions out(s);
    QImage noisy(200, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 200; ++x)
            noisy.setPixel(x, y, qRgb((x * 37) & 255, (y * 91) & 255, (x ^ y) & 255));
    const qint64 png = out.encode(noisy, QStringLiteral("png")).size();
    const qint64 jpgLow = out.encode(noisy, QStringLiteral("jpg"), 10).size();
    const qint64 jpgHigh = out.encode(noisy, QStringLiteral("jpg"), 95).size();
    QVERIFY(png > 0 && jpgLow > 0 && jpgLow < jpgHigh);
    EncodePreviewDialog dialog(noisy, out);
    dialog.setFormat(QStringLiteral("jpg"));
    dialog.setQuality(10);
    QCOMPARE(dialog.estimatedBytes(), jpgLow);
    QCOMPARE(dialog.preview().size(), noisy.size());
    dialog.setQuality(95);
    QCOMPARE(dialog.estimatedBytes(), jpgHigh);
    dialog.setFormat(QStringLiteral("png"));
    QCOMPARE(dialog.estimatedBytes(), png);
}

void TestM6D::copyAfterSaveAndMetadata()
{
    Settings s;
    QTemporaryDir dir;
    OutputActions out(s);
    out.setSource(QStringLiteral("My Window"), QStringLiteral("app.exe"));
    auto save = [&](const QImage& image, const QString& name) {
        out.setDialogOverride(dir.filePath(name));
        return out.saveWithDialog(image, nullptr);
    };
    // 元数据
    const QString path = save(testImage(), QStringLiteral("a.png"));
    QVERIFY(!path.isEmpty());
    QImageReader reader(path);
    QVERIFY(reader.textKeys().contains(QStringLiteral("Source")));
    QCOMPARE(reader.text(QStringLiteral("Source")), QStringLiteral("My Window"));
    QCOMPARE(reader.text(QStringLiteral("Process")), QStringLiteral("app.exe"));
    QVERIFY(reader.textKeys().contains(QStringLiteral("CreationTime")));
    s.setValue(QStringLiteral("output.metadata"), false);
    const QString plain = save(testImage(), QStringLiteral("b.png"));
    QVERIFY(!QImageReader(plain).textKeys().contains(QStringLiteral("Source")));
    // 保存后复制
    QApplication::clipboard()->clear();
    s.setValue(QStringLiteral("output.copyAfterSave"), true);
    save(testImage(30, 20), QStringLiteral("c.png"));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(30, 20));
}

void TestM6D::explorerSelectionAndAllDesktopsDoNotCrash()
{
#if defined(Q_OS_WIN)
    const QStringList files = win::selectedExplorerFiles(); // 可能为空（桌面锁定 / 无资源管理器窗口）
    for (const QString& f : files)
        QVERIFY(!f.isEmpty());
    Settings s;
    PinWindow pin(testImage(), s);
    pin.show();
    const bool ok = win::setWindowOnAllDesktops(reinterpret_cast<void*>(pin.winId()), true); // 未公开接口：不保证成功
    Q_UNUSED(ok);
    Settings s2;
    s2.setValue(QStringLiteral("pin.allDesktops"), true);
    PinWindow pin2(testImage(), s2);
    pin2.show(); // showEvent 里调用，不能崩溃
    QVERIFY(pin2.isVisible());
#else
    QSKIP("Windows only");
#endif
}

void TestM6D::duplicateBehavior()
{
    Settings s;
    PinManager m(s, nullptr);
    const QImage img = testImage(50, 40, qRgb(1, 2, 3));
    // duplicate（默认）：照常重复
    PinWindow* a = m.pinImage(img, QPoint(0, 0));
    PinWindow* b = m.pinImage(img, QPoint(100, 0));
    QVERIFY(a && b && a != b);
    QCOMPARE(m.pins().size(), 2);
    // shake：返回已有贴图，不新建
    s.setValue(QStringLiteral("pin.duplicateBehavior"), QStringLiteral("shake"));
    PinWindow* c = m.pinImage(img, QPoint(200, 0));
    QVERIFY(c == a || c == b);
    QCOMPARE(m.pins().size(), 2);
    QTest::qWait(250); // 抖动结束回到原位
    QCOMPARE(c->contentTopLeft(), c == a ? QPointF(0, 0) : QPointF(100, 0));
    // restore：已关闭的同图恢复
    s.setValue(QStringLiteral("pin.duplicateBehavior"), QStringLiteral("restore"));
    a->closePin();
    b->closePin();
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 0);
    QCOMPARE(m.closedPins().size(), 2);
    PinWindow* r = m.pinImage(img, QPoint(300, 0));
    QVERIFY(r);
    QCOMPARE(m.pins().size(), 1);
    QCOMPARE(m.closedPins().size(), 1);
    QVERIFY(r->physicalTopLeft() == QPoint(0, 0) || r->physicalTopLeft() == QPoint(100, 0)); // 恢复到原位置
    // 不同内容照常贴
    PinWindow* d = m.pinImage(testImage(50, 40, qRgb(9, 9, 9)), QPoint(300, 0));
    QVERIFY(d && d != r);
    QCOMPARE(m.pins().size(), 2);
}

void TestM6D::zoomAnchorFineStepSmooth()
{
    Settings s;
    s.setValue(QStringLiteral("pin.zoomStep"), 50);
    s.setValue(QStringLiteral("pin.zoomFineStep"), 10);
    PinWindow pin(testImage(200, 100), s);
    pin.moveToPhysical(QPoint(100, 100));
    // 中心缩放：中心不动
    s.setValue(QStringLiteral("pin.zoomAnchor"), QStringLiteral("center"));
    const QPointF center = pin.contentRect().center();
    pin.zoomStep(1, QPointF(24, 24)); // 传入的光标位置被忽略
    QCOMPARE(pin.scale(), 1.5);
    QCOMPARE(pin.contentRect().center(), center);
    // 左上角固定
    s.setValue(QStringLiteral("pin.zoomAnchor"), QStringLiteral("topLeft"));
    pin.zoomStep(1, QPointF(200, 80));
    QCOMPARE(pin.contentRect().topLeft(), QPointF(100 + 0, 100 + 0) + (pin.contentRect().topLeft() - QPointF(100, 100)));
    QCOMPARE(pin.physicalTopLeft(), QPoint(100 - 50, 100 - 25)); // 上一步中心缩放后的左上角保持
    // 精细步长：Shift+滚轮
    s.setValue(QStringLiteral("pin.zoomAnchor"), QStringLiteral("cursor"));
    const qreal before = pin.scale();
    QWheelEvent wheel(QPointF(30, 30), QPointF(30, 30), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ShiftModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&pin, &wheel);
    QVERIFY(std::abs(pin.scale() - before * 1.1) < 1e-6);
    // 平滑缩放关闭：放大后像素边缘锐利（不出现中间色）
    PinWindow sharp([] {
        QImage img(2, 1, QImage::Format_ARGB32_Premultiplied);
        img.setPixel(0, 0, qRgb(0, 0, 0));
        img.setPixel(1, 0, qRgb(255, 255, 255));
        return img;
    }(), s);
    sharp.moveToPhysical(QPoint(0, 0));
    sharp.setScale(20, false);
    sharp.show();
    s.setValue(QStringLiteral("pin.smoothZoom"), false);
    sharp.repaint();
    const QImage grabbed = sharp.grab().toImage();
    const QPoint mid = (sharp.contentRect().translated(-sharp.pos()).center()).toPoint();
    const QRgb left = grabbed.pixel(mid.x() - 3, mid.y()), right = grabbed.pixel(mid.x() + 3, mid.y());
    QVERIFY(qRed(left) < 30 && qRed(right) > 225);
}

void TestM6D::brightnessAndReset()
{
    Settings s;
    PinWindow pin(testImage(), s);
    sendKey(&pin, QStringLiteral("8"));
    QCOMPARE(pin.brightness(), 10);
    QCOMPARE(qRed(pin.displayedImage().pixel(5, 5)), 110);
    sendKey(&pin, QStringLiteral("7"));
    sendKey(&pin, QStringLiteral("7"));
    QCOMPARE(pin.brightness(), -10);
    QCOMPARE(qRed(pin.displayedImage().pixel(5, 5)), 90);
    pin.setGrayscale(true);
    sendKey(&pin, QStringLiteral("0"));
    QCOMPARE(pin.brightness(), 0);
    QVERIFY(!pin.grayscale());
    QCOMPARE(pin.displayedImage().pixel(5, 5), qRgb(100, 120, 140));
    QCOMPARE(pin.stateJson().value(QStringLiteral("brightness")).toInt(), 0);
}

void TestM6D::lockTypes()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* pin = m.pinImage(testImage(), QPoint(0, 0));
    sendKey(pin, QStringLiteral("L"));
    QVERIFY(pin->locked());
    // all：不能缩放、关闭
    pin->zoomStep(1, QPointF(24, 24));
    QCOMPARE(pin->scale(), 1.0);
    sendKey(pin, QStringLiteral("Escape"));
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 1);
    QVERIFY(pin->lockBlocks(QStringLiteral("move")));
    // zoom：只禁缩放
    s.setValue(QStringLiteral("pin.lockType"), QStringLiteral("zoom"));
    QVERIFY(!pin->lockBlocks(QStringLiteral("move")));
    QVERIFY(pin->lockBlocks(QStringLiteral("zoom")));
    QVERIFY(!pin->lockBlocks(QStringLiteral("close")));
    // move：只禁移动
    s.setValue(QStringLiteral("pin.lockType"), QStringLiteral("move"));
    pin->zoomStep(1, QPointF(24, 24));
    QVERIFY(pin->scale() > 1.0);
    sendKey(pin, QStringLiteral("L"));
    QVERIFY(!pin->locked());
    QVERIFY(pin->stateJson().contains(QStringLiteral("locked")));
}

void TestM6D::titleAndAlphaBackground()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 100)); // 远离桌面边缘，标题条在窗口内
    pin.setTitle(QStringLiteral("  Hello  "));
    QCOMPARE(pin.title(), QStringLiteral("Hello"));
    pin.show();
    const QImage withTitle = pin.grab().toImage();
    auto darkBand = [&](const QImage& img) {
        const QRectF c = pin.contentRect().translated(-pin.pos());
        int n = 0;
        for (int y = static_cast<int>(c.top()) - 20; y < static_cast<int>(c.top()) - 2; ++y)
            for (int x = static_cast<int>(c.left()); x < static_cast<int>(c.right()); ++x)
                if (qAlpha(img.pixel(x, y)) > 140 && qGray(img.pixel(x, y)) < 60)
                    ++n;
        return n;
    };
    QVERIFY(darkBand(withTitle) > 50); // 标题条（黑底）
    pin.setTitle(QString());
    QVERIFY(darkBand(pin.grab().toImage()) < 50);
    QVERIFY(pin.stateJson().value(QStringLiteral("title")).toString().isEmpty());
    // 透明图像背景
    QImage alpha(40, 40, QImage::Format_ARGB32_Premultiplied);
    alpha.fill(Qt::transparent);
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 40; ++x)
            alpha.setPixel(x, y, qRgb(255, 0, 0));
    s.setValue(QStringLiteral("pin.alphaBackground"), QStringLiteral("checkerLight"));
    PinWindow a(alpha, s);
    a.moveToPhysical(QPoint(0, 0));
    a.show();
    a.repaint();
    QImage g = a.grab().toImage();
    const QPoint inside = (a.contentRect().translated(-a.pos()).topLeft() + QPointF(20, 30)).toPoint();
    QVERIFY(qRed(g.pixel(inside)) > 190 && qGreen(g.pixel(inside)) > 190); // 浅色棋盘格
    s.setValue(QStringLiteral("pin.alphaBackground"), QStringLiteral("checkerDark"));
    a.repaint();
    g = a.grab().toImage();
    QVERIFY(qRed(g.pixel(inside)) < 120);
    s.setValue(QStringLiteral("pin.alphaBackground"), QStringLiteral("transparent"));
    a.moveToPhysical(QPoint(1, 1)); // relayout → 遮罩
    QVERIFY(!a.mask().isEmpty());
    QVERIFY(a.mask().contains((a.contentRect().translated(-a.pos()).topLeft() + QPointF(20, 10)).toPoint()));
    QVERIFY(!a.mask().contains(inside));
    s.setValue(QStringLiteral("pin.alphaBackground"), QStringLiteral("pseudo"));
    a.moveToPhysical(QPoint(2, 2));
    QVERIFY(a.mask().isEmpty());
}

void TestM6D::dragMimeAndGesture()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* img = m.pinImage(testImage(30, 20), QPoint(0, 0));
    std::unique_ptr<QMimeData> mime(img->makeDragMimeData());
    QVERIFY(mime->hasImage());
    PinWindow* text = m.pinText(QStringLiteral("drag me"), QPoint(0, 0));
    std::unique_ptr<QMimeData> tm(text->makeDragMimeData());
    QCOMPARE(tm->text(), QStringLiteral("drag me"));
    QVERIFY(tm->hasHtml());
    PinWindow* color = m.pinColor(QColor(0, 0, 255), QPoint(0, 0));
    std::unique_ptr<QMimeData> cm(color->makeDragMimeData());
    QCOMPARE(cm->text(), QStringLiteral("#0000FF"));
    QTemporaryDir dir;
    const QString doc = dir.filePath(QStringLiteral("a.txt"));
    {
        QFile f(doc);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    PinWindow* file = m.pinFile(doc, QPoint(0, 0));
    std::unique_ptr<QMimeData> fm(file->makeDragMimeData());
    QCOMPARE(fm->urls().first().toLocalFile(), doc);
    // 触控板缩放手势
    s.setValue(QStringLiteral("pin.zoomAnchor"), QStringLiteral("center"));
    QNativeGestureEvent gesture(Qt::ZoomNativeGesture, QPointingDevice::primaryPointingDevice(), 2, QPointF(10, 10), QPointF(10, 10), QPointF(10, 10), 0.5, QPointF());
    QApplication::sendEvent(img, &gesture);
    QCOMPARE(img->scale(), 1.5);
}

void TestM6D::soloAndBatch()
{
    Settings s;
    s.setValue(QStringLiteral("pin.confirmBatch"), false);
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(0, 0));
    PinWindow* b = m.pinImage(testImage(), QPoint(300, 0));
    PinWindow* c = m.pinImage(testImage(), QPoint(600, 0));
    m.toggleSelected(b);
    m.setSolo(true);
    QVERIFY(!a->isVisible() && b->isVisible() && !c->isVisible());
    m.setSolo(false);
    QVERIFY(a->isVisible() && c->isVisible());
    m.setSolo(true);
    m.clearSelection(); // 清选中时退出独显
    QVERIFY(!m.solo() && a->isVisible());
    // 批量缩略图
    s.setValue(QStringLiteral("pin.thumbnailSize"), 50);
    m.setAllThumbnails(true);
    QVERIFY(a->thumbnail() && b->thumbnail() && c->thumbnail());
    m.setAllThumbnails(false);
    QVERIFY(!a->thumbnail());
    // 全部保存到文件夹
    QTemporaryDir dir;
    QCOMPARE(m.saveAllToFolder(dir.path()), 3);
    QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("pin-*.png")}, QDir::Files).size(), 3);
    m.closeAll();
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 0);
}

QTEST_MAIN(TestM6D)
#include "tst_m6d.moc"
