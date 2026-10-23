// REQ: PIN-05 PIN-06 PIN-07 PIN-11 PIN-21 PIN-27 PIN-29 PIN-30 PIN-31 PIN-32 PIN-36 PIN-53 PIN-60 GRP-01
// M5 贴图 P1：HTML / 文本选择 / 颜色贴图、替换、吸附、灰度反色、缩略图、显示范围、裁剪、置顶、阴影状态色、关闭确认、多选、分组
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/ColorPin.h"
#include "core/pin/TextPin.h"
#include "support/TestSupport.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <QScreen>
#include <QTemporaryDir>
#include <QTest>

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

int confirmCalls = 0;
bool confirmAnswer = true;

} // namespace

class TestPinP1 : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        PinWindow::confirmHandler = [](QWidget*, const QString&) {
            ++confirmCalls;
            return confirmAnswer;
        };
    }
    void htmlPinAndIgnoreFormatting();
    void textSelectionAndCopy();
    void colorPinParseRenderCopy();
    void replacePolicies();
    void snapToPinsAndScreen();
    void grayscaleInvert();
    void thumbnailToggle();
    void viewRectAndPan();
    void cropRestoreConfirm();
    void topmostToggle();
    void shadowColorsByState();
    void confirmCloseAndBatch();
    void multiSelectOperations();
    void groups();
};

void TestPinP1::htmlPinAndIgnoreFormatting()
{
    Settings s;
    PinManager m(s, nullptr);
    auto makeMime = [] {
        auto* mime = new QMimeData;
        mime->setText(QStringLiteral("Hello World"));
        mime->setHtml(QStringLiteral("<p><span style=\"color:#ff0000;font-size:30px\">Hello</span> World <img src=\"http://example.invalid/x.png\"></p>"));
        return mime;
    };
    QApplication::clipboard()->setMimeData(makeMime());
    PinWindow* pin = m.pinClipboard(QPoint(0, 0));
    QVERIFY(pin);
    QCOMPARE(pin->contentKind(), PinWindow::ContentKind::Text);
    QVERIFY(!pin->ignoringFormatting());
    QCOMPARE(pin->textPlain().trimmed(), QStringLiteral("Hello World"));
    QVERIFY(pin->textHtml().contains(QStringLiteral("#ff0000")));
    const QImage rich = pin->originalImage();
    // 有红色像素（保留颜色）
    bool red = false;
    for (int y = 0; y < rich.height() && !red; ++y)
        for (int x = 0; x < rich.width() && !red; ++x)
            if (qRed(rich.pixel(x, y)) > 200 && qGreen(rich.pixel(x, y)) < 80)
                red = true;
    QVERIFY(red);
    // 忽略格式：按纯文本重新排版，尺寸变小、没有红色
    pin->setIgnoreFormatting(true);
    QVERIFY(pin->ignoringFormatting());
    const QImage plain = pin->originalImage();
    QVERIFY(plain.height() < rich.height());
    red = false;
    for (int y = 0; y < plain.height() && !red; ++y)
        for (int x = 0; x < plain.width() && !red; ++x)
            if (qRed(plain.pixel(x, y)) > 200 && qGreen(plain.pixel(x, y)) < 80)
                red = true;
    QVERIFY(!red);
    pin->setIgnoreFormatting(false);
    QCOMPARE(pin->originalImage().size(), rich.size());
    // 设置关闭富文本：直接按纯文本贴
    s.setValue(QStringLiteral("pin.textHtml"), false);
    QApplication::clipboard()->setMimeData(makeMime());
    PinWindow* plainPin = m.pinClipboard(QPoint(0, 0));
    QVERIFY(plainPin && plainPin->ignoringFormatting());
}

void TestPinP1::textSelectionAndCopy()
{
    TextPinOptions o;
    o.font = QFont(QStringLiteral("Arial"), 12);
    o.scale = 2.0;
    TextPinDocument doc(QStringLiteral("abc def\nsecond line"), o);
    QCOMPARE(doc.length(), 19);
    QCOMPARE(doc.selectedText(0, 3), QStringLiteral("abc"));
    QCOMPARE(doc.selectedText(4, 10), QStringLiteral("def\nse"));
    QVERIFY(doc.selectedHtml(0, 3).contains(QStringLiteral("abc")));
    const QList<QRectF> rects = doc.selectionRects(0, 3);
    QCOMPARE(rects.size(), 1);
    QVERIFY(rects[0].left() >= o.padding * o.scale && rects[0].width() > 10);
    QCOMPARE(doc.selectionRects(4, 10).size(), 2); // 跨行
    // 命中测试：第一行开头 → 0；末尾以后 → 行末
    QCOMPARE(doc.hitTest(rects[0].topLeft() + QPointF(1, 1)), 0);
    QVERIFY(doc.hitTest(QPointF(10000, rects[0].center().y())) >= 7);
    // 贴图：选择模式下 Ctrl+C 复制选中部分（纯文本 + HTML）
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* pin = m.pinText(QStringLiteral("abc def\nsecond line"), QPoint(0, 0));
    QVERIFY(pin);
    pin->copyText();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("abc def\nsecond line"));
    QVERIFY(QApplication::clipboard()->mimeData()->hasHtml());
    pin->setTextSelectionMode(true);
    pin->selectTextRange(4, 7);
    QCOMPARE(pin->selectedText(), QStringLiteral("def"));
    QCOMPARE(pin->textSelectionRectsImage().size(), 1);
    sendKey(pin, QStringLiteral("Ctrl+C"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("def"));
    sendKey(pin, QStringLiteral("Escape")); // 退出选择模式而不是关闭
    QVERIFY(!pin->textSelectionMode());
    QCOMPARE(m.pins().size(), 1);
}

void TestPinP1::colorPinParseRenderCopy()
{
    QCOMPARE(parseColorText(QStringLiteral("#ff8000")), QColor(255, 128, 0));
    QCOMPARE(parseColorText(QStringLiteral(" #F80 ")), QColor(255, 136, 0));
    QCOMPARE(parseColorText(QStringLiteral("rgb(255, 128, 0)")), QColor(255, 128, 0));
    QCOMPARE(parseColorText(QStringLiteral("rgba(255,128,0,0.5)"))->alpha(), 128);
    QCOMPARE(parseColorText(QStringLiteral("255 128 0")), QColor(255, 128, 0));
    QCOMPARE(parseColorText(QStringLiteral("1.0, 0.5, 0.0")), QColor::fromRgbF(1.0, 0.5, 0.0));
    QCOMPARE(parseColorText(QStringLiteral("orange")), QColor(QStringLiteral("orange")));
    QVERIFY(!parseColorText(QStringLiteral("hello")));
    QVERIFY(!parseColorText(QStringLiteral("300 1 1")));
    QVERIFY(!parseColorText(QStringLiteral("some text\n#ff0000")));
    const auto lines = colorFormatLines(QColor(255, 128, 0));
    QCOMPARE(lines[0].text, QStringLiteral("#FF8000"));
    QCOMPARE(lines[1].text, QStringLiteral("rgb(255, 128, 0)"));
    QVERIFY(lines[2].text.startsWith(QStringLiteral("hsv(30")));
    QVERIFY(lines[3].text.startsWith(QStringLiteral("hsl(30")));
    ColorPinOptions o;
    const QImage card = renderColorPin(QColor(255, 128, 0), o);
    QCOMPARE(card.pixel(10, 10), qRgb(255, 128, 0));
    QVERIFY(card.height() > o.swatchHeight + 4 * 10);
    // 剪贴板里的颜色文本 → 色卡贴图；复制为其他格式；编辑色值
    Settings s;
    PinManager m(s, nullptr);
    QApplication::clipboard()->setText(QStringLiteral("rgb(0, 0, 255)"));
    PinWindow* pin = m.pinClipboard(QPoint(0, 0));
    QVERIFY(pin);
    QCOMPARE(pin->contentKind(), PinWindow::ContentKind::Color);
    QCOMPARE(pin->color(), QColor(0, 0, 255));
    QCOMPARE(pin->originalImage().pixel(5, 5), qRgb(0, 0, 255));
    pin->copyColorAs(QStringLiteral("hex"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#0000FF"));
    pin->copyColorAs(QStringLiteral("hsl"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hsl(240, 100%, 50%)"));
    pin->setColorContent(QColor(10, 20, 30), 1.0);
    QCOMPARE(pin->originalImage().pixel(5, 5), qRgb(10, 20, 30));
    // 色卡状态可恢复
    const QJsonObject state = pin->stateJson();
    QCOMPARE(state.value(QStringLiteral("color")).toString(), QStringLiteral("#ff0a141e"));
}

void TestPinP1::replacePolicies()
{
    Settings s;
    s.setValue(QStringLiteral("pin.confirmBatch"), false);
    PinManager m(s, nullptr);
    PinWindow* pin = m.pinImage(testImage(), QPoint(50, 60));
    QVERIFY(pin);
    QMimeData mime;
    mime.setImageData(testImage(30, 20));
    // close：原贴图进历史，新贴图在原位置
    s.setValue(QStringLiteral("pin.replacePolicy"), QStringLiteral("close"));
    PinWindow* fresh = m.replacePin(pin, &mime);
    QApplication::processEvents();
    QVERIFY(fresh && fresh != pin);
    QCOMPARE(fresh->physicalTopLeft(), QPoint(50, 60));
    QCOMPARE(fresh->originalImage().size(), QSize(30, 20));
    QCOMPARE(m.pins().size(), 1);
    QCOMPARE(m.closedPins().size(), 1);
    // keep：原贴图保留，新贴图错开
    s.setValue(QStringLiteral("pin.replacePolicy"), QStringLiteral("keep"));
    PinWindow* kept = m.replacePin(fresh, &mime);
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 2);
    QCOMPARE(kept->physicalTopLeft(), QPoint(80, 90));
    // destroy：原贴图销毁，不进历史
    s.setValue(QStringLiteral("pin.replacePolicy"), QStringLiteral("destroy"));
    m.replacePin(kept, &mime);
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 2);
    QCOMPARE(m.closedPins().size(), 1);
    // Ctrl+V 走同一路径（剪贴板文本 → 文本贴图）
    QApplication::clipboard()->setText(QStringLiteral("replacement"));
    sendKey(m.pins().first(), QStringLiteral("Ctrl+V"));
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 2);
    QCOMPARE(m.pins().last()->contentKind(), PinWindow::ContentKind::Text);
}

void TestPinP1::snapToPinsAndScreen()
{
    Settings s;
    s.setValue(QStringLiteral("pin.snapDistance"), 10);
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(100, 100), QPoint(300, 300)); // 内容 300..400
    PinWindow* b = m.pinImage(testImage(50, 50), QPoint(600, 600));
    QVERIFY(a && b);
    // b 的右边靠近 a 的左边（距离 6）→ 吸附到 300-50=250；垂直方向不在范围内保持
    QCOMPARE(b->snappedTopLeft(QPointF(244, 500)), QPointF(250, 500));
    // 左边靠近 a 的右边
    QCOMPARE(b->snappedTopLeft(QPointF(407, 500)), QPointF(400, 500));
    // 上边靠近 a 的下边，并且左边对齐 a 的左边
    QCOMPARE(b->snappedTopLeft(QPointF(296, 395)), QPointF(300, 400));
    // 超出吸附距离：不动
    QCOMPARE(b->snappedTopLeft(QPointF(230, 500)), QPointF(230, 500));
    // 屏幕边缘（offscreen 屏幕从 0,0 开始）
    QCOMPARE(b->snappedTopLeft(QPointF(4, 7)), QPointF(0, 0));
    // 不可见的贴图不参与
    a->hide();
    QCOMPARE(b->snappedTopLeft(QPointF(244, 500)), QPointF(244, 500));
}

void TestPinP1::grayscaleInvert()
{
    Settings s;
    PinWindow pin(testImage(), s);
    const QRgb original = pin.displayedImage().pixel(100, 50); // (100, 50, 77)
    QCOMPARE(original, qRgb(100, 50, 77));
    sendKey(&pin, QStringLiteral("5"));
    QVERIFY(pin.grayscale());
    const QRgb gray = pin.displayedImage().pixel(100, 50);
    QCOMPARE(qRed(gray), qGreen(gray));
    QCOMPARE(qGreen(gray), qBlue(gray));
    sendKey(&pin, QStringLiteral("5"));
    QVERIFY(!pin.grayscale());
    QCOMPARE(pin.displayedImage().pixel(100, 50), original);
    sendKey(&pin, QStringLiteral("6"));
    QVERIFY(pin.inverted());
    QCOMPARE(pin.displayedImage().pixel(100, 50), qRgb(155, 205, 178));
    // 两者叠加，且状态可序列化
    pin.setGrayscale(true);
    const QJsonObject state = pin.stateJson();
    QVERIFY(state.value(QStringLiteral("gray")).toBool() && state.value(QStringLiteral("invert")).toBool());
    PinWindow restored(testImage(), s);
    restored.applyStateJson(state);
    QCOMPARE(restored.displayedImage().pixel(100, 50), pin.displayedImage().pixel(100, 50));
    QVERIFY(pin.originalImage().pixel(100, 50) == original); // 原图不变
}

void TestPinP1::thumbnailToggle()
{
    Settings s;
    s.setValue(QStringLiteral("pin.thumbnailSize"), 100);
    PinWindow pin(testImage(400, 200), s);
    pin.moveToPhysical(QPoint(0, 0));
    pin.setScale(1.5, false);
    pin.toggleThumbnail();
    QVERIFY(pin.thumbnail());
    QCOMPARE(pin.physicalContentRect().size(), QSize(100, 50));
    pin.toggleThumbnail();
    QVERIFY(!pin.thumbnail());
    QCOMPARE(pin.scale(), 1.5);
    // Shift+双击切换
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(50, 50), QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(&pin, &dbl);
    QVERIFY(pin.thumbnail());
}

void TestPinP1::viewRectAndPan()
{
    Settings s;
    PinWindow pin(testImage(200, 150), s);
    pin.moveToPhysical(QPoint(0, 0));
    pin.setViewRect(QRectF(50, 40, 60, 30));
    QCOMPARE(pin.viewRect(), QRectF(50, 40, 60, 30));
    QCOMPARE(pin.physicalContentRect().size(), QSize(60, 30));
    QCOMPARE(pin.displayedImage().pixel(0, 0), qRgb(50, 40, 77));
    // 再次以当前显示坐标缩小范围：相对当前范围
    pin.setViewRect(QRectF(10, 5, 20, 10));
    QCOMPARE(pin.viewRect(), QRectF(60, 45, 20, 10));
    // 平移，限制在完整图内
    pin.panView(QPointF(5, -3));
    QCOMPARE(pin.viewRect(), QRectF(65, 42, 20, 10));
    QCOMPARE(pin.displayedImage().pixel(0, 0), qRgb(65, 42, 77));
    pin.panView(QPointF(1000, 1000));
    QCOMPARE(pin.viewRect(), QRectF(180, 140, 20, 10));
    // 状态可序列化；清除后恢复完整
    QVERIFY(pin.stateJson().contains(QStringLiteral("view")));
    pin.clearViewRect();
    QCOMPARE(pin.physicalContentRect().size(), QSize(200, 150));
    // 旋转后范围仍限制在变换后的图内
    pin.setViewRect(QRectF(0, 0, 100, 100));
    pin.rotateQuarter(1);
    QVERIFY(QRectF(0, 0, 150, 200).contains(pin.viewRect()));
}

void TestPinP1::cropRestoreConfirm()
{
    Settings s;
    PinWindow pin(testImage(200, 150), s);
    pin.moveToPhysical(QPoint(0, 0));
    pin.cropTo(QRectF(20, 10, 100, 50));
    QCOMPARE(pin.cropRect(), QRect(20, 10, 100, 50));
    QCOMPARE(pin.physicalContentRect(), QRect(0, 0, 100, 50));
    QCOMPARE(pin.displayedImage().pixel(0, 0), qRgb(20, 10, 77));
    QCOMPARE(pin.originalImage().size(), QSize(200, 150)); // 可恢复：原图未动
    pin.restoreCrop();
    QCOMPARE(pin.physicalContentRect().size(), QSize(200, 150));
    // 旋转 90° 后裁剪：显示坐标换算回原图
    pin.rotateQuarter(1); // 顺时针：显示图 150x200；显示 (x,y) ← 原图 (y, 199-x)…
    pin.cropTo(QRectF(0, 0, 150, 50)); // 显示图顶部 50 行 = 原图最左 50 列
    QCOMPARE(pin.cropRect(), QRect(0, 0, 50, 150));
    QCOMPARE(pin.physicalContentRect().size(), QSize(150, 50));
    // 确认：彻底裁掉
    pin.confirmCrop();
    QVERIFY(pin.cropRect().isNull());
    QCOMPARE(pin.originalImage().size(), QSize(50, 150));
    QCOMPARE(pin.physicalContentRect().size(), QSize(150, 50));
    // 状态往返
    PinWindow other(testImage(200, 150), s);
    other.cropTo(QRectF(10, 10, 30, 30));
    PinWindow restored(testImage(200, 150), s);
    restored.applyStateJson(other.stateJson());
    QCOMPARE(restored.cropRect(), QRect(10, 10, 30, 30));
}

void TestPinP1::topmostToggle()
{
    Settings s;
    PinWindow pin(testImage(), s);
    pin.show();
    QVERIFY(pin.topmost());
    QVERIFY(pin.windowFlags() & Qt::WindowStaysOnTopHint);
    sendKey(&pin, QStringLiteral("T"));
    QVERIFY(!pin.topmost());
    QVERIFY(!(pin.windowFlags() & Qt::WindowStaysOnTopHint));
    QCOMPARE(pin.windowType(), Qt::Window); // 普通窗口：任务栏可切回
    QVERIFY(pin.isVisible());
    QVERIFY(!pin.stateJson().value(QStringLiteral("topmost")).toBool());
    pin.setTopmost(true);
    QCOMPARE(pin.windowType(), Qt::Tool);
    // 标注中 T 是文字工具，不切换置顶
    pin.toggleToolbar();
    sendKey(&pin, QStringLiteral("T"));
    QVERIFY(pin.topmost());
    QCOMPARE(pin.editor().tool(), AnnotationEditor::Tool::Text);
}

void TestPinP1::shadowColorsByState()
{
    Settings s;
    PinWindow pin(testImage(), s);
    const QColor inactive = pin.shadowColor();
    QVERIFY(inactive.alpha() > 0);
    pin.setPassthrough(true);
    const QColor through = pin.shadowColor();
    QVERIFY(through != inactive);
    pin.setPassthrough(false);
    pin.setSelected(true);
    const QColor selected = pin.shadowColor();
    QVERIFY(selected != inactive && selected != through);
    pin.setSelected(false);
    pin.setGroupIndex(2);
    const QColor group = pin.shadowColor();
    QVERIFY(group != inactive && group != selected);
    pin.setGroupIndex(3);
    QVERIFY(pin.shadowColor() != group);
    s.setValue(QStringLiteral("pin.shadow"), false);
    QCOMPARE(pin.shadowColor().alpha(), 0);
    // 关掉阴影后绘制不崩溃
    pin.show();
    pin.repaint();
}

void TestPinP1::confirmCloseAndBatch()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(0, 0));
    PinWindow* b = m.pinImage(testImage(), QPoint(300, 0));
    QVERIFY(a && b);
    // 单个关闭默认不确认
    confirmCalls = 0;
    confirmAnswer = false;
    sendKey(a, QStringLiteral("Escape"));
    QApplication::processEvents();
    QCOMPARE(confirmCalls, 0);
    QCOMPARE(m.pins().size(), 1);
    // 开启后 Esc 要确认；拒绝则不关
    s.setValue(QStringLiteral("pin.confirmClose"), true);
    sendKey(b, QStringLiteral("Escape"));
    QCOMPARE(confirmCalls, 1);
    QCOMPARE(m.pins().size(), 1);
    confirmAnswer = true;
    sendKey(b, QStringLiteral("Shift+Escape"));
    QApplication::processEvents();
    QCOMPARE(confirmCalls, 2);
    QCOMPARE(m.pins().size(), 0);
    // 批量：多个时确认一次；拒绝则一个都不关
    s.setValue(QStringLiteral("pin.confirmClose"), false);
    m.pinImage(testImage(), QPoint(0, 0));
    m.pinImage(testImage(), QPoint(300, 0));
    confirmCalls = 0;
    confirmAnswer = false;
    m.closeAll();
    QCOMPARE(confirmCalls, 1);
    QCOMPARE(m.pins().size(), 2);
    confirmAnswer = true;
    m.closeAll();
    QApplication::processEvents();
    QCOMPARE(confirmCalls, 2);
    QCOMPARE(m.pins().size(), 0);
    // 只有一个时不确认
    m.pinImage(testImage(), QPoint(0, 0));
    m.closeAll();
    QCOMPARE(confirmCalls, 2);
}

void TestPinP1::multiSelectOperations()
{
    Settings s;
    s.setValue(QStringLiteral("pin.confirmBatch"), false);
    PinManager m(s, nullptr);
    PinWindow* a = m.pinImage(testImage(), QPoint(0, 0));
    PinWindow* b = m.pinImage(testImage(), QPoint(300, 0));
    PinWindow* c = m.pinImage(testImage(), QPoint(600, 0));
    QVERIFY(a && b && c);
    m.toggleSelected(a);
    m.toggleSelected(b);
    QCOMPARE(m.selectedPins().size(), 2);
    QVERIFY(a->selected() && b->selected() && !c->selected());
    // 一起移动：拖动 a 时 b 同步
    const QPointF bBefore = b->contentTopLeft();
    emit a->groupMoved(a, QPointF(10, 20));
    QCOMPARE(b->contentTopLeft(), bBefore + QPointF(10, 20));
    QCOMPARE(c->contentTopLeft(), QPointF(600, 0));
    // 一起调透明度
    sendKey(a, QStringLiteral("Ctrl+-"));
    QCOMPARE(b->opacity(), 0.9);
    QCOMPARE(c->opacity(), 1.0);
    // 一起重置
    a->setScale(2.0, false);
    b->setScale(2.0, false);
    QMouseEvent mid(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(a, &mid);
    QCOMPARE(a->scale(), 1.0);
    QCOMPARE(b->scale(), 1.0);
    // Ctrl+A 全选可见；Esc 一起关闭
    c->hide();
    m.clearSelection();
    sendKey(a, QStringLiteral("Ctrl+A"));
    QCOMPARE(m.selectedPins().size(), 2);
    sendKey(a, QStringLiteral("Escape"));
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 1);
    QCOMPARE(m.pins().first(), c);
    QCOMPARE(m.closedPins().size(), 2);
    // Ctrl+单击切换选中
    c->show();
    QMouseEvent ctrlClick(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
    QApplication::sendEvent(c, &ctrlClick);
    QVERIFY(c->selected());
    QApplication::sendEvent(c, &ctrlClick);
    QVERIFY(!c->selected());
}

void TestPinP1::groups()
{
    Settings s;
    PinManager m(s, nullptr);
    QCOMPARE(m.groupNames().size(), 1);
    PinWindow* a = m.pinImage(testImage(), QPoint(0, 0));
    QVERIFY(a);
    a->closePin();
    QApplication::processEvents();
    PinWindow* b = m.pinImage(testImage(), QPoint(0, 0));
    QVERIFY(b && b->isVisible());
    QCOMPARE(m.closedPins().size(), 1);
    // 新建组并切换：原组贴图隐藏，新组为空、历史独立
    const int work = m.newGroup(QStringLiteral("Work"));
    QCOMPARE(work, 1);
    QCOMPARE(m.currentGroupName(), QStringLiteral("Work"));
    QVERIFY(!b->isVisible());
    QCOMPARE(m.pins().size(), 0);
    QCOMPARE(m.closedPins().size(), 0);
    QVERIFY(!m.restoreLastClosed());
    PinWindow* w = m.pinImage(testImage(), QPoint(0, 0));
    QVERIFY(w && w->isVisible());
    QCOMPARE(w->groupIndex(), 1);
    // 按名称切换、回到上一个、轮换
    QVERIFY(m.switchToGroupNamed(QStringLiteral("default")));
    QVERIFY(b->isVisible() && !w->isVisible());
    QCOMPARE(m.pins().size(), 1);
    m.previousGroup();
    QCOMPARE(m.currentGroupIndex(), 1);
    m.nextGroup();
    QCOMPARE(m.currentGroupIndex(), 0);
    m.nextGroup();
    QCOMPARE(m.currentGroupIndex(), 1);
    // 关闭 Work 组的贴图进入 Work 组历史；清空组销毁贴图并清空历史
    w->closePin();
    QApplication::processEvents();
    QCOMPARE(m.closedPins().size(), 1);
    m.pinImage(testImage(), QPoint(0, 0));
    m.clearCurrentGroup();
    QCOMPARE(m.pins().size(), 0);
    QCOMPARE(m.closedPins().size(), 0);
    m.switchToGroup(0);
    QCOMPARE(m.pins().size(), 1);
    QCOMPARE(m.closedPins().size(), 1);
}

QTEST_MAIN(TestPinP1)
#include "tst_pin_p1.moc"
