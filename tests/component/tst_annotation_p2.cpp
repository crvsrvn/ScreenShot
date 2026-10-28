// REQ: ANN-04 ANN-08 ANN-12 ANN-14 ANN-15 ANN-16 ANN-22 ANN-24 ANN-26 ANN-30 ANN-31 ANN-34 ANN-38
// M6 标注 P2：色板编辑 / 透明度、框选多选、新建后自动选中、自定义工具栏、记住工具 / 默认工具、贴图隐藏标注、
// 扇形 / 弧形、端点与连接样式、箭头注释、智能擦除、自动马赛克、文字换行与指示箭头、聚光灯、水印
#include "app/AnnotationEditor.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "app/PinWindow.h"
#include "core/annotation/Overlays.h"
#include "core/config/Settings.h"
#include "core/fx/Inpaint.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

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
    QPlainTextEdit* textEdit()
    {
        for (auto* w : session->overlays())
            if (auto* e = w->findChild<QPlainTextEdit*>())
                return e;
        return nullptr;
    }
};

int countColor(const QImage& img, QRgb color)
{
    int n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (img.pixel(x, y) == color)
                ++n;
    return n;
}

} // namespace

class TestAnnotationP2 : public QObject {
    Q_OBJECT
private slots:
    void paletteEditImportExportWheel();
    void marqueeMultiSelectMoveDelete();
    void selectAfterCreate();
    void customToolbarCommonMoreRecent();
    void defaultToolbarKeepsCommonOnly();
    void rememberToolAndDefaultTool();
    void pinHideAnnotationsAndKeepAfterRotate();
    void ellipseArcHandles();
    void capAndJoinStyles();
    void arrowNoteOnDoubleClick();
    void smartEraseInpaint();
    void autoMosaicGroup();
    void effectOutlineOnScreenOnly();
    void textWrapAndPointer();
    void spotlight();
};

void TestAnnotationP2::paletteEditImportExportWheel()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.palette"), QStringLiteral("#ff0000,#00ff00"));
    f.startWith(Tool::Rect);
    CaptureToolbar* tb = f.session->toolbar();
    QCOMPARE(tb->paletteColors(), (QList<QColor>{QColor(255, 0, 0), QColor(0, 255, 0)}));
    QCOMPARE(tb->colorButtons().size(), 2);
    tb->colorButtons()[1]->click();
    QCOMPARE(f.editor().style().stroke, QColor(0, 255, 0));
    // 排序 / 编辑通过 savePalette；导出 / 导入
    tb->savePalette({QColor(0, 255, 0), QColor(255, 0, 0), QColor(0, 0, 255, 128)});
    QCOMPARE(f.settings.stringValue(QStringLiteral("annotation.palette")), QStringLiteral("#00ff00,#ff0000,#800000ff"));
    QCOMPARE(tb->colorButtons().size(), 3);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("palette.txt"));
    QVERIFY(tb->exportPalette(path));
    tb->savePalette({QColor(1, 2, 3)});
    QCOMPARE(tb->colorButtons().size(), 1);
    QVERIFY(tb->importPalette(path));
    QCOMPARE(tb->paletteColors().size(), 3);
    QCOMPARE(tb->paletteColors()[2], QColor(0, 0, 255, 128));
    // 滚轮：透明度 ±10 并应用
    QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, -240), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(tb->colorButtons()[0], &wheel);
    QCOMPARE(tb->paletteColors()[0].alpha(), 235);
    QCOMPARE(f.editor().style().stroke.alpha(), 235);
}

void TestAnnotationP2::marqueeMultiSelectMoveDelete()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.editor().setFilled(true);
    f.drag(QPoint(150, 150), QPoint(200, 200));
    f.drag(QPoint(250, 150), QPoint(300, 200));
    f.drag(QPoint(400, 300), QPoint(450, 350));
    QCOMPARE(f.editor().document().count(), 3);
    const int a = f.editor().document().at(0)->id(), b = f.editor().document().at(1)->id(), c = f.editor().document().at(2)->id();
    // 选择模式下在空白处框选前两个
    f.session->setTool(Tool::None);
    f.session->pressAt(QPoint(120, 120), Qt::LeftButton);
    QCOMPARE(f.editor().dragMode(), AnnotationEditor::DragMode::Marquee);
    f.session->dragTo(QPoint(330, 230));
    QVERIFY(!f.editor().marqueeRect().isEmpty());
    f.session->releaseAt(QPoint(330, 230), Qt::LeftButton);
    QCOMPARE(f.editor().selectedIds().size(), 2);
    QVERIFY(f.editor().selectedIds().contains(a) && f.editor().selectedIds().contains(b));
    // 拖动其中一个：一起移动，可一次撤销
    f.session->pressAt(QPoint(175, 175), Qt::LeftButton);
    f.session->dragTo(QPoint(185, 195));
    f.session->releaseAt(QPoint(185, 195), Qt::LeftButton);
    QCOMPARE(f.editor().document().find(a)->bounds().topLeft(), QPointF(60, 70));
    QCOMPARE(f.editor().document().find(b)->bounds().topLeft(), QPointF(160, 70));
    QCOMPARE(f.editor().document().find(c)->bounds().topLeft(), QPointF(300, 200));
    f.editor().undo();
    QCOMPARE(f.editor().document().find(a)->bounds().topLeft(), QPointF(50, 50));
    QCOMPARE(f.editor().document().find(b)->bounds().topLeft(), QPointF(150, 50));
    // 一起删除，一次撤销恢复
    f.editor().selectMany({a, b});
    f.editor().deleteSelected();
    QCOMPARE(f.editor().document().count(), 1);
    f.editor().undo();
    QCOMPARE(f.editor().document().count(), 3);
    // 单击别处清掉多选
    f.editor().selectMany({a, b});
    f.click(QPoint(425, 325));
    QCOMPARE(f.editor().selectedIds(), QList<int>{c});
}

void TestAnnotationP2::selectAfterCreate()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.selectAfterCreate"), true);
    f.startWith(Tool::Rect);
    f.drag(QPoint(150, 150), QPoint(200, 200));
    QCOMPARE(f.editor().selectedId(), f.editor().document().at(0)->id());
    f.settings.setValue(QStringLiteral("annotation.selectAfterCreate"), false);
    f.drag(QPoint(250, 250), QPoint(300, 300));
    QCOMPARE(f.editor().selectedId(), -1);
}

void TestAnnotationP2::customToolbarCommonMoreRecent()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.toolbarCommon"), QStringLiteral("arrow,rect"));
    f.startWith(Tool::Rect);
    CaptureToolbar* tb = f.session->toolbar();
    QVERIFY(tb->button(QStringLiteral("rect"))->isVisibleTo(tb));
    QVERIFY(tb->button(QStringLiteral("arrow"))->isVisibleTo(tb));
    QVERIFY(!tb->button(QStringLiteral("mosaic"))->isVisibleTo(tb));
    QVERIFY(tb->button(QStringLiteral("more"))->isVisibleTo(tb));
    QVERIFY(!tb->button(QStringLiteral("recent"))->isVisibleTo(tb));
    // 用折叠区工具后出现“最近”按钮
    f.session->setTool(Tool::Mosaic);
    QVERIFY(tb->button(QStringLiteral("recent"))->isVisibleTo(tb));
    QVERIFY(tb->button(QStringLiteral("recent"))->isChecked());
    f.session->setTool(Tool::Rect);
    QVERIFY(!tb->button(QStringLiteral("recent"))->isChecked());
    tb->button(QStringLiteral("recent"))->click();
    QCOMPARE(f.editor().tool(), Tool::Mosaic);
}

void TestAnnotationP2::defaultToolbarKeepsCommonOnly()
{
    Fixture f;
    f.startWith(Tool::Rect);
    CaptureToolbar* tb = f.session->toolbar();
    QVERIFY(tb->testAttribute(Qt::WA_AlwaysShowToolTips)); // 工具栏不激活，也要显示悬停提示
    // 主栏只有常用工具与输出按钮；撤销 / 重做只用快捷键，椭圆、直线分别是框选、箭头工具的形状选项
    for (const char* name : {"rect", "arrow", "pencil", "mosaic", "text", "step", "more", "pin", "save", "cancel", "copy"})
        QVERIFY2(tb->button(QString::fromLatin1(name))->isVisibleTo(tb), name);
    for (const char* name : {"ellipse", "line", "undo", "redo"})
        QVERIFY2(!tb->button(QString::fromLatin1(name)), name);
    // 其余工具和低频操作在“更多”面板里，仍能按名称找到并使用
    for (const char* name : {"highlighter", "blur", "eraser", "clear", "mode", "scroll", "ratio", "shadow", "border"})
        QVERIFY2(!tb->button(QString::fromLatin1(name))->isVisibleTo(tb), name);
    tb->button(QStringLiteral("highlighter"))->click();
    QCOMPARE(f.editor().tool(), Tool::Highlighter);
    QVERIFY(tb->button(QStringLiteral("recent"))->isVisibleTo(tb));
    // 箭头工具：形状选项在箭头 / 直线之间切换
    tb->button(QStringLiteral("arrow"))->click();
    QCOMPARE(f.editor().tool(), Tool::Arrow);
    QToolButton* line = tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_line"));
    QVERIFY(line && line->isVisibleTo(tb));
    QVERIFY(!tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_rect"))->isVisibleTo(tb));
    line->click();
    QCOMPARE(f.editor().tool(), Tool::Line);
    QVERIFY(tb->button(QStringLiteral("arrow"))->isChecked());
    QVERIFY(line->isChecked());
    QVERIFY(!tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_arrow"))->isChecked());
    // 框选工具：形状选项在矩形 / 椭圆之间切换，主栏按钮保持选中
    tb->button(QStringLiteral("rect"))->click();
    QCOMPARE(f.editor().tool(), Tool::Rect);
    QToolButton* ellipse = tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_ellipse"));
    QVERIFY(ellipse && ellipse->isVisibleTo(tb));
    QVERIFY(!tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_brush"))->isVisibleTo(tb));
    QVERIFY(!line->isVisibleTo(tb));
    ellipse->click();
    QCOMPARE(f.editor().tool(), Tool::Ellipse);
    QVERIFY(tb->button(QStringLiteral("rect"))->isChecked());
    QVERIFY(ellipse->isChecked());
    tb->optionsRow()->findChild<QToolButton*>(QStringLiteral("shape_rect"))->click();
    QCOMPARE(f.editor().tool(), Tool::Rect);
}

void TestAnnotationP2::effectOutlineOnScreenOnly()
{
    // 矩形马赛克在屏幕上画出边界，导出图里没有
    Fixture f;
    f.startWith(Tool::Mosaic);
    f.editor().setEffectShape(EffectAnnotation::Shape::Rect);
    f.drag(QPoint(150, 150), QPoint(300, 250));
    QCOMPARE(f.editor().document().count(), 1);
    const QRect box = f.editor().document().at(0)->bounds().toAlignedRect();
    const QImage exported = f.editor().document().render().convertToFormat(QImage::Format_ARGB32);
    QImage screen(exported.size(), QImage::Format_ARGB32_Premultiplied);
    screen.fill(Qt::transparent);
    {
        QPainter p(&screen);
        f.editor().paint(p, QPointF(0, 0), 1.0);
    }
    screen = screen.convertToFormat(QImage::Format_ARGB32);
    int onEdge = 0;
    for (int y = 0; y < screen.height(); ++y)
        for (int x = 0; x < screen.width(); ++x) {
            if (screen.pixel(x, y) == exported.pixel(x, y))
                continue;
            QVERIFY2(box.adjusted(-2, -2, 2, 2).contains(x, y) && !box.adjusted(2, 2, -2, -2).contains(x, y), qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
            ++onEdge;
        }
    QVERIFY(onEdge > box.width());
}

void TestAnnotationP2::rememberToolAndDefaultTool()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.rememberTool"), true);
    f.startWith(Tool::Ellipse);
    QCOMPARE(f.settings.stringValue(QStringLiteral("annotation.lastToolCapture")), QStringLiteral("ellipse"));
    f.session->cancel();
    // 下次选区确定后直接进入上次的工具
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(f.sel);
    QCOMPARE(f.session->state(), CaptureSession::State::Annotating);
    QCOMPARE(f.editor().tool(), Tool::Ellipse);
    f.session->cancel();
    // 默认工具
    f.settings.setValue(QStringLiteral("annotation.rememberTool"), false);
    f.settings.setValue(QStringLiteral("annotation.defaultCaptureTool"), QStringLiteral("arrow"));
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(f.sel);
    QCOMPARE(f.editor().tool(), Tool::Arrow);
    f.session->cancel();
    f.settings.setValue(QStringLiteral("annotation.defaultCaptureTool"), QStringLiteral("none"));
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(f.sel);
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    f.session->cancel();
    // 贴图单独记忆
    f.settings.setValue(QStringLiteral("annotation.rememberTool"), true);
    QImage img(100, 80, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    PinWindow pin(img, f.settings);
    pin.setTool(Tool::Pencil);
    QCOMPARE(f.settings.stringValue(QStringLiteral("annotation.lastToolPin")), QStringLiteral("pencil"));
    pin.toggleToolbar();
    pin.toggleToolbar();
    QCOMPARE(pin.editor().tool(), Tool::Pencil);
}

void TestAnnotationP2::pinHideAnnotationsAndKeepAfterRotate()
{
    Settings s;
    QImage img(100, 60, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgb(10, 20, 30));
    PinWindow pin(img, s);
    pin.setTool(Tool::Rect);
    pin.editor().setFilled(true);
    pin.editor().setColor(Qt::red);
    pin.editor().pressAt(QPointF(10, 10), Qt::LeftButton, Qt::NoModifier);
    pin.editor().moveTo(QPointF(30, 30));
    pin.editor().releaseAt(QPointF(30, 30), Qt::LeftButton);
    pin.toggleToolbar();
    QVERIFY(pin.hasAnnotations());
    QCOMPARE(pin.displayedImage().pixel(20, 20), qRgb(255, 0, 0));
    QCOMPARE(pin.originalImage().pixel(20, 20), qRgb(10, 20, 30));
    pin.setAnnotationsHidden(true);
    QCOMPARE(pin.displayedImage().pixel(20, 20), qRgb(10, 20, 30));
    pin.setAnnotationsHidden(false);
    QCOMPARE(pin.displayedImage().pixel(20, 20), qRgb(255, 0, 0));
    // 旋转后标注仍在
    pin.rotateQuarter(1);
    QCOMPARE(pin.displayedImage().size(), QSize(60, 100));
    QCOMPARE(pin.displayedImage().pixel(60 - 21, 20), qRgb(255, 0, 0));
    pin.rotateQuarter(-1);
    // 再标注一次：叠加
    pin.setTool(Tool::Rect);
    pin.editor().setFilled(true);
    pin.editor().setColor(Qt::blue);
    pin.editor().pressAt(QPointF(50, 10), Qt::LeftButton, Qt::NoModifier);
    pin.editor().moveTo(QPointF(70, 30));
    pin.editor().releaseAt(QPointF(70, 30), Qt::LeftButton);
    pin.toggleToolbar();
    QCOMPARE(pin.displayedImage().pixel(20, 20), qRgb(255, 0, 0));
    QCOMPARE(pin.displayedImage().pixel(60, 20), qRgb(0, 0, 255));
}

void TestAnnotationP2::ellipseArcHandles()
{
    Fixture f;
    f.startWith(Tool::Ellipse);
    f.editor().setColor(Qt::red);
    f.editor().setWidth(4);
    f.drag(QPoint(150, 150), QPoint(350, 350)); // 图像 (50,50) 200x200
    const int id = f.editor().document().at(0)->id();
    f.editor().select(id);
    auto ellipse = [&] { return static_cast<const EllipseAnnotation*>(f.editor().document().find(id)); };
    QVERIFY(ellipse()->isFullEllipse());
    const QImage full = f.render();
    // 控制点在 0° 处（右侧中点）
    QCOMPARE(f.editor().arcHandleImage(false), QPointF(250, 150));
    // 拖终点到 90°（顶部）：弧 0..90
    const QPoint endHandle = f.sel.topLeft() + QPoint(250, 150);
    f.session->pressAt(endHandle, Qt::LeftButton);
    QCOMPARE(f.editor().dragMode(), AnnotationEditor::DragMode::ArcEnd);
    f.session->dragTo(f.sel.topLeft() + QPoint(150, 50));
    f.session->releaseAt(f.sel.topLeft() + QPoint(150, 50), Qt::LeftButton);
    QVERIFY(std::abs(ellipse()->spanAngle() - 90) < 2);
    const QImage arc = f.render();
    QVERIFY(arc != full);
    // 弧只在右上象限：左下角上的原椭圆边缘不再是红
    QVERIFY(full.pixel(50 + 29, 50 + 171) == qRgb(255, 0, 0) || full.pixel(50 + 30, 50 + 170) == qRgb(255, 0, 0));
    QVERIFY(countColor(arc.copy(50, 150, 100, 100), qRgb(255, 0, 0)) == 0);
    // 填充 → 扇形；API 直接设置
    f.editor().setFilled(true);
    f.editor().setArc(180, 90);
    QCOMPARE(ellipse()->startAngle(), 180.0);
    const QImage pie = f.render();
    QCOMPARE(pie.pixel(150 - 30, 150 + 30), qRgb(255, 0, 0)); // 左下象限内
    QVERIFY(pie.pixel(150 + 30, 150 - 30) != qRgb(255, 0, 0));
    QVERIFY(f.editor().document().canUndo());
    // 序列化
    auto copy = annotationFromJson(ellipse()->toJson());
    QCOMPARE(static_cast<EllipseAnnotation*>(copy.get())->spanAngle(), 90.0);
}

void TestAnnotationP2::capAndJoinStyles()
{
    Fixture f;
    f.startWith(Tool::Line);
    f.editor().setColor(Qt::red);
    f.editor().setWidth(10);
    f.editor().setArrowHeads(LineAnnotation::Head::None, LineAnnotation::Head::None);
    f.drag(QPoint(200, 200), QPoint(300, 200));
    const QImage round = f.render();
    f.editor().select(f.editor().document().at(0)->id());
    f.editor().setCapStyle(Qt::SquareCap);
    QCOMPARE(f.editor().document().at(0)->style().capStyle, Qt::SquareCap);
    const QImage square = f.render();
    QVERIFY(round != square);
    QVERIFY(countColor(square, qRgb(255, 0, 0)) > countColor(round, qRgb(255, 0, 0))); // 方形端点覆盖更多
    f.editor().setJoinStyle(Qt::MiterJoin);
    QCOMPARE(f.editor().document().at(0)->style().joinStyle, Qt::MiterJoin);
    auto copy = annotationFromJson(f.editor().document().at(0)->toJson());
    QCOMPARE(copy->style().joinStyle, Qt::MiterJoin);
    QCOMPARE(copy->style().capStyle, Qt::SquareCap);
    // 工具栏按钮
    CaptureToolbar* tb = f.session->toolbar();
    QVERIFY(tb->findChild<QToolButton*>(QStringLiteral("capSquare"))->isChecked());
    tb->findChild<QToolButton*>(QStringLiteral("capSquare"))->click();
    QCOMPARE(f.editor().style().capStyle, Qt::RoundCap);
}

void TestAnnotationP2::arrowNoteOnDoubleClick()
{
    Fixture f;
    f.startWith(Tool::Arrow);
    f.editor().setColor(Qt::red);
    f.drag(QPoint(150, 200), QPoint(300, 200));
    const int id = f.editor().document().at(0)->id();
    const QImage before = f.render();
    f.session->doubleClickAt(QPoint(225, 200));
    QVERIFY(f.editor().textEditing());
    QVERIFY(f.textEdit());
    f.textEdit()->setPlainText(QStringLiteral("note"));
    f.editor().commitText();
    const auto* line = static_cast<const LineAnnotation*>(f.editor().document().find(id));
    QCOMPARE(line->text(), QStringLiteral("note"));
    QVERIFY(line->bounds().right() > 200 + 8);
    QVERIFY(f.render() != before);
    // 关闭后双击不再弹输入
    f.settings.setValue(QStringLiteral("annotation.arrowNote"), false);
    f.session->doubleClickAt(QPoint(225, 200));
    QVERIFY(!f.editor().textEditing());
}

void TestAnnotationP2::smartEraseInpaint()
{
    // 渐变背景上的黑块：修补后接近渐变
    QImage img(120, 80, QImage::Format_RGB32);
    for (int y = 0; y < 80; ++y)
        for (int x = 0; x < 120; ++x)
            img.setPixel(x, y, qRgb(x * 2, 100, 100));
    for (int y = 30; y < 50; ++y)
        for (int x = 50; x < 70; ++x)
            img.setPixel(x, y, qRgb(0, 0, 0));
    QPainterPath mask;
    mask.addRect(QRectF(50, 30, 20, 20));
    const QImage fixed = fx::inpaint(img, mask);
    QVERIFY(std::abs(qRed(fixed.pixel(60, 40)) - 120) < 20);
    QVERIFY(std::abs(qGreen(fixed.pixel(60, 40)) - 100) < 10);
    QCOMPARE(fixed.pixel(10, 10), qRgb(20, 100, 100)); // 区域外不变
    // 工具
    Fixture f;
    f.h.screenCapture->setImage(0, [&] {
        QImage s = FakeScreenCapture::patternImage(0, f.h.screenCapture->screens().first().physicalGeometry.size());
        QPainter p(&s);
        p.fillRect(QRect(100, 100, 400, 300), QColor(200, 200, 200));
        p.fillRect(QRect(250, 200, 30, 30), Qt::black);
        return s;
    }());
    f.startWith(Tool::SmartErase);
    f.drag(QPoint(245, 195), QPoint(285, 235));
    QCOMPARE(f.editor().document().count(), 1);
    const auto* effect = static_cast<const EffectAnnotation*>(f.editor().document().at(0));
    QCOMPARE(effect->effect(), EffectAnnotation::Effect::Inpaint);
    const QImage out = f.render();
    const QRgb c = out.pixel(165, 115);
    QVERIFY2(qRed(c) > 180 && qGreen(c) > 180, qPrintable(QString::number(c, 16)));
}

void TestAnnotationP2::autoMosaicGroup()
{
    Fixture f;
    // 底图里放三块相同的图案
    f.h.screenCapture->setImage(0, [&] {
        QImage s = FakeScreenCapture::patternImage(0, f.h.screenCapture->screens().first().physicalGeometry.size());
        QPainter p(&s);
        p.fillRect(QRect(100, 100, 400, 300), Qt::white);
        for (const QPoint& at : {QPoint(130, 130), QPoint(300, 130), QPoint(130, 300)}) {
            p.fillRect(QRect(at, QSize(40, 24)), QColor(20, 40, 60));
            p.fillRect(QRect(at + QPoint(5, 5), QSize(10, 10)), Qt::yellow);
        }
        return s;
    }());
    f.settings.setValue(QStringLiteral("annotation.autoMosaic"), true);
    f.startWith(Tool::Mosaic);
    f.editor().setEffectShape(EffectAnnotation::Shape::Rect);
    f.drag(QPoint(130, 130), QPoint(170, 154));
    QCOMPARE(f.editor().document().count(), 3);
    int group = -1;
    for (int i = 0; i < 3; ++i) {
        const auto* e = static_cast<const EffectAnnotation*>(f.editor().document().at(i));
        QVERIFY(e->groupId() > 0);
        if (group < 0)
            group = e->groupId();
        QCOMPARE(e->groupId(), group);
    }
    QCOMPARE(f.editor().document().at(1)->bounds().topLeft(), QPointF(200, 30));
    QCOMPARE(f.editor().document().at(2)->bounds().topLeft(), QPointF(30, 200));
    // 一次撤销全部；删除一个同组一起删
    f.editor().undo();
    QCOMPARE(f.editor().document().count(), 0);
    f.editor().redo();
    QCOMPARE(f.editor().document().count(), 3);
    f.editor().select(f.editor().document().at(1)->id());
    f.editor().deleteSelected();
    QCOMPARE(f.editor().document().count(), 0);
    // 关闭后只打一处
    f.editor().setAutoMosaic(false);
    f.drag(QPoint(130, 130), QPoint(170, 154));
    QCOMPARE(f.editor().document().count(), 1);
}

void TestAnnotationP2::textWrapAndPointer()
{
    TextAnnotation t;
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(20);
    t.setFont(font);
    t.setText(QStringLiteral("alpha beta gamma delta epsilon"));
    const double full = t.textBox().width();
    t.setWrapWidth(full / 2 + 10);
    QVERIFY(t.wrappedLines().size() >= 2);
    QVERIFY(t.textBox().width() <= full / 2 + 10 + 1);
    QVERIFY(t.textBox().height() > 20);
    // 拖宽度 → 换行宽度；拖高度 → 字号
    const QRectF b = t.textBox();
    t.setBounds(QRectF(b.topLeft(), QSizeF(b.width() + 60, b.height())));
    QCOMPARE(t.wrapWidth(), b.width() + 60);
    // 指示箭头
    t.setPointer(QPointF(300, 300));
    QVERIFY(t.hitTest((t.textBox().center() + QPointF(300, 300)) / 2, 6)); // 连线上的点
    QImage img(400, 400, QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        AnnotationStyle s;
        s.stroke = Qt::red;
        s.width = 3;
        t.setStyle(s);
        t.setPosition(QPointF(10, 10));
        t.paint(p, RenderContext{});
    }
    QVERIFY(countColor(img, qRgb(255, 0, 0)) > 100);
    QVERIFY(qRed(img.pixel(295, 295)) > 200 && qGreen(img.pixel(295, 295)) < 100);
    auto copy = annotationFromJson(t.toJson());
    QCOMPARE(static_cast<TextAnnotation*>(copy.get())->pointer().value_or(QPointF()), QPointF(300, 300));
    QCOMPARE(static_cast<TextAnnotation*>(copy.get())->wrapWidth(), t.wrapWidth());
    // 编辑器：指示箭头控制点拖拽
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.selectAfterCreate"), true);
    f.startWith(Tool::Text);
    f.click(QPoint(150, 150));
    QVERIFY(f.textEdit());
    f.textEdit()->setPlainText(QStringLiteral("hi"));
    f.editor().commitText();
    const int id = f.editor().selectedId();
    QVERIFY(id >= 0);
    const QPointF handle = f.editor().pointerHandleImage();
    QVERIFY(handle.x() > -1e8);
    const QPoint start = f.sel.topLeft() + handle.toPoint();
    f.session->pressAt(start, Qt::LeftButton);
    QCOMPARE(f.editor().dragMode(), AnnotationEditor::DragMode::Pointer);
    f.session->dragTo(f.sel.topLeft() + QPoint(200, 220));
    f.session->releaseAt(f.sel.topLeft() + QPoint(200, 220), Qt::LeftButton);
    QCOMPARE(static_cast<const TextAnnotation*>(f.editor().document().find(id))->pointer().value_or(QPointF()), QPointF(200, 220));
}

void TestAnnotationP2::spotlight()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.spotlightOpacity"), 255);
    f.startWith(Tool::Spotlight);
    f.editor().setColor(Qt::red);
    f.editor().setWidth(0);
    f.drag(QPoint(200, 200), QPoint(300, 300));
    QCOMPARE(f.editor().document().count(), 1);
    QCOMPARE(f.editor().document().at(0)->type(), QStringLiteral("spotlight"));
    const QImage out = f.render();
    QCOMPARE(out.pixel(10, 10), qRgb(0, 0, 0)); // 区域外全黑
    QCOMPARE(out.pixel(150, 150), FakeScreenCapture::patternPixel(0, 250, 250)); // 区域内原样
    // 椭圆形状 + 边框
    f.editor().select(f.editor().document().at(0)->id());
    f.editor().setWidth(4);
    f.session->setTool(Tool::Spotlight);
    f.editor().setSpotlightShape(SpotlightAnnotation::Shape::Ellipse);
    f.drag(QPoint(350, 150), QPoint(450, 250));
    const auto* sp = static_cast<const SpotlightAnnotation*>(f.editor().document().at(1));
    QCOMPARE(sp->shape(), SpotlightAnnotation::Shape::Ellipse);
    auto copy = annotationFromJson(sp->toJson());
    QCOMPARE(static_cast<SpotlightAnnotation*>(copy.get())->maskOpacity(), 255);
}

QTEST_MAIN(TestAnnotationP2)
#include "tst_annotation_p2.moc"
