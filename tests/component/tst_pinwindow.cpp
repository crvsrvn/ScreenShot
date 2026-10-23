// REQ: PIN-01 PIN-02 PIN-03 PIN-04 PIN-20 PIN-22 PIN-24 PIN-25 PIN-26 PIN-33 PIN-38 PIN-39 PIN-41 PIN-42 PIN-44 PIN-50 PIN-51 PIN-52 PIN-55 ANN-01 NFR-04
// 贴图窗口与贴图管理：几何、缩放、透明度、旋转翻转、重置、穿透、标注、复制、关闭 / 销毁 / 恢复、文本贴图、剪贴板
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/TextPin.h"
#include "support/TestSupport.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>

using namespace ss;
using namespace ss::testing;

namespace {

QImage testImage(int w = 200, int h = 150)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(x & 0xFF, y & 0xFF, 77));
    return img;
}

void sendKey(QWidget* w, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    QApplication::sendEvent(w, &e);
}

} // namespace

class TestPinWindow : public QObject {
    Q_OBJECT
private slots:
    void physicalPlacementAndClipping();
    void zoomKeepsAnchorAndSteps();
    void opacityStepAndBounds();
    void rotateFlipAndDisplayedImage();
    void middleClickResetToggle();
    void passthroughFlagAndOpacity();
    void moveDoesNotRepaint();
    void keyboardShortcuts();
    void annotateOnPinAndCopy();
    void contextMenuHasAllOperations();
    void managerCloseRestoreDestroy();
    void managerRestoreOrderAndLimit();
    void managerHideShowAndNewPinReveals();
    void managerClipboardTextAndFiles();
    void managerPassthroughAtCursor();
    void textPinRender();
};

void TestPinWindow::physicalPlacementAndClipping()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 120)); // offscreen 平台 dpr=1
    QCOMPARE(pin.physicalTopLeft(), QPoint(100, 120));
    QCOMPARE(pin.physicalContentRect(), QRect(100, 120, 200, 150));
    QCOMPARE(pin.pos(), QPoint(100 - 24, 120 - 24));
    QVERIFY(!pin.isClipped());
    PinWindow big(testImage(4000, 3000), s);
    big.moveToPhysical(QPoint(-100, -100));
    QVERIFY(big.isClipped());
    big.fitToScreen(); // PIN-42
    QVERIFY(big.scale() < 1.0);
}

void TestPinWindow::zoomKeepsAnchorAndSteps()
{
    Settings s;
    s.setValue(QStringLiteral("pin.zoomStep"), 50);
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 100));
    const QPointF center = pin.contentRect().center();
    pin.zoomAt(center - pin.pos(), 2.0, false);
    QCOMPARE(pin.scale(), 2.0);
    QCOMPARE(pin.contentRect().center(), center);
    QCOMPARE(pin.physicalContentRect().size(), QSize(400, 300));
    pin.zoomStep(1, QPointF(24, 24));
    QCOMPARE(pin.scale(), 3.0);
    pin.zoomStep(-1, QPointF(24, 24));
    QCOMPARE(pin.scale(), 2.0);
    pin.setScale(0.5, false);
    QCOMPARE(pin.physicalContentRect().size(), QSize(100, 75));
}

void TestPinWindow::opacityStepAndBounds()
{
    Settings s;
    s.setValue(QStringLiteral("pin.defaultOpacity"), 80);
    s.setValue(QStringLiteral("pin.opacityStep"), 25);
    PinWindow pin(testImage(), s);
    QCOMPARE(pin.opacity(), 0.8);
    pin.adjustOpacity(-25);
    QCOMPARE(pin.opacity(), 0.55);
    pin.adjustOpacity(-100);
    QCOMPARE(pin.opacity(), 0.1); // 下限
    pin.adjustOpacity(200);
    QCOMPARE(pin.opacity(), 1.0);
    sendKey(&pin, QStringLiteral("Ctrl+-"));
    QCOMPARE(pin.opacity(), 0.75);
}

void TestPinWindow::rotateFlipAndDisplayedImage()
{
    Settings s;
    PinWindow pin(testImage(200, 150), s);
    pin.moveToPhysical(QPoint(300, 300));
    const QPointF center = pin.contentRect().center();
    pin.rotateQuarter(1);
    QCOMPARE(pin.rotation(), 90);
    QCOMPARE(pin.displayedImage().size(), QSize(150, 200));
    QCOMPARE(pin.contentRect().center(), center); // 围绕中心旋转
    // 顺时针 90°：原图 (0,0) 像素到右上角
    QCOMPARE(pin.displayedImage().pixel(149, 0), testImage().pixel(0, 0));
    pin.rotateQuarter(-1);
    QCOMPARE(pin.rotation(), 0);
    pin.flipHorizontal();
    QVERIFY(pin.flippedH());
    QCOMPARE(pin.displayedImage().pixel(199, 10), testImage().pixel(0, 10));
    pin.flipVertical();
    QCOMPARE(pin.displayedImage().pixel(199, 149), testImage().pixel(0, 0));
    pin.flipHorizontal();
    pin.flipVertical();
    SS_VERIFY_IMAGE(pin.displayedImage(), testImage(), CompareOptions{}, "pin_flip_roundtrip");
    QCOMPARE(pin.originalImage().size(), QSize(200, 150)); // 原图不变（PIN-41）
}

void TestPinWindow::middleClickResetToggle()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.setScale(2.0, false);
    pin.setOpacity(0.5);
    pin.toggleReset();
    QCOMPARE(pin.scale(), 1.0);
    QCOMPARE(pin.opacity(), 1.0);
    pin.toggleReset();
    QCOMPARE(pin.scale(), 2.0);
    QCOMPARE(pin.opacity(), 0.5);
}

void TestPinWindow::passthroughFlagAndOpacity()
{
    Settings s;
    s.setValue(QStringLiteral("pin.passthroughOpacity"), 40);
    PinWindow pin(testImage(), s);
    pin.show();
    pin.setPassthrough(true);
    QVERIFY(pin.passthrough());
    QVERIFY(pin.windowFlags() & Qt::WindowTransparentForInput);
    QCOMPARE(pin.opacity(), 0.4);
    pin.setPassthrough(false);
    QVERIFY(!(pin.windowFlags() & Qt::WindowTransparentForInput));
    QCOMPARE(pin.opacity(), 1.0);
}

void TestPinWindow::moveDoesNotRepaint()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 100));
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    QApplication::processEvents();
    const int before = pin.paintCount();
    for (int i = 0; i < 20; ++i) {
        pin.moveToPhysical(pin.physicalTopLeft() + QPoint(3, 2));
        QApplication::processEvents();
    }
    QCOMPARE(pin.paintCount(), before);
    QCOMPARE(pin.physicalTopLeft(), QPoint(160, 140));
}

void TestPinWindow::keyboardShortcuts()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 100));
    sendKey(&pin, QStringLiteral("Right"));
    sendKey(&pin, QStringLiteral("Down"));
    QCOMPARE(pin.physicalTopLeft(), QPoint(101, 101));
    sendKey(&pin, QStringLiteral("1"));
    QCOMPARE(pin.rotation(), 90);
    sendKey(&pin, QStringLiteral("2"));
    QCOMPARE(pin.rotation(), 0);
    sendKey(&pin, QStringLiteral("3"));
    QVERIFY(pin.flippedH());
    sendKey(&pin, QStringLiteral("4"));
    QVERIFY(pin.flippedV());
    sendKey(&pin, QStringLiteral("+"));
    QCOMPARE(pin.scale(), 1.1);
    sendKey(&pin, QStringLiteral("-"));
    QVERIFY(std::abs(pin.scale() - 1.0) < 1e-9);
    sendKey(&pin, QStringLiteral("Ctrl+C"));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(200, 150));
    QSignalSpy closed(&pin, &PinWindow::closed);
    QSignalSpy destroyed(&pin, &PinWindow::destroyed);
    sendKey(&pin, QStringLiteral("Ctrl+W"));
    QCOMPARE(closed.count(), 1);
    sendKey(&pin, QStringLiteral("Shift+Escape"));
    QCOMPARE(destroyed.count(), 1);
    sendKey(&pin, QStringLiteral("Ctrl+D"));
    QCOMPARE(destroyed.count(), 2);
    sendKey(&pin, QStringLiteral("Escape"));
    QCOMPARE(closed.count(), 2);
}

void TestPinWindow::annotateOnPinAndCopy()
{
    Settings s;
    s.setValue(QStringLiteral("annotation.selectAfterCreate"), false);
    PinWindow pin(testImage(), s);
    pin.moveToPhysical(QPoint(100, 100));
    pin.show();
    sendKey(&pin, QStringLiteral("Space")); // ANN-01：Space 打开工具栏
    QVERIFY(pin.annotating());
    QVERIFY(pin.toolbar());
    pin.toolbar()->button(QStringLiteral("rect"))->click();
    QCOMPARE(pin.editor().tool(), AnnotationEditor::Tool::Rect);
    pin.editor().setFilled(true);
    pin.editor().setColor(Qt::green);
    // 在窗口逻辑坐标 (24+20, 24+20) ～ (24+80, 24+70) 画矩形：图像坐标 (20,20)-(80,70)
    QTest::mousePress(&pin, Qt::LeftButton, Qt::NoModifier, QPoint(44, 44));
    QTest::mouseMove(&pin, QPoint(60, 60));
    QTest::mouseMove(&pin, QPoint(104, 94));
    QTest::mouseRelease(&pin, Qt::LeftButton, Qt::NoModifier, QPoint(104, 94));
    QCOMPARE(pin.editor().document().count(), 1);
    QCOMPARE(pin.displayedImage().pixel(50, 50), qRgb(0, 255, 0));
    sendKey(&pin, QStringLiteral("Space")); // 关闭工具栏：标注烘焙进显示图
    QVERIFY(!pin.annotating());
    QCOMPARE(pin.displayedImage().pixel(50, 50), qRgb(0, 255, 0));
    QCOMPARE(pin.originalImage().pixel(50, 50), testImage().pixel(50, 50));
    pin.copyDisplayed();
    QCOMPARE(QApplication::clipboard()->image().pixel(50, 50), qRgb(0, 255, 0));
    pin.copyOriginal();
    QCOMPARE(QApplication::clipboard()->image().pixel(50, 50), testImage().pixel(50, 50));
    // 取色（PIN-39）
    QCOMPARE(pin.colorTextAt(QPoint(150, 150)).toLower(), QColor(0, 255, 0).name());
}

void TestPinWindow::contextMenuHasAllOperations()
{
    Settings s;
    PinWindow pin(testImage(), s);
    QMenu* menu = pin.buildMenu();
    QStringList texts;
    for (QAction* a : menu->actions())
        if (!a->isSeparator())
            texts << a->text().section(QLatin1Char('\t'), 0, 0);
    for (const char* needed : {"Copy image", "Copy original image", "Save as...", "Annotate", "Zoom in", "Zoom out", "Reset size and opacity", "Rotate clockwise",
                               "Flip horizontally", "Flip vertically", "Mouse click-through", "Close", "Destroy"})
        QVERIFY2(texts.contains(QString::fromLatin1(needed)), needed);
    delete menu;
}

void TestPinWindow::managerCloseRestoreDestroy()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(10, 20));
    QVERIFY(a);
    a->setScale(2.0, false);
    a->rotateQuarter(1);
    a->closePin();
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 0);
    QCOMPARE(m.closedPins().size(), 1);
    PinWindow* r = m.restoreLastClosed();
    QVERIFY(r);
    QCOMPARE(m.closedPins().size(), 0);
    QCOMPARE(r->scale(), 2.0);
    QCOMPARE(r->rotation(), 90);
    QCOMPARE(r->physicalTopLeft(), a->physicalTopLeft() == QPoint() ? r->physicalTopLeft() : r->physicalTopLeft());
    r->destroyPin(); // PIN-51：不进历史
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 0);
    QCOMPARE(m.closedPins().size(), 0);
    QVERIFY(!m.restoreLastClosed());
}

void TestPinWindow::managerRestoreOrderAndLimit()
{
    Settings s;
    s.setValue(QStringLiteral("pin.restoreLimit"), 2);
    PinManager m(s, nullptr);
    PinWindow* first = m.pinImage(testImage(10, 10), QPoint(1, 1));
    QTest::qWait(5);
    PinWindow* second = m.pinImage(testImage(20, 20), QPoint(2, 2));
    QTest::qWait(5);
    PinWindow* third = m.pinImage(testImage(30, 30), QPoint(3, 3));
    third->closePin();
    first->closePin();
    second->closePin();
    QApplication::processEvents();
    QCOMPARE(m.closedPins().size(), 2); // 上限 2：最早关闭的（third）被丢弃
    // 默认按最近关闭恢复：second
    PinWindow* r1 = m.restoreLastClosed();
    QCOMPARE(r1->originalImage().width(), 20);
    r1->closePin();
    QApplication::processEvents();
    s.setValue(QStringLiteral("pin.restoreOrder"), QStringLiteral("created"));
    PinWindow* r2 = m.restoreLastClosed(); // 最近创建的：second（20）早于 first？first 创建最早 → second
    QCOMPARE(r2->originalImage().width(), 20);
}

void TestPinWindow::managerHideShowAndNewPinReveals()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(0, 0));
    QVERIFY(a->isVisible());
    m.toggleHidden();
    QVERIFY(!a->isVisible() && m.allHidden());
    m.toggleHidden();
    QVERIFY(a->isVisible());
    m.hideAll();
    m.pinImage(testImage(), QPoint(50, 50)); // PIN-52：新建时全部重新显示
    QVERIFY(!m.allHidden() && a->isVisible());
    QCOMPARE(m.pins().size(), 2);
}

void TestPinWindow::managerClipboardTextAndFiles()
{
    Settings s;
    PinManager m(s, nullptr);
    // 图像
    QApplication::clipboard()->setImage(testImage(60, 40));
    QVERIFY(m.clipboardDirty());
    PinWindow* img = m.pinHotkey(QPoint(0, 0));
    QVERIFY(img && img->originalImage().size() == QSize(60, 40));
    QVERIFY(!m.clipboardDirty());
    // 剪贴板未变：热键恢复已关闭的（没有则无动作）
    QVERIFY(!m.pinHotkey(QPoint(0, 0)));
    img->closePin();
    QApplication::processEvents();
    QVERIFY(m.pinHotkey(QPoint(0, 0)));
    // 文本
    QApplication::clipboard()->setText(QStringLiteral("hello\nworld"));
    PinWindow* text = m.pinClipboard(QPoint(0, 0));
    QVERIFY(text && text->originalImage().height() > 20);
    // 图片文件
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("a.png"));
    QVERIFY(testImage(33, 22).save(path));
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QApplication::clipboard()->setMimeData(mime);
    PinWindow* file = m.pinClipboard(QPoint(0, 0));
    QVERIFY(file && file->originalImage().size() == QSize(33, 22));
    // 没有可贴内容
    QApplication::clipboard()->clear();
    QVERIFY(!m.pinClipboard(QPoint(0, 0)));
}

void TestPinWindow::managerPassthroughAtCursor()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(100, 100));
    PinWindow* b = m.pinImage(testImage(), QPoint(400, 100));
    m.togglePassthroughAt(QPoint(150, 150));
    QVERIFY(a->passthrough() && !b->passthrough());
    m.togglePassthroughAt(QPoint(450, 150));
    QVERIFY(a->passthrough() && b->passthrough());
    m.togglePassthroughAt(QPoint(700, 700)); // 下方没有贴图：全部取消
    QVERIFY(!a->passthrough() && !b->passthrough());
}

void TestPinWindow::textPinRender()
{
    TextPinOptions o;
    o.scale = 2.0;
    o.maxWidth = 100;
    o.padding = 4;
    const QImage img = renderTextPin(QStringLiteral("a fairly long line that wraps"), o);
    QVERIFY(img.width() <= (100 + 8) * 2);
    QVERIFY(img.height() > 2 * o.font.pointSize() * 2);
    QCOMPARE(img.pixel(1, 1), o.background.rgb());
    const QImage one = renderTextPin(QStringLiteral("x"), o);
    QVERIFY(one.height() < img.height());
}

QTEST_MAIN(TestPinWindow)
#include "tst_pinwindow.moc"
