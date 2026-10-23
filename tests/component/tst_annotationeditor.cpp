// REQ: ANN-01 ANN-02 ANN-03 ANN-05 ANN-06 ANN-07 ANN-09 ANN-10 ANN-13 ANN-17 ANN-20 ANN-23 ANN-27 ANN-28 ANN-29 ANN-32 ANN-36 NFR-08 AC-03 AC-04
// 标注编辑器与会话、覆盖窗口、工具栏的集成：工具绘制、Shift 约束、二次编辑、折线、文字输入、复制粘贴、显示与导出一致
#include "app/AnnotationEditor.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QSignalSpy>
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
    CaptureHistory history{5};
    OutputActions output{settings};
    std::unique_ptr<CaptureSession> session;
    QRect sel{100, 100, 400, 300}; // 150% 屏上

    Fixture()
        : p(makeFakePlatform(&h))
    {
        session = std::make_unique<CaptureSession>(p.screenCapture.get(), p.windowDetector.get(), p.elementDetector.get(), settings);
        session->setHistory(&history);
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
    void key(const QString& seq)
    {
        const QKeyCombination c = QKeySequence(seq)[0];
        QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
        session->handleKeyPress(&e);
    }
};

} // namespace

class TestAnnotationEditor : public QObject {
    Q_OBJECT
private slots:
    void drawRectEntersAnnotatingAndExports();
    void shiftConstrainsSquareAndLineAngle();
    void selectMoveResizeRotateDelete();
    void polylineClicksAndArrowToggle();
    void pencilStraightLineWithShift();
    void textInputCommitAndReedit();
    void copyPasteUndoRedoClear();
    void widthKeysToolShortcutsAndColor();
    void mosaicBlurEraserHighlighter();
    void exportMatchesOverlayRender();
    void rightClickLeavesAnnotatingKeepsResult();
    void magnifierAndColorWhileAnnotating();
    void toolbarButtonsDriveEditor();
    void undoRedoThirtyOperations();
    void imeTextRotateExport();
};

void TestAnnotationEditor::undoRedoThirtyOperations()
{
    // AC-04：连续 30 个标注操作后撤销到底再重做到底，每一步状态正确，无残影
    Fixture f;
    f.startWith(Tool::Rect);
    const QImage empty = f.editor().document().render();
    QList<QImage> renders;
    for (int i = 0; i < 30; ++i) {
        const QPoint at(110 + (i % 10) * 30, 110 + (i / 10) * 30);
        f.drag(at, at + QPoint(14, 14));
        QCOMPARE(f.editor().document().count(), i + 1);
        renders.append(f.editor().document().render());
    }
    for (int i = 29; i >= 0; --i) {
        QCOMPARE(f.editor().document().render(), renders[i]);
        f.editor().document().undo();
        QCOMPARE(f.editor().document().count(), i);
    }
    QVERIFY(!f.editor().document().canUndo());
    QCOMPARE(f.editor().document().render(), empty); // 撤销到底后没有残影
    for (int i = 0; i < 30; ++i) {
        f.editor().document().redo();
        QCOMPARE(f.editor().document().count(), i + 1);
        QCOMPARE(f.editor().document().render(), renders[i]);
    }
    QVERIFY(!f.editor().document().canRedo());
    QCOMPARE(f.session->finalImage().size(), f.sel.size());
}

void TestAnnotationEditor::imeTextRotateExport()
{
    // AC-03：文字工具经输入法事件（预编辑 → 提交）输入中文，旋转后导出，导出图与屏幕渲染逐像素一致
    Fixture f;
    f.startWith(Tool::Text);
    f.click(QPoint(150, 150));
    QPlainTextEdit* edit = nullptr;
    for (auto* w : f.session->overlays())
        if (auto* e = w->findChild<QPlainTextEdit*>())
            edit = e;
    QVERIFY(edit);
    QInputMethodEvent preedit(QStringLiteral("zhongwen"), {});
    QCoreApplication::sendEvent(edit, &preedit);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("中文输入"));
    QCoreApplication::sendEvent(edit, &commit);
    QCOMPARE(edit->toPlainText(), QStringLiteral("中文输入"));
    f.editor().commitText();
    QCOMPARE(f.editor().document().count(), 1);
    const int id = f.editor().document().at(0)->id();
    QCOMPARE(static_cast<const TextAnnotation*>(f.editor().document().find(id))->text(), QStringLiteral("中文输入"));
    const QImage before = f.editor().document().render();
    // 旋转柄拖到正右方 → 90°（ANN-10）
    f.editor().select(id);
    const QPoint rhPhys = f.sel.topLeft() + f.editor().rotateHandleImage().toPoint();
    const QPoint right = f.sel.topLeft() + f.editor().document().find(id)->bounds().center().toPoint() + QPoint(100, 0);
    f.session->pressAt(rhPhys, Qt::LeftButton);
    f.session->dragTo(rhPhys + QPoint(40, 40));
    f.session->dragTo(right);
    f.session->releaseAt(right, Qt::LeftButton);
    QVERIFY(std::abs(f.editor().document().find(id)->rotation() - 90.0) < 0.5);
    const QImage shown = f.editor().document().render();
    QVERIFY(shown != before);
    const QImage exported = f.session->finalImage();
    QCOMPARE(exported.size(), shown.size());
    QCOMPARE(exported.convertToFormat(QImage::Format_ARGB32), shown.convertToFormat(QImage::Format_ARGB32));
}

void TestAnnotationEditor::drawRectEntersAnnotatingAndExports()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.editor().setColor(Qt::red);
    f.editor().setFilled(true);
    f.drag(QPoint(150, 150), QPoint(249, 219));
    QCOMPARE(f.editor().document().count(), 1);
    QCOMPARE(f.editor().document().at(0)->bounds(), QRectF(50, 50, 99, 69));
    const QImage out = f.session->currentImage();
    QCOMPARE(out.size(), f.sel.size());
    QCOMPARE(out.pixel(100, 100), qRgb(255, 0, 0));
    QCOMPARE(out.pixel(10, 10), FakeScreenCapture::patternPixel(0, 110, 110));
    QSignalSpy captured(f.session.get(), &CaptureSession::captured);
    f.key(QStringLiteral("Return"));
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(0).value<QImage>().pixel(100, 100), qRgb(255, 0, 0));
    QVERIFY(!f.session->isActive());
    QVERIFY(!f.editor().active());
}

void TestAnnotationEditor::shiftConstrainsSquareAndLineAngle()
{
    Fixture f;
    f.startWith(Tool::Ellipse);
    f.editor().setShiftHeld(true);
    f.drag(QPoint(150, 150), QPoint(250, 190));
    QCOMPARE(f.editor().document().at(0)->bounds(), QRectF(50, 50, 100, 100));
    f.editor().setShiftHeld(false);
    f.session->setTool(Tool::Line);
    f.editor().setShiftHeld(true);
    f.drag(QPoint(150, 150), QPoint(250, 160)); // 接近水平 → 吸附到 0°
    const auto* line = static_cast<const LineAnnotation*>(f.editor().document().at(1));
    QCOMPARE(line->points().size(), 2);
    QVERIFY(std::abs(line->points().last().y() - 50) < 0.01);
    QVERIFY(line->points().last().x() > 140);
}

void TestAnnotationEditor::selectMoveResizeRotateDelete()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.editor().setFilled(true);
    f.drag(QPoint(150, 150), QPoint(249, 249));
    const int id = f.editor().document().at(0)->id();
    // 单击选中（ANN-07）
    f.click(QPoint(200, 200));
    QCOMPARE(f.editor().selectedId(), id);
    // 内部拖动移动
    f.drag(QPoint(200, 200), QPoint(230, 210));
    QCOMPARE(f.editor().document().find(id)->bounds().topLeft(), QPointF(80, 60));
    // 右下角锚点缩放
    const QRectF b = f.editor().document().find(id)->bounds();
    const QPoint corner = f.sel.topLeft() + b.bottomRight().toPoint();
    f.drag(corner, corner + QPoint(20, 10));
    QCOMPARE(f.editor().document().find(id)->bounds().size(), QSizeF(b.width() + 20, b.height() + 10));
    // 旋转柄
    const QPointF rh = f.editor().rotateHandleImage();
    const QPoint rhPhys = f.sel.topLeft() + rh.toPoint();
    f.session->pressAt(rhPhys, Qt::LeftButton);
    f.session->dragTo(rhPhys + QPoint(60, 60));
    f.session->dragTo(f.sel.topLeft() + f.editor().document().find(id)->bounds().center().toPoint() + QPoint(100, 0)); // 正右方 → 90°
    f.session->releaseAt(f.sel.topLeft() + f.editor().document().find(id)->bounds().center().toPoint() + QPoint(100, 0), Qt::LeftButton);
    QVERIFY(std::abs(f.editor().document().find(id)->rotation() - 90.0) < 0.5);
    QCOMPARE(f.editor().document().undoStack().count(), 4);
    // Del 删除，撤销恢复
    f.key(QStringLiteral("Delete"));
    QCOMPARE(f.editor().document().count(), 0);
    f.key(QStringLiteral("Ctrl+Z"));
    QCOMPARE(f.editor().document().count(), 1);
    // 关闭二次编辑后点击不再选中
    f.settings.setValue(QStringLiteral("annotation.secondEdit"), false);
    f.editor().select(-1);
    f.session->setTool(Tool::None);
    f.click(QPoint(200, 200));
    QCOMPARE(f.editor().selectedId(), -1);
}

void TestAnnotationEditor::polylineClicksAndArrowToggle()
{
    Fixture f;
    f.startWith(Tool::Line);
    f.click(QPoint(150, 150)); // 单击开始折线
    QVERIFY(f.editor().hasDraft());
    f.session->dragTo(QPoint(250, 150));
    f.click(QPoint(250, 150));
    f.session->dragTo(QPoint(250, 250));
    f.session->doubleClickAt(QPoint(250, 250)); // 双击结束
    QCOMPARE(f.editor().document().count(), 1);
    const auto* line = static_cast<const LineAnnotation*>(f.editor().document().at(0));
    QCOMPARE(line->points().size(), 3);
    QVERIFY(!line->arrowEnd());
    // Tab 切换到箭头后画的线带箭头（ANN-23）
    f.key(QStringLiteral("Tab"));
    QCOMPARE(f.editor().tool(), Tool::Arrow);
    f.drag(QPoint(300, 300), QPoint(400, 350));
    QVERIFY(static_cast<const LineAnnotation*>(f.editor().document().at(1))->arrowEnd());
    // 右键结束折线
    f.key(QStringLiteral("Tab"));
    f.click(QPoint(150, 300));
    f.session->dragTo(QPoint(200, 300));
    f.session->pressAt(QPoint(200, 300), Qt::RightButton);
    QCOMPARE(f.editor().document().count(), 3);
    QVERIFY(!f.editor().hasDraft());
}

void TestAnnotationEditor::pencilStraightLineWithShift()
{
    Fixture f;
    f.settings.setValue(QStringLiteral("annotation.pencilAngle"), QStringLiteral("45"));
    f.startWith(Tool::Pencil);
    f.editor().setShiftHeld(true);
    f.session->pressAt(QPoint(150, 150), Qt::LeftButton);
    f.session->dragTo(QPoint(160, 155));
    f.session->dragTo(QPoint(250, 160)); // 吸附到水平
    f.session->releaseAt(QPoint(250, 160), Qt::LeftButton);
    const auto* pencil = static_cast<const PencilAnnotation*>(f.editor().document().at(0));
    QCOMPARE(pencil->points().size(), 2);
    QVERIFY(std::abs(pencil->points().last().y() - 50) < 0.01);
    f.editor().setShiftHeld(false);
    f.drag(QPoint(150, 200), QPoint(260, 260));
    QVERIFY(static_cast<const PencilAnnotation*>(f.editor().document().at(1))->points().size() >= 3);
}

void TestAnnotationEditor::textInputCommitAndReedit()
{
    Fixture f;
    f.startWith(Tool::Text);
    f.click(QPoint(150, 150));
    QVERIFY(f.editor().textEditing());
    QPlainTextEdit* edit = nullptr;
    for (auto* w : f.session->overlays())
        if (auto* e = w->findChild<QPlainTextEdit*>())
            edit = e;
    QVERIFY(edit);
    edit->setPlainText(QStringLiteral("你好 Hello"));
    f.editor().commitText();
    QVERIFY(!f.editor().textEditing());
    QCOMPARE(f.editor().document().count(), 1);
    const auto* text = static_cast<const TextAnnotation*>(f.editor().document().at(0));
    QCOMPARE(text->text(), QStringLiteral("你好 Hello"));
    QCOMPARE(text->position(), QPointF(50, 50));
    // 双击重新编辑；Esc 取消不改内容
    f.session->doubleClickAt(QPoint(155, 155));
    QVERIFY(f.editor().textEditing());
    f.editor().cancelText();
    QCOMPARE(static_cast<const TextAnnotation*>(f.editor().document().at(0))->text(), QStringLiteral("你好 Hello"));
    // 空文字不创建
    f.click(QPoint(300, 300));
    f.editor().commitText();
    QCOMPARE(f.editor().document().count(), 1);
    // 字体改变作用于选中的文字
    f.editor().select(text->id());
    QFont bigger = f.editor().textFont();
    bigger.setPixelSize(40);
    f.editor().setTextFont(bigger);
    QCOMPARE(static_cast<const TextAnnotation*>(f.editor().document().at(0))->font().pixelSize(), 40);
}

void TestAnnotationEditor::copyPasteUndoRedoClear()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.drag(QPoint(150, 150), QPoint(200, 200));
    f.click(QPoint(150, 175)); // 点在描边上选中
    QVERIFY(f.editor().selectedId() >= 0);
    f.key(QStringLiteral("Ctrl+Shift+C"));
    f.key(QStringLiteral("Ctrl+Shift+V"));
    QCOMPARE(f.editor().document().count(), 2);
    QCOMPARE(f.editor().document().at(1)->bounds().topLeft(), QPointF(60, 60));
    f.key(QStringLiteral("Ctrl+Z"));
    QCOMPARE(f.editor().document().count(), 1);
    f.key(QStringLiteral("Ctrl+Y"));
    QCOMPARE(f.editor().document().count(), 2);
    f.editor().clearAll();
    QCOMPARE(f.editor().document().count(), 0);
    f.key(QStringLiteral("Ctrl+Z"));
    QCOMPARE(f.editor().document().count(), 2);
}

void TestAnnotationEditor::widthKeysToolShortcutsAndColor()
{
    Fixture f;
    f.startWith(Tool::Rect);
    const int w0 = f.editor().style().width;
    f.key(QStringLiteral("]"));
    QCOMPARE(f.editor().style().width, w0 + 1);
    f.key(QStringLiteral("["));
    f.key(QStringLiteral("1"));
    QCOMPARE(f.editor().style().width, w0 - 1);
    f.key(QStringLiteral("2"));
    QCOMPARE(f.editor().style().width, w0);
    f.key(QStringLiteral("T"));
    QCOMPARE(f.editor().tool(), Tool::Text);
    f.key(QStringLiteral("B"));
    QCOMPARE(f.editor().tool(), Tool::Pencil);
    // 各工具记住自己的颜色（ANN-05）
    f.settings.setValue(QStringLiteral("annotation.rememberColorPerTool"), true);
    f.editor().setColor(Qt::green);
    f.session->setTool(Tool::Rect);
    f.editor().setColor(Qt::blue);
    f.session->setTool(Tool::Pencil);
    QCOMPARE(f.editor().style().stroke, QColor(Qt::green));
    f.session->setTool(Tool::Rect);
    QCOMPARE(f.editor().style().stroke, QColor(Qt::blue));
    QCOMPARE(f.settings.stringValue(QStringLiteral("annotation.color")), QColor(Qt::blue).name(QColor::HexArgb));
}

void TestAnnotationEditor::mosaicBlurEraserHighlighter()
{
    Fixture f;
    f.startWith(Tool::Mosaic);
    f.editor().setEffectShape(EffectAnnotation::Shape::Rect);
    f.drag(QPoint(150, 150), QPoint(249, 249));
    f.session->setTool(Tool::Blur);
    f.editor().setEffectShape(EffectAnnotation::Shape::Brush);
    f.drag(QPoint(300, 150), QPoint(400, 160));
    f.session->setTool(Tool::Highlighter);
    f.editor().setHighlighterShape(HighlighterAnnotation::Shape::Rect);
    f.drag(QPoint(150, 300), QPoint(300, 340));
    f.session->setTool(Tool::Eraser);
    f.editor().setEraserShape(EraserAnnotation::Shape::Rect);
    f.drag(QPoint(160, 160), QPoint(200, 200));
    QCOMPARE(f.editor().document().count(), 4);
    const QImage out = f.session->currentImage();
    const QImage base = FakeScreenCapture::patternImage(0, QSize(3840, 2160)).copy(f.sel);
    QCOMPARE(out.pixel(80, 80), base.pixel(80, 80));           // 被擦掉的马赛克露出底图
    QVERIFY(out.pixel(120, 120) != base.pixel(120, 120));      // 马赛克仍在
    QVERIFY(out.pixel(100, 220) != base.pixel(100, 220));      // 荧光笔
}

void TestAnnotationEditor::exportMatchesOverlayRender()
{
    // NFR-08：覆盖窗口显示的像素与导出图一致（在 100% 屏上逐像素；避开选区边框）
    Fixture f;
    f.sel = QRect(4000, 100, 300, 200);
    f.startWith(Tool::Rect);
    f.editor().setColor(QColor(0, 200, 120));
    f.drag(QPoint(4050, 150), QPoint(4200, 250));
    f.session->setTool(Tool::Pencil);
    f.drag(QPoint(4100, 120), QPoint(4250, 280));
    f.session->setTool(Tool::Text);
    f.click(QPoint(4120, 200));
    for (auto* w : f.session->overlays())
        if (auto* e = w->findChild<QPlainTextEdit*>())
            e->setPlainText(QStringLiteral("NFR-08"));
    f.editor().commitText();
    f.editor().select(-1);
    OverlayWindow* w = f.session->overlayAt(f.sel.topLeft());
    const QImage screen = w->renderToImage().convertToFormat(QImage::Format_ARGB32);
    const QImage exported = f.session->currentImage().convertToFormat(QImage::Format_ARGB32);
    const QRect inner(2, 2, f.sel.width() - 4, f.sel.height() - 4);
    const QImage shown = screen.copy(QRect(f.sel.topLeft() - w->screenInfo().physicalGeometry.topLeft() + inner.topLeft(), inner.size()));
    SS_VERIFY_IMAGE(shown, exported.copy(inner), CompareOptions{}, "annotation_display_vs_export");
}

void TestAnnotationEditor::rightClickLeavesAnnotatingKeepsResult()
{
    Fixture f;
    f.startWith(Tool::Rect);
    f.editor().setFilled(true);
    f.drag(QPoint(150, 150), QPoint(249, 249));
    f.session->pressAt(QPoint(400, 350), Qt::RightButton); // 没有草稿和选中：结束标注
    QCOMPARE(f.session->state(), CaptureSession::State::Selected);
    QCOMPARE(f.session->currentImage().pixel(100, 100), QColor(f.editor().style().stroke).rgb());
    QSignalSpy captured(f.session.get(), &CaptureSession::captured);
    f.session->accept();
    QCOMPARE(captured.count(), 1);
    QCOMPARE(QApplication::clipboard()->image().pixel(100, 100), QColor(f.editor().style().stroke).rgb());
}

void TestAnnotationEditor::magnifierAndColorWhileAnnotating()
{
    Fixture f;
    f.startWith(Tool::Rect);
    QVERIFY(!f.session->magnifierVisible());
    f.session->setAltHeld(true);
    QVERIFY(f.session->magnifierVisible());
    f.session->hoverAt(QPoint(120, 130));
    QSignalSpy copied(f.session.get(), &CaptureSession::colorCopied);
    f.key(QStringLiteral("C"));
    QCOMPARE(copied.count(), 1);
    QCOMPARE(QApplication::clipboard()->text(), QColor(FakeScreenCapture::patternPixel(0, 120, 130)).name().toUpper());
}

void TestAnnotationEditor::toolbarButtonsDriveEditor()
{
    Fixture f;
    QVERIFY(f.session->start(1, false));
    f.session->setSelection(f.sel);
    CaptureToolbar* tb = f.session->toolbar();
    QVERIFY(tb);
    for (const char* name : {"rect", "ellipse", "line", "arrow", "pencil", "highlighter", "mosaic", "blur", "text", "eraser", "undo", "redo", "clear", "copy", "save", "pin", "cancel"})
        QVERIFY2(tb->button(QString::fromLatin1(name)), name);
    tb->button(QStringLiteral("rect"))->click();
    QCOMPARE(f.session->state(), CaptureSession::State::Annotating);
    QCOMPARE(f.editor().tool(), Tool::Rect);
    QVERIFY(tb->optionsRow()->isVisibleTo(tb));
    tb->button(QStringLiteral("fill"))->click();
    QVERIFY(f.editor().style().filled);
    f.drag(QPoint(150, 150), QPoint(200, 200));
    tb->button(QStringLiteral("undo"))->click();
    QCOMPARE(f.editor().document().count(), 0);
    tb->button(QStringLiteral("redo"))->click();
    QCOMPARE(f.editor().document().count(), 1);
    tb->button(QStringLiteral("rect"))->click(); // 再点一次取消工具
    QCOMPARE(f.editor().tool(), Tool::None);
}

QTEST_MAIN(TestAnnotationEditor)
#include "tst_annotationeditor.moc"
