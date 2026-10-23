// REQ: ANN-11 ANN-21 ANN-25 ANN-33 ANN-35 ANN-37
// M5 标注 P1：右键贴合元素、虚线 / 点线与圆角矩形、箭头样式、文字描边 / 背景 / 投影、序号、放大标注
#include "app/AnnotationEditor.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "core/annotation/Extras.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakeElementDetector.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>

using namespace ss;
using namespace ss::testing;
using Tool = AnnotationEditor::Tool;

namespace {

struct Fixture {
    FakePlatformHandles h;
    Platform p;
    Settings settings;
    OutputActions output{settings};
    std::unique_ptr<CaptureSession> session;
    QRect sel{100, 100, 400, 300};

    Fixture()
        : p(makeFakePlatform(&h))
    {
        session = std::make_unique<CaptureSession>(p.screenCapture.get(), p.windowDetector.get(), p.elementDetector.get(), settings);
        session->setOutput(&output);
        session->prepareOverlays();
        settings.setValue(QStringLiteral("annotation.selectAfterCreate"), false);
    }
    AnnotationEditor& editor() { return session->editor(); }
    void startWith(Tool tool)
    {
        QVERIFY(session->start(1, false));
        session->setSelection(sel);
        session->setTool(tool);
        QCOMPARE(session->state(), CaptureSession::State::Annotating);
    }
    void drag(QPoint from, QPoint to)
    {
        session->pressAt(from, Qt::LeftButton);
        session->dragTo(from + QPoint(4, 4));
        session->dragTo(to);
        session->releaseAt(to, Qt::LeftButton);
    }
    void click(QPoint at)
    {
        session->pressAt(at, Qt::LeftButton);
        session->releaseAt(at, Qt::LeftButton);
    }
    QImage render() { return session->currentImage(); }
    // 文字输入框挂在选区所在的覆盖窗口上，覆盖窗口的顺序不固定：逐个查找
    QPlainTextEdit* textEdit()
    {
        for (auto* w : session->overlays())
            if (auto* e = w->findChild<QPlainTextEdit*>())
                return e;
        return nullptr;
    }
};

// 两张图是否有任何像素不同
bool differs(const QImage& a, const QImage& b)
{
    return a != b;
}

} // namespace

class TestAnnotationP1 : public QObject {
    Q_OBJECT
private slots:
    void rightClickSnapsToElement();
    void penStylesAndRoundedRect();
    void radiusHandleDrag();
    void arrowHeadStyles();
    void textDecor();
    void stepNumbersAutoIncrementStylesPointerNoteRenumber();
    void magnifierAnnotation();
    void toolbarRowsAndJsonRoundTrip();
};

void TestAnnotationP1::rightClickSnapsToElement()
{
    Fixture f;
    f.h.elementDetector->setElements({QRect(150, 150, 100, 60), QRect(120, 120, 300, 200)});
    f.startWith(Tool::Rect);
    f.editor().setFilled(true);
    f.editor().setColor(Qt::red);
    f.session->pressAt(QPoint(180, 170), Qt::RightButton); // 最内层元素
    QCOMPARE(f.session->state(), CaptureSession::State::Annotating);
    QCOMPARE(f.editor().document().count(), 1);
    QCOMPARE(f.editor().document().at(0)->bounds(), QRectF(50, 50, 100, 60));
    QCOMPARE(f.render().pixel(60, 60), qRgb(255, 0, 0));
    // 马赛克 / 荧光笔也贴合；元素超出选区时裁剪到选区
    f.h.elementDetector->setElements({QRect(0, 0, 200, 200)});
    f.session->setTool(Tool::Mosaic);
    f.session->pressAt(QPoint(110, 110), Qt::RightButton);
    QCOMPARE(f.editor().document().count(), 2);
    QCOMPARE(f.editor().document().at(1)->bounds(), QRectF(0, 0, 100, 100));
    f.session->setTool(Tool::Highlighter);
    f.session->pressAt(QPoint(110, 110), Qt::RightButton);
    QCOMPARE(f.editor().document().count(), 3);
    QCOMPARE(f.editor().document().at(2)->type(), QStringLiteral("highlighter"));
    // 没有元素也没有窗口：右键仍是“结束标注”
    f.h.elementDetector->setElements({});
    f.h.windowDetector->setWindows({});
    f.session->setTool(Tool::Ellipse);
    f.session->pressAt(QPoint(300, 300), Qt::RightButton);
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    QCOMPARE(f.editor().document().count(), 3);
    // 线条工具不贴合：右键直接结束标注
    f.session->setTool(Tool::Line);
    f.h.elementDetector->setElements({QRect(150, 150, 100, 60)});
    f.session->pressAt(QPoint(180, 170), Qt::RightButton);
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
}

void TestAnnotationP1::penStylesAndRoundedRect()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.editor().setColor(Qt::red);
    f.editor().setWidth(3);
    f.drag(QPoint(150, 150), QPoint(350, 300));
    const QImage solid = f.render();
    f.editor().setPenStyle(Qt::DashLine);
    f.drag(QPoint(150, 150), QPoint(350, 300)); // 同位置再画一条虚线
    QCOMPARE(f.editor().document().at(1)->style().penStyle, Qt::DashLine);
    // 虚线矩形的边上有间隙：沿上边扫描，有红也有非红
    f.editor().document().remove(f.editor().document().at(0)->id());
    const QImage dashed = f.render();
    int red = 0, other = 0;
    for (int x = 60; x < 240; ++x)
        (dashed.pixel(x, 50) == qRgb(255, 0, 0) ? red : other)++;
    QVERIFY(red > 10 && other > 10);
    QVERIFY(differs(solid, dashed));
    // 圆角：角上的像素不再是红色，边中点仍是红色
    f.editor().setPenStyle(Qt::SolidLine);
    f.editor().setCornerRadius(30);
    f.drag(QPoint(150, 350), QPoint(350, 390)); // 高 40 → 半径限制为 20
    const auto* rr = static_cast<const RectAnnotation*>(f.editor().document().at(1));
    QCOMPARE(rr->radius(), 20.0);
    f.editor().document().remove(f.editor().document().at(0)->id());
    const QImage rounded = f.render();
    QVERIFY(rounded.pixel(50, 250) != qRgb(255, 0, 0));  // 左上角被圆掉
    QCOMPARE(rounded.pixel(150, 250), qRgb(255, 0, 0));  // 上边中点
    // 铅笔点线：沿直线扫描出现间隙
    f.session->setTool(Tool::Pencil);
    f.editor().setPenStyle(Qt::DotLine);
    f.editor().setShiftHeld(true);
    f.drag(QPoint(150, 200), QPoint(350, 200));
    f.editor().setShiftHeld(false);
    const QImage dotted = f.render();
    red = other = 0;
    for (int x = 60; x < 240; ++x)
        (dotted.pixel(x, 100) == qRgb(255, 0, 0) ? red : other)++;
    QVERIFY(red > 10 && other > 10);
}

void TestAnnotationP1::radiusHandleDrag()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.drag(QPoint(150, 150), QPoint(350, 350));
    const int id = f.editor().document().at(0)->id();
    f.editor().select(id);
    // 控制点在上边、离左上角 12px；拖到 +40 → 半径 40
    const QPointF handle = f.editor().radiusHandleImage();
    QCOMPARE(handle, QPointF(50 + 12, 50));
    const QPoint from = f.sel.topLeft() + handle.toPoint();
    f.session->pressAt(from, Qt::LeftButton);
    QCOMPARE(f.editor().dragMode(), AnnotationEditor::DragMode::Radius);
    f.session->dragTo(from + QPoint(40, 0));
    f.session->releaseAt(from + QPoint(40, 0), Qt::LeftButton);
    QCOMPARE(static_cast<const RectAnnotation*>(f.editor().document().find(id))->radius(), 40.0);
    QVERIFY(f.editor().document().canUndo());
    f.editor().undo();
    QCOMPARE(static_cast<const RectAnnotation*>(f.editor().document().find(id))->radius(), 0.0);
    // 椭圆没有圆角控制点
    f.editor().select(-1);
    f.session->setTool(Tool::Ellipse);
    f.drag(QPoint(150, 150), QPoint(250, 250));
    f.editor().select(f.editor().document().at(1)->id());
    QVERIFY(f.editor().radiusHandleImage().x() < -1e8);
}

void TestAnnotationP1::arrowHeadStyles()
{
    Fixture f;
    f.startWith(Tool::Arrow);
    f.editor().setColor(Qt::red);
    f.editor().setWidth(4);
    f.editor().setArrowHeads(LineAnnotation::Head::Dot, LineAnnotation::Head::Open); // 双向，起点圆点、终点开放箭头
    f.drag(QPoint(150, 200), QPoint(350, 200));
    const int lineId = f.editor().document().at(0)->id();
    auto line = [&] { return static_cast<const LineAnnotation*>(f.editor().document().find(lineId)); }; // 修改后对象会被克隆替换
    QCOMPARE(line()->headStart(), LineAnnotation::Head::Dot);
    QCOMPARE(line()->headEnd(), LineAnnotation::Head::Open);
    const QImage img = f.render();
    QCOMPARE(img.pixel(50, 100), qRgb(255, 0, 0));   // 起点圆点中心
    QCOMPARE(img.pixel(150, 100), qRgb(255, 0, 0));  // 线中段
    QVERIFY(img.pixel(236, 94) == qRgb(255, 0, 0) || img.pixel(237, 93) == qRgb(255, 0, 0)); // 开放箭头的一翼
    // 每种样式都能画且互不相同
    QList<QImage> renders;
    for (LineAnnotation::Head h : {LineAnnotation::Head::None, LineAnnotation::Head::Triangle, LineAnnotation::Head::Open, LineAnnotation::Head::Dot, LineAnnotation::Head::Bar}) {
        f.editor().select(lineId);
        f.editor().setArrowHeads(LineAnnotation::Head::None, h);
        QCOMPARE(line()->headEnd(), h);
        renders.append(f.render());
    }
    for (int i = 0; i < renders.size(); ++i)
        for (int j = i + 1; j < renders.size(); ++j)
            QVERIFY2(differs(renders[i], renders[j]), qPrintable(QStringLiteral("%1 vs %2").arg(i).arg(j)));
    // Tab 切到线条再切回箭头时沿用样式
    f.editor().select(-1);
    f.editor().setArrowHeads(LineAnnotation::Head::Bar, LineAnnotation::Head::Triangle);
    f.editor().toggleArrow();
    QCOMPARE(f.editor().tool(), Tool::Line);
    f.editor().toggleArrow();
    f.drag(QPoint(150, 250), QPoint(350, 250));
    const auto* second = static_cast<const LineAnnotation*>(f.editor().document().at(1));
    QCOMPARE(second->headStart(), LineAnnotation::Head::Bar);
    QCOMPARE(second->headEnd(), LineAnnotation::Head::Triangle);
    // 旧 JSON（arrowEnd 布尔）仍能读
    QJsonObject old = second->toJson();
    old.remove(QStringLiteral("headStart"));
    old.remove(QStringLiteral("headEnd"));
    old[QStringLiteral("arrowEnd")] = true;
    auto restored = annotationFromJson(old);
    QCOMPARE(static_cast<LineAnnotation*>(restored.get())->headEnd(), LineAnnotation::Head::Triangle);
    QCOMPARE(static_cast<LineAnnotation*>(restored.get())->headStart(), LineAnnotation::Head::None);
}

void TestAnnotationP1::textDecor()
{
    TextAnnotation plain;
    plain.setText(QStringLiteral("Hi"));
    plain.setPosition(QPointF(20, 20));
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(24);
    plain.setFont(font);
    AnnotationStyle style;
    style.stroke = Qt::white;
    plain.setStyle(style);
    auto renderOn = [](const Annotation& a) {
        QImage img(200, 100, QImage::Format_ARGB32);
        img.fill(Qt::white); // 白底白字：没有装饰时几乎看不见
        QPainter p(&img);
        a.paint(p, RenderContext{});
        return img;
    };
    const QImage none = renderOn(plain);
    // 描边：出现深色像素
    TextDecor d;
    d.outline = true;
    plain.setDecor(d);
    const QImage outlined = renderOn(plain);
    QVERIFY(differs(none, outlined));
    int dark = 0;
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 200; ++x)
            if (qGray(outlined.pixel(x, y)) < 100)
                ++dark;
    QVERIFY(dark > 20);
    // 背景：包围盒扩大 padding，背景色出现在文字周围
    d = TextDecor();
    d.background = true;
    d.backgroundColor = QColor(0, 0, 255);
    d.backgroundPadding = 8;
    plain.setDecor(d);
    QCOMPARE(plain.bounds().topLeft(), QPointF(20, 20));
    QVERIFY(plain.bounds().width() > QFontMetricsF(font).horizontalAdvance(QStringLiteral("Hi")) + 16);
    const QImage boxed = renderOn(plain);
    QCOMPARE(boxed.pixel(24, 24), qRgb(0, 0, 255)); // 文字左上内边距处是背景色
    // 投影：出现半透明黑混合后的灰色像素
    d = TextDecor();
    d.shadow = true;
    d.shadowColor = QColor(0, 0, 0, 255);
    d.shadowOffset = QPointF(3, 3);
    plain.setDecor(d);
    const QImage shadowed = renderOn(plain);
    QVERIFY(differs(none, shadowed));
    // 序列化往返
    auto copy = annotationFromJson(plain.toJson());
    QCOMPARE(static_cast<TextAnnotation*>(copy.get())->decor(), d);
    // 编辑器：装饰作用于新建文字与选中文字
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.selectAfterCreate"), true);
    f.startWith(Tool::Text);
    TextDecor ed = f.editor().textDecor();
    ed.outline = true;
    f.editor().setTextDecor(ed);
    f.click(QPoint(200, 200));
    QVERIFY(f.editor().textEditing());
    QVERIFY(f.textEdit());
    f.textEdit()->setPlainText(QStringLiteral("abc"));
    f.editor().commitText();
    const int textId = f.editor().document().at(0)->id();
    QVERIFY(static_cast<const TextAnnotation*>(f.editor().document().find(textId))->decor().outline);
    ed.background = true;
    f.editor().setTextDecor(ed);
    QVERIFY(static_cast<const TextAnnotation*>(f.editor().document().find(textId))->decor().background);
    QVERIFY(f.editor().document().canUndo());
}

void TestAnnotationP1::stepNumbersAutoIncrementStylesPointerNoteRenumber()
{
    Fixture f;
    f.startWith(Tool::Step);
    f.editor().setColor(QColor(230, 60, 60));
    f.click(QPoint(150, 150));
    f.click(QPoint(250, 150));
    f.drag(QPoint(350, 150), QPoint(400, 250)); // 拖出指示箭头
    QCOMPARE(f.editor().document().count(), 3);
    auto step = [&](int index) { return static_cast<const StepAnnotation*>(f.editor().document().at(index)); }; // 修改后对象会被克隆替换
    QCOMPARE(step(0)->number(), 1);
    QCOMPARE(step(1)->number(), 2);
    QCOMPARE(step(2)->number(), 3);
    QVERIFY(!step(0)->tail());
    QVERIFY(step(2)->tail());
    QCOMPARE(*step(2)->tail(), QPointF(300, 150));
    QCOMPARE(step(2)->center(), QPointF(250, 50));
    // 徽标中心是填充色，箭头终点附近有颜色
    QImage img = f.render();
    QVERIFY(img.pixel(50, 50) != FakeScreenCapture::patternPixel(0, 150, 150));
    QVERIFY(img.pixel(298, 148) != FakeScreenCapture::patternPixel(0, 398, 248) || img.pixel(295, 145) != FakeScreenCapture::patternPixel(0, 395, 245));
    // 样式：字母 / 罗马
    QCOMPARE(StepAnnotation::formatNumber(1, StepAnnotation::NumberStyle::Alpha), QStringLiteral("A"));
    QCOMPARE(StepAnnotation::formatNumber(27, StepAnnotation::NumberStyle::Alpha), QStringLiteral("AA"));
    QCOMPARE(StepAnnotation::formatNumber(4, StepAnnotation::NumberStyle::Roman), QStringLiteral("IV"));
    QCOMPARE(StepAnnotation::formatNumber(1994, StepAnnotation::NumberStyle::Roman), QStringLiteral("MCMXCIV"));
    f.editor().setStepStyle(StepAnnotation::NumberStyle::Roman);
    QCOMPARE(step(1)->label(), QStringLiteral("II"));
    QCOMPARE(step(2)->numberStyle(), StepAnnotation::NumberStyle::Roman);
    f.editor().undo();
    QCOMPARE(step(1)->label(), QStringLiteral("2"));
    // 手动改值：下一个自动编号跟着最大值走
    f.editor().select(f.editor().document().at(1)->id());
    f.editor().setStepNumber(10);
    QCOMPARE(f.editor().nextStepNumber(), 11);
    f.editor().setStepNumber(2);
    f.editor().select(-1);
    // 说明文字：双击徽标输入
    f.session->doubleClickAt(QPoint(150, 150));
    QVERIFY(f.editor().textEditing());
    QVERIFY(f.textEdit());
    f.textEdit()->setPlainText(QStringLiteral("first"));
    f.editor().commitText();
    QCOMPARE(step(0)->text(), QStringLiteral("first"));
    QVERIFY(f.editor().document().at(0)->bounds().right() > 100);
    // 删除中间序号：递补（可配置）
    f.editor().select(f.editor().document().at(1)->id());
    f.editor().deleteSelected();
    QCOMPARE(f.editor().document().count(), 2);
    QCOMPARE(step(1)->number(), 2);
    f.editor().undo(); // 一条宏命令：删除与递补一起撤销
    QCOMPARE(f.editor().document().count(), 3);
    QCOMPARE(step(2)->number(), 3);
    f.settings.setValue(QStringLiteral("annotation.stepRenumber"), false);
    f.editor().select(f.editor().document().at(1)->id());
    f.editor().deleteSelected();
    QCOMPARE(step(1)->number(), 3);
    // 序列化往返
    auto copy = annotationFromJson(f.editor().document().at(1)->toJson());
    const auto* restored = static_cast<const StepAnnotation*>(copy.get());
    QCOMPARE(restored->number(), 3);
    QVERIFY(restored->tail());
}

void TestAnnotationP1::magnifierAnnotation()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.magnifierZoom"), 3);
    f.startWith(Tool::Rect);
    f.editor().setColor(Qt::red);
    f.editor().setFilled(true);
    f.drag(QPoint(150, 150), QPoint(170, 170)); // 一个红块，位于图像 (50,50)-(70,70)
    f.session->setTool(Tool::Magnifier);
    f.editor().setColor(Qt::blue);
    f.editor().setWidth(2);
    f.drag(QPoint(140, 140), QPoint(180, 180)); // 源区域 (40,40,40,40)，含红块
    QCOMPARE(f.editor().document().count(), 2);
    const int magId = f.editor().document().at(1)->id();
    auto mag = [&] { return static_cast<const MagnifierAnnotation*>(f.editor().document().find(magId)); }; // 修改后对象会被克隆替换
    QCOMPARE(mag()->source(), QRectF(40, 40, 40, 40));
    QCOMPARE(mag()->zoom(), 3.0);
    QCOMPARE(mag()->target(), QRectF(80 + 16, 40, 120, 120)); // 紧挨右侧
    // 默认不包含下层标注：目标里对应红块的位置是底图放大，不是红色
    QImage img = f.render();
    const QPointF redInTarget = mag()->target().topLeft() + QPointF((60 - 40) * 3, (60 - 40) * 3);
    QVERIFY(img.pixel(redInTarget.toPoint()) != qRgb(255, 0, 0));
    // 包含下层标注：变红
    f.editor().select(magId);
    f.editor().setMagnifierIncludeAnnotations(true);
    img = f.render();
    QCOMPARE(img.pixel(redInTarget.toPoint()), qRgb(255, 0, 0));
    // 边框是蓝色
    QCOMPARE(img.pixel(QPoint(96 + 60, 40)), qRgb(0, 0, 255));
    // 倍数改为 2：目标尺寸随之变化；椭圆形状；阴影开关影响渲染
    f.editor().setMagnifierZoom(2);
    QCOMPARE(mag()->target().size(), QSizeF(80, 80));
    const QImage withShadow = f.render();
    f.editor().setMagnifierShadow(false);
    QVERIFY(differs(withShadow, f.render()));
    f.editor().setMagnifierShape(MagnifierAnnotation::Shape::Ellipse);
    QCOMPARE(mag()->shape(), MagnifierAnnotation::Shape::Ellipse);
    img = f.render();
    QVERIFY(img.pixel(mag()->target().topLeft().toPoint()) != qRgb(0, 0, 255)); // 椭圆的角没有边框
    // 连接线样式用线型
    f.editor().setPenStyle(Qt::DashLine);
    QCOMPARE(mag()->style().penStyle, Qt::DashLine);
    // 移动目标不动源
    f.editor().select(-1);
    f.editor().select(magId);
    const QRectF before = mag()->target();
    const QPoint inside = f.sel.topLeft() + mag()->target().center().toPoint();
    f.session->pressAt(inside, Qt::LeftButton);
    f.session->dragTo(inside + QPoint(30, 40));
    f.session->releaseAt(inside + QPoint(30, 40), Qt::LeftButton);
    QCOMPARE(mag()->target().topLeft(), before.topLeft() + QPointF(30, 40));
    QCOMPARE(mag()->source(), QRectF(40, 40, 40, 40));
    // 放不下时放到左侧 / 下方
    MagnifierAnnotation m2;
    m2.setSource(QRectF(300, 10, 50, 50));
    m2.placeTarget(2, QRectF(0, 0, 400, 300));
    QCOMPARE(m2.target(), QRectF(300 - 16 - 100, 10, 100, 100));
    MagnifierAnnotation m3;
    m3.setSource(QRectF(10, 10, 300, 50));
    m3.placeTarget(1, QRectF(0, 0, 400, 300));
    QCOMPARE(m3.target().top(), 10 + 50 + 16);
    // 序列化往返
    auto copy = annotationFromJson(mag()->toJson());
    const auto* restored = static_cast<const MagnifierAnnotation*>(copy.get());
    QCOMPARE(restored->source(), mag()->source());
    QCOMPARE(restored->target(), mag()->target());
    QVERIFY(restored->includeAnnotations());
    QVERIFY(!restored->shadow());
}

void TestAnnotationP1::toolbarRowsAndJsonRoundTrip()
{
    Fixture f;
    f.startWith(Tool::Arrow);
    CaptureToolbar* tb = f.session->toolbar();
    QVERIFY(tb);
    QVERIFY(tb->button(QStringLiteral("step")));
    QVERIFY(tb->button(QStringLiteral("magnifier")));
    QCOMPARE(CaptureToolbar::toolFromName(QStringLiteral("step")), Tool::Step);
    QCOMPARE(CaptureToolbar::toolName(Tool::Magnifier), QStringLiteral("magnifier"));
    auto* arrowEnd = tb->findChild<QComboBox*>(QStringLiteral("arrowEnd"));
    QVERIFY(arrowEnd && arrowEnd->isVisibleTo(tb->optionsRow()));
    arrowEnd->setCurrentIndex(3); // dot
    QCOMPARE(f.editor().arrowHeadEnd(), LineAnnotation::Head::Dot);
    tb->findChild<QToolButton*>(QStringLiteral("style_dash"))->click();
    QCOMPARE(f.editor().style().penStyle, Qt::DashLine);
    f.session->setTool(Tool::Rect);
    auto* radius = tb->findChild<QSpinBox*>(QStringLiteral("radius"));
    QVERIFY(radius->isVisibleTo(tb->optionsRow()));
    radius->setValue(9);
    QCOMPARE(f.editor().cornerRadius(), 9.0);
    f.session->setTool(Tool::Step);
    tb->findChild<QComboBox*>(QStringLiteral("stepStyle"))->setCurrentIndex(1);
    QCOMPARE(f.editor().stepStyle(), StepAnnotation::NumberStyle::Alpha);
    f.session->setTool(Tool::Magnifier);
    tb->findChild<QSpinBox*>(QStringLiteral("zoom"))->setValue(5);
    QCOMPARE(f.editor().magnifierZoom(), 5.0);
    f.session->setTool(Tool::Text);
    tb->findChild<QToolButton*>(QStringLiteral("outline"))->click();
    QVERIFY(f.editor().textDecor().outline);
    // 文档含全部新类型时可序列化往返（PIN-54 恢复）
    f.session->setTool(Tool::Rect);
    f.editor().setCornerRadius(5);
    f.drag(QPoint(150, 150), QPoint(250, 250));
    f.session->setTool(Tool::Step);
    f.click(QPoint(300, 300));
    f.session->setTool(Tool::Magnifier);
    f.drag(QPoint(150, 300), QPoint(200, 350));
    const QJsonObject json = f.editor().document().toJson();
    AnnotationDocument doc;
    doc.setBaseImage(f.editor().document().baseImage());
    doc.fromJson(json);
    QCOMPARE(doc.count(), 3);
    QCOMPARE(doc.at(0)->type(), QStringLiteral("rect"));
    QCOMPARE(static_cast<const RectAnnotation*>(doc.at(0))->radius(), 5.0);
    QCOMPARE(doc.at(1)->type(), QStringLiteral("step"));
    QCOMPARE(doc.at(2)->type(), QStringLiteral("magnifier"));
    QCOMPARE(doc.render(), f.editor().document().render());
}

QTEST_MAIN(TestAnnotationP1)
#include "tst_annotation_p1.moc"
