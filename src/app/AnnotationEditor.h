#pragma once

#include "core/annotation/AnnotationDocument.h"
#include "core/annotation/Extras.h"
#include "core/annotation/Overlays.h"
#include "core/annotation/Shapes.h"
#include "core/capture/SelectionGeometry.h"

#include <QFont>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <memory>

class QPlainTextEdit;
class QWidget;

namespace ss {

class Settings;

// 标注编辑器（M2）：在截图选区上创建、二次编辑标注。坐标为图像像素（选区左上角为原点），
// 由会话把物理坐标减去选区原点后传入。渲染只经文档的 render()，显示与导出一致（NFR-08）。
class AnnotationEditor : public QObject {
    Q_OBJECT
public:
    enum class Tool { None, Rect, Ellipse, Line, Arrow, Pencil, Highlighter, Mosaic, Blur, Text, Eraser, Step, Magnifier, Spotlight, SmartErase };
    Q_ENUM(Tool)
    enum class DragMode { None, Create, Move, Resize, Rotate, Pending, Radius, Marquee, ArcStart, ArcEnd, Pointer };

    explicit AnnotationEditor(Settings& settings, QObject* parent = nullptr);
    ~AnnotationEditor() override;

    // ---- 生命周期 ----
    void begin(const QImage& base, const QRect& physicalRegion);
    void end();
    bool active() const { return m_active; }
    QRect region() const { return m_region; }
    AnnotationDocument& document() { return m_doc; }
    const AnnotationDocument& document() const { return m_doc; }

    // ---- 工具与样式 ----
    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    AnnotationStyle style() const { return m_style; }
    void setColor(const QColor& c);
    void setWidth(int w);
    void adjustWidth(int delta);
    void setFilled(bool filled);
    void setHighlighterShape(HighlighterAnnotation::Shape s) { m_highlighterShape = s; }
    HighlighterAnnotation::Shape highlighterShape() const { return m_highlighterShape; }
    void setEffectShape(EffectAnnotation::Shape s) { m_effectShape = s; }
    EffectAnnotation::Shape effectShape() const { return m_effectShape; }
    void setEffectStrength(int s) { m_effectStrength = std::clamp(s, 1, 100); }
    int effectStrength() const { return m_effectStrength; }
    void setEraserShape(EraserAnnotation::Shape s) { m_eraserShape = s; }
    EraserAnnotation::Shape eraserShape() const { return m_eraserShape; }
    void setTextFont(const QFont& f);
    QFont textFont() const { return m_textFont; }
    void toggleArrow(); // Tab：线条 ↔ 箭头
    void setShiftHeld(bool held) { m_shift = held; }
    // 线型（ANN-21）：实线 / 虚线 / 点线，作用于新建与选中项
    void setPenStyle(Qt::PenStyle s);
    // 圆角矩形（ANN-21）：新建矩形与选中矩形的圆角半径
    void setCornerRadius(double r);
    double cornerRadius() const { return m_cornerRadius; }
    // 箭头样式（ANN-25）：起点 / 终点分别设置
    void setArrowHeads(LineAnnotation::Head start, LineAnnotation::Head end);
    LineAnnotation::Head arrowHeadStart() const { return m_headStart; }
    LineAnnotation::Head arrowHeadEnd() const { return m_headEnd; }
    // 文字装饰（ANN-33）
    void setTextDecor(const TextDecor& d);
    TextDecor textDecor() const { return m_textDecor; }
    // 序号（ANN-35）
    void setStepStyle(StepAnnotation::NumberStyle s);
    StepAnnotation::NumberStyle stepStyle() const { return m_stepStyle; }
    void setStepNumber(int n);     // 手动改选中序号的值
    int nextStepNumber() const;    // 下一个自动编号
    // 放大（ANN-37）
    void setMagnifierZoom(double z) { m_magnifierZoom = std::clamp(z, 1.0, 8.0); applyMagnifierToSelected(); }
    double magnifierZoom() const { return m_magnifierZoom; }
    void setMagnifierShape(MagnifierAnnotation::Shape s) { m_magnifierShape = s; applyMagnifierToSelected(); }
    MagnifierAnnotation::Shape magnifierShape() const { return m_magnifierShape; }
    void setMagnifierIncludeAnnotations(bool on) { m_magnifierInclude = on; applyMagnifierToSelected(); }
    bool magnifierIncludeAnnotations() const { return m_magnifierInclude; }
    void setMagnifierShadow(bool on) { m_magnifierShadow = on; applyMagnifierToSelected(); }
    bool magnifierShadow() const { return m_magnifierShadow; }
    // 用给定矩形（图像坐标）直接创建当前工具的形状（ANN-11 右键贴合元素）；工具不支持时返回 false
    bool createFromRect(const QRectF& r);

    // ---- 鼠标（图像坐标）----
    void pressAt(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods);
    void moveTo(QPointF p);
    void releaseAt(QPointF p, Qt::MouseButton button);
    void doubleClickAt(QPointF p);
    DragMode dragMode() const { return m_drag; }
    bool hasDraft() const { return m_draft != nullptr; }
    bool textEditing() const { return m_textEdit != nullptr; }
    SelectionHandle handleAt(QPointF p) const; // 选中项上的锚点
    bool rotateHandleAt(QPointF p) const;
    bool radiusHandleAt(QPointF p) const;      // 圆角矩形的圆角控制点
    // 端点 / 连接样式（ANN-24）
    void setCapStyle(Qt::PenCapStyle s);
    void setJoinStyle(Qt::PenJoinStyle s);
    // 扇形 / 弧形（ANN-22）：选中椭圆的控制点；setArc 直接设置
    QPointF arcHandleImage(bool end) const;
    bool arcHandleAt(QPointF p, bool& end) const;
    void setArc(double startDegrees, double spanDegrees);
    // 文字指示箭头（ANN-34）：选中文字下方的控制点，拖出指向目标
    QPointF pointerHandleImage() const;
    bool pointerHandleAt(QPointF p) const;
    void setTextPointer(std::optional<QPointF> target);
    // 聚光灯（ANN-38）
    void setSpotlightShape(SpotlightAnnotation::Shape s) { m_spotlightShape = s; }
    SpotlightAnnotation::Shape spotlightShape() const { return m_spotlightShape; }
    // 水印（ANN-39）：按设置添加 / 移除一个水印
    int addWatermark();
    void removeWatermark();
    bool hasWatermark() const;
    // 自动马赛克（ANN-31）：按设置开关；手动覆盖
    void setAutoMosaic(bool on) { m_autoMosaic = on; }
    bool autoMosaic() const { return m_autoMosaic; }

    // ---- 编辑 ----
    int selectedId() const { return m_selectedId; }
    void select(int id);
    // 多选（ANN-08）：框选得到；一起移动 / 删除
    QList<int> selectedIds() const;
    void selectMany(const QList<int>& ids);
    QRectF marqueeRect() const { return m_marquee; }
    void deleteSelected();
    void copySelected();
    bool pasteCopied();
    void undo();
    void redo();
    void clearAll();
    bool finishDraft();   // 结束折线 / 文字输入；返回是否有草稿被结束
    void cancelDraft();

    // ---- 文字输入（ANN-32）----
    // host：承载输入框的控件；hostTopLeft：图像原点在 host 上的逻辑坐标；scale：物理 / 逻辑
    void setTextHost(QWidget* host, QPointF hostTopLeft, qreal scale);
    void commitText();
    void cancelText();

    // ---- 渲染 ----
    QImage renderForExport() const;                       // 文档 + 草稿
    void paint(QPainter& p, QPointF topLeft, qreal scale) const; // 画到覆盖窗口（逻辑坐标）
    // 选中项的锚点（图像坐标）
    QList<QPointF> selectionHandlesImage() const;
    QPointF rotateHandleImage() const;
    QPointF radiusHandleImage() const; // 不适用时返回极远点

signals:
    void changed();
    void toolChanged(ss::AnnotationEditor::Tool tool);
    void selectionChanged(int id);
    void styleChanged(); // 样式（颜色 / 笔宽 / 线型 / 端点 / 连接）经接口改变

private:
    std::unique_ptr<Annotation> makeDraft(QPointF start);
    void updateDraft(QPointF p);
    void commitDraft();
    void applyStyleToSelected();
    QPointF constrain(QPointF start, QPointF p) const;    // Shift 约束
    double snapAngle(double degrees) const;
    void startTextInput(QPointF p, Annotation* existing); // 文字或序号说明
    void applyMagnifierToSelected();
    void renumberStepsAfterRemoval(int removedNumber);
    void addAutoMosaics(const EffectAnnotation& original);
    void removeGroup(int groupId);
    void positionTextEdit();
    QRectF selectedBounds() const;

    Settings& m_settings;
    AnnotationDocument m_doc;
    QRect m_region;
    bool m_active = false;
    Tool m_tool = Tool::None;
    AnnotationStyle m_style;
    QHash<int, QColor> m_toolColors; // ANN-05：各工具记住自己的颜色
    HighlighterAnnotation::Shape m_highlighterShape = HighlighterAnnotation::Shape::Brush;
    EffectAnnotation::Shape m_effectShape = EffectAnnotation::Shape::Rect;
    int m_effectStrength = 30;
    EraserAnnotation::Shape m_eraserShape = EraserAnnotation::Shape::Brush;
    QFont m_textFont;
    bool m_shift = false;
    double m_cornerRadius = 0;
    LineAnnotation::Head m_headStart = LineAnnotation::Head::None;
    LineAnnotation::Head m_headEnd = LineAnnotation::Head::Triangle;
    TextDecor m_textDecor;
    StepAnnotation::NumberStyle m_stepStyle = StepAnnotation::NumberStyle::Numeric;
    double m_magnifierZoom = 2.0;
    SpotlightAnnotation::Shape m_spotlightShape = SpotlightAnnotation::Shape::Rect;
    bool m_autoMosaic = false;
    QList<int> m_multi;      // 多选里除主选中外的其他项
    QRectF m_marquee;        // 框选中的矩形
    int m_nextGroupId = 1;
    MagnifierAnnotation::Shape m_magnifierShape = MagnifierAnnotation::Shape::Rect;
    bool m_magnifierInclude = false;
    bool m_magnifierShadow = true;
    // 草稿与拖拽
    std::unique_ptr<Annotation> m_draft;
    QPointF m_dragStart;
    QPointF m_lastPoint;
    DragMode m_drag = DragMode::None;
    SelectionHandle m_dragHandle = SelectionHandle::None;
    std::unique_ptr<Annotation> m_beforeEdit; // 二次编辑前的副本
    QRectF m_editOrigin;
    bool m_polylineActive = false;
    int m_selectedId = -1;
    std::unique_ptr<Annotation> m_copied;
    // 文字输入
    QPointer<QPlainTextEdit> m_textEdit;
    QWidget* m_textHost = nullptr;
    QPointF m_hostTopLeft;
    qreal m_hostScale = 1.0;
    QPointF m_textPos;
    int m_textEditingId = -1;
};

} // namespace ss
