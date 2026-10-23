#pragma once

#include "core/annotation/AnnotationDocument.h"
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
    enum class Tool { None, Rect, Ellipse, Line, Arrow, Pencil, Highlighter, Mosaic, Blur, Text, Eraser };
    Q_ENUM(Tool)
    enum class DragMode { None, Create, Move, Resize, Rotate, Pending };

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

    // ---- 编辑 ----
    int selectedId() const { return m_selectedId; }
    void select(int id);
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

signals:
    void changed();
    void toolChanged(ss::AnnotationEditor::Tool tool);
    void selectionChanged(int id);

private:
    std::unique_ptr<Annotation> makeDraft(QPointF start);
    void updateDraft(QPointF p);
    void commitDraft();
    void applyStyleToSelected();
    QPointF constrain(QPointF start, QPointF p) const;    // Shift 约束
    double snapAngle(double degrees) const;
    void startTextInput(QPointF p, TextAnnotation* existing);
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
