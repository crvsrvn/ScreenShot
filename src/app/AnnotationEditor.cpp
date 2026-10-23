#include "app/AnnotationEditor.h"

#include "core/config/Settings.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <cmath>

namespace ss {

namespace {
constexpr double kHitTolerance = 4.0;
constexpr double kHandleGrip = 6.0;
constexpr double kRotateOffset = 24.0;
constexpr int kClickTolerance = 3;

// 文字输入框：Esc 取消、右键 / Ctrl+Enter 提交
class InlineTextEdit : public QPlainTextEdit {
public:
    explicit InlineTextEdit(AnnotationEditor* editor, QWidget* parent)
        : QPlainTextEdit(parent)
        , m_editor(editor)
    {
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setAttribute(Qt::WA_InputMethodEnabled, true);
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) {
            m_editor->cancelText();
            return;
        }
        if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ControlModifier)) {
            m_editor->commitText();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::RightButton) {
            m_editor->commitText();
            return;
        }
        QPlainTextEdit::mousePressEvent(e);
    }

private:
    AnnotationEditor* m_editor;
};

} // namespace

AnnotationEditor::AnnotationEditor(Settings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_textFont = QFont(QStringLiteral("Microsoft YaHei"));
    m_textFont.setPixelSize(22);
    connect(&m_doc, &AnnotationDocument::changed, this, &AnnotationEditor::changed);
}

AnnotationEditor::~AnnotationEditor()
{
    cancelText();
}

void AnnotationEditor::begin(const QImage& base, const QRect& physicalRegion)
{
    m_doc.setBaseImage(base);
    m_doc.undoStack().clear();
    m_doc.fromJson({});
    m_doc.setBaseImage(base);
    m_region = physicalRegion;
    m_active = true;
    m_draft.reset();
    m_drag = DragMode::None;
    m_selectedId = -1;
    m_polylineActive = false;
    m_style.width = m_settings.intValue(QStringLiteral("annotation.width"));
    m_style.stroke = QColor::fromString(m_settings.stringValue(QStringLiteral("annotation.color")));
    m_style.fill = m_style.stroke;
    emit changed();
}

void AnnotationEditor::end()
{
    cancelText();
    m_draft.reset();
    m_active = false;
    m_tool = Tool::None;
    m_selectedId = -1;
    m_doc.fromJson({});
    m_doc.setBaseImage(QImage());
}

// ---- 工具与样式 ----

void AnnotationEditor::setTool(Tool tool)
{
    if (m_tool == tool)
        return;
    finishDraft();
    if (m_settings.boolValue(QStringLiteral("annotation.rememberColorPerTool"))) {
        m_toolColors[static_cast<int>(m_tool)] = m_style.stroke;
        if (m_toolColors.contains(static_cast<int>(tool))) {
            m_style.stroke = m_toolColors.value(static_cast<int>(tool));
            m_style.fill = m_style.stroke;
        }
    }
    m_tool = tool;
    emit toolChanged(tool);
    emit changed();
}

void AnnotationEditor::setColor(const QColor& c)
{
    m_style.stroke = c;
    m_style.fill = c;
    m_settings.setValue(QStringLiteral("annotation.color"), c.name(QColor::HexArgb));
    applyStyleToSelected();
}

void AnnotationEditor::setWidth(int w)
{
    m_style.width = std::clamp(w, 1, 100);
    m_settings.setValue(QStringLiteral("annotation.width"), m_style.width);
    applyStyleToSelected();
}

void AnnotationEditor::adjustWidth(int delta)
{
    setWidth(m_style.width + delta);
}

void AnnotationEditor::setFilled(bool filled)
{
    m_style.filled = filled;
    applyStyleToSelected();
}

void AnnotationEditor::setTextFont(const QFont& f)
{
    m_textFont = f;
    if (m_textEdit)
        m_textEdit->setFont(f);
    if (Annotation* a = m_doc.find(m_selectedId); a && a->type() == QLatin1String("text")) {
        auto before = a->clone();
        static_cast<TextAnnotation*>(a)->setFont(f);
        m_doc.commitModification(m_selectedId, std::move(before));
    }
}

void AnnotationEditor::toggleArrow()
{
    if (m_tool == Tool::Line)
        setTool(Tool::Arrow);
    else if (m_tool == Tool::Arrow)
        setTool(Tool::Line);
    if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get()))
        line->setArrow(false, m_tool == Tool::Arrow);
}

void AnnotationEditor::applyStyleToSelected()
{
    Annotation* a = m_doc.find(m_selectedId);
    if (!a)
        return;
    auto before = a->clone();
    AnnotationStyle s = a->style();
    s.stroke = m_style.stroke;
    s.fill = m_style.fill;
    s.width = m_style.width;
    if (a->usesFill())
        s.filled = m_style.filled;
    a->setStyle(s);
    m_doc.commitModification(m_selectedId, std::move(before));
}

// ---- Shift 约束（ANN-09）----

double AnnotationEditor::snapAngle(double degrees) const
{
    const QString mode = m_settings.stringValue(QStringLiteral("annotation.pencilAngle"));
    const int step = mode == QLatin1String("free") ? 0 : mode.toInt();
    if (step <= 0)
        return degrees;
    return std::round(degrees / step) * step;
}

QPointF AnnotationEditor::constrain(QPointF start, QPointF p) const
{
    if (!m_shift)
        return p;
    switch (m_tool) {
    case Tool::Rect:
    case Tool::Ellipse:
    case Tool::Mosaic:
    case Tool::Blur:
    case Tool::Eraser: {
        // 正方形 / 正圆
        const double side = std::max(std::abs(p.x() - start.x()), std::abs(p.y() - start.y()));
        return QPointF(start.x() + (p.x() >= start.x() ? side : -side), start.y() + (p.y() >= start.y() ? side : -side));
    }
    case Tool::Line:
    case Tool::Arrow: {
        const double angle = std::atan2(p.y() - start.y(), p.x() - start.x()) * 180.0 / M_PI;
        const double snapped = std::round(angle / 45.0) * 45.0 * M_PI / 180.0;
        const double len = std::hypot(p.x() - start.x(), p.y() - start.y());
        return start + QPointF(std::cos(snapped) * len, std::sin(snapped) * len);
    }
    case Tool::Pencil: {
        const double angle = std::atan2(p.y() - start.y(), p.x() - start.x()) * 180.0 / M_PI;
        const double snapped = snapAngle(angle) * M_PI / 180.0;
        const double len = std::hypot(p.x() - start.x(), p.y() - start.y());
        return start + QPointF(std::cos(snapped) * len, std::sin(snapped) * len);
    }
    default:
        return p;
    }
}

// ---- 草稿 ----

std::unique_ptr<Annotation> AnnotationEditor::makeDraft(QPointF start)
{
    std::unique_ptr<Annotation> a;
    switch (m_tool) {
    case Tool::Rect: a = std::make_unique<RectAnnotation>(); break;
    case Tool::Ellipse: a = std::make_unique<EllipseAnnotation>(); break;
    case Tool::Line:
    case Tool::Arrow: {
        auto l = std::make_unique<LineAnnotation>();
        l->points() = {start, start};
        l->setArrow(false, m_tool == Tool::Arrow);
        a = std::move(l);
        break;
    }
    case Tool::Pencil: {
        auto p = std::make_unique<PencilAnnotation>();
        p->points() = {start};
        p->setSmooth(m_settings.boolValue(QStringLiteral("annotation.pencilSmooth")));
        a = std::move(p);
        break;
    }
    case Tool::Highlighter: {
        auto h = std::make_unique<HighlighterAnnotation>();
        h->setShape(m_highlighterShape);
        h->setBlend(m_settings.stringValue(QStringLiteral("annotation.highlighterBlend")) == QLatin1String("translucent") ? HighlighterAnnotation::Blend::Translucent
                                                                                                                              : HighlighterAnnotation::Blend::Multiply);
        h->points() = {start};
        h->setRect(QRectF(start, start));
        a = std::move(h);
        break;
    }
    case Tool::Mosaic:
    case Tool::Blur: {
        auto e = std::make_unique<EffectAnnotation>();
        e->setEffect(m_tool == Tool::Mosaic ? EffectAnnotation::Effect::Mosaic : EffectAnnotation::Effect::Blur);
        e->setShape(m_effectShape);
        e->setStrength(m_effectStrength);
        e->points() = {start};
        e->setRect(QRectF(start, start));
        a = std::move(e);
        break;
    }
    case Tool::Eraser: {
        auto e = std::make_unique<EraserAnnotation>();
        e->setShape(m_eraserShape);
        e->points() = {start};
        e->setRect(QRectF(start, start));
        a = std::move(e);
        break;
    }
    case Tool::Text:
    case Tool::None:
        return nullptr;
    }
    AnnotationStyle s = m_style;
    if (m_tool == Tool::Highlighter) {
        s.width = std::max(m_style.width, 12);
        if (s.stroke.alpha() == 255)
            s.stroke.setAlpha(m_settings.intValue(QStringLiteral("annotation.highlighterAlpha")));
    }
    if (m_tool == Tool::Mosaic || m_tool == Tool::Blur || m_tool == Tool::Eraser)
        s.width = std::max(m_style.width * 4, 16); // 画笔形状的刷子宽度
    if (!a->usesFill())
        s.filled = false;
    a->setStyle(s);
    if (auto* r = dynamic_cast<RectAnnotation*>(a.get()))
        r->setBounds(QRectF(start, start));
    return a;
}

void AnnotationEditor::updateDraft(QPointF raw)
{
    if (!m_draft)
        return;
    const QPointF p = constrain(m_dragStart, raw);
    m_lastPoint = p;
    if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get())) {
        line->points().last() = p;
    } else if (auto* hl = dynamic_cast<HighlighterAnnotation*>(m_draft.get())) {
        if (hl->shape() == HighlighterAnnotation::Shape::Rect)
            hl->setRect(QRectF(m_dragStart, p));
        else
            hl->points().append(raw);
    } else if (auto* pencil = dynamic_cast<PencilAnnotation*>(m_draft.get())) {
        if (m_shift) {
            pencil->points() = {m_dragStart, p};
        } else {
            pencil->points().append(raw);
        }
    } else if (auto* eff = dynamic_cast<EffectAnnotation*>(m_draft.get())) {
        if (eff->shape() == EffectAnnotation::Shape::Brush)
            eff->points().append(raw);
        else
            eff->setRect(QRectF(m_dragStart, p));
    } else if (auto* er = dynamic_cast<EraserAnnotation*>(m_draft.get())) {
        if (er->shape() == EraserAnnotation::Shape::Brush)
            er->points().append(raw);
        else
            er->setRect(QRectF(m_dragStart, p));
    } else if (auto* rect = dynamic_cast<RectAnnotation*>(m_draft.get())) {
        rect->setBounds(QRectF(m_dragStart, p));
    }
    emit changed();
}

void AnnotationEditor::commitDraft()
{
    if (!m_draft)
        return;
    // 过小的形状视为误触
    const QRectF b = m_draft->bounds();
    const bool pointish = dynamic_cast<LineAnnotation*>(m_draft.get()) == nullptr && b.width() < 2 && b.height() < 2
        && m_draft->type() != QLatin1String("pencil") && m_draft->type() != QLatin1String("highlighter") && m_draft->type() != QLatin1String("effect")
        && m_draft->type() != QLatin1String("eraser");
    if (pointish) {
        m_draft.reset();
        emit changed();
        return;
    }
    const int id = m_doc.add(std::move(m_draft));
    m_draft.reset();
    if (m_settings.boolValue(QStringLiteral("annotation.selectAfterCreate")))
        select(id);
    emit changed();
}

bool AnnotationEditor::finishDraft()
{
    if (m_textEdit) {
        commitText();
        return true;
    }
    if (!m_draft)
        return false;
    if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get()); line && m_polylineActive) {
        // 折线：去掉跟随鼠标的末点
        if (line->points().size() > 2)
            line->points().removeLast();
        m_polylineActive = false;
    }
    commitDraft();
    return true;
}

void AnnotationEditor::cancelDraft()
{
    m_draft.reset();
    m_polylineActive = false;
    m_drag = DragMode::None;
    emit changed();
}

// ---- 鼠标 ----

QRectF AnnotationEditor::selectedBounds() const
{
    const Annotation* a = m_doc.find(m_selectedId);
    return a ? a->bounds() : QRectF();
}

QList<QPointF> AnnotationEditor::selectionHandlesImage() const
{
    const Annotation* a = m_doc.find(m_selectedId);
    if (!a)
        return {};
    const QRectF b = a->bounds();
    const QList<QPointF> raw{b.topLeft(), QPointF(b.center().x(), b.top()), b.topRight(), QPointF(b.right(), b.center().y()),
                             b.bottomRight(), QPointF(b.center().x(), b.bottom()), b.bottomLeft(), QPointF(b.left(), b.center().y())};
    QTransform t;
    t.translate(b.center().x(), b.center().y());
    t.rotate(a->rotation());
    t.translate(-b.center().x(), -b.center().y());
    QList<QPointF> out;
    for (const auto& p : raw)
        out.append(t.map(p));
    return out;
}

QPointF AnnotationEditor::rotateHandleImage() const
{
    const Annotation* a = m_doc.find(m_selectedId);
    if (!a || !a->rotatable())
        return QPointF(-1e9, -1e9);
    const QRectF b = a->bounds();
    QTransform t;
    t.translate(b.center().x(), b.center().y());
    t.rotate(a->rotation());
    t.translate(-b.center().x(), -b.center().y());
    return t.map(QPointF(b.center().x(), b.top() - kRotateOffset));
}

SelectionHandle AnnotationEditor::handleAt(QPointF p) const
{
    const Annotation* a = m_doc.find(m_selectedId);
    if (!a)
        return SelectionHandle::Outside;
    static const SelectionHandle handles[] = {SelectionHandle::TopLeft, SelectionHandle::Top, SelectionHandle::TopRight, SelectionHandle::Right,
                                              SelectionHandle::BottomRight, SelectionHandle::Bottom, SelectionHandle::BottomLeft, SelectionHandle::Left};
    const QList<QPointF> pts = selectionHandlesImage();
    for (int i = 0; i < pts.size(); ++i) {
        if (std::abs(pts[i].x() - p.x()) <= kHandleGrip && std::abs(pts[i].y() - p.y()) <= kHandleGrip)
            return handles[i];
    }
    return a->hitTest(p, kHitTolerance) ? SelectionHandle::Inside : SelectionHandle::Outside;
}

bool AnnotationEditor::rotateHandleAt(QPointF p) const
{
    const QPointF r = rotateHandleImage();
    return std::abs(r.x() - p.x()) <= kHandleGrip && std::abs(r.y() - p.y()) <= kHandleGrip;
}

void AnnotationEditor::pressAt(QPointF p, Qt::MouseButton button, Qt::KeyboardModifiers mods)
{
    if (!m_active)
        return;
    if (m_textEdit) {
        commitText(); // 点击别处：提交文字
    }
    if (button == Qt::RightButton) {
        if (m_polylineActive || m_draft) {
            finishDraft();
        } else if (m_selectedId >= 0) {
            select(-1);
        }
        return;
    }
    if (button != Qt::LeftButton)
        return;
    m_dragStart = p;
    m_lastPoint = p;
    // 折线进行中：再点一下加一个顶点（ANN-23）
    if (m_polylineActive) {
        if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get())) {
            line->points().last() = constrain(line->points()[line->points().size() - 2], p);
            line->points().append(p);
            emit changed();
        }
        return;
    }
    const bool secondEdit = m_settings.boolValue(QStringLiteral("annotation.secondEdit"));
    // 选中项的锚点 / 旋转柄 / 内部（ANN-07）
    if (secondEdit && m_selectedId >= 0) {
        if (rotateHandleAt(p)) {
            m_drag = DragMode::Rotate;
            m_beforeEdit = m_doc.find(m_selectedId)->clone();
            return;
        }
        const SelectionHandle h = handleAt(p);
        if (h == SelectionHandle::Inside) {
            Annotation* a = m_doc.find(m_selectedId);
            if (mods & Qt::ControlModifier) {
                // Ctrl+拖动复制
                auto copy = a->clone();
                const int id = m_doc.add(std::move(copy));
                select(id);
                a = m_doc.find(id);
            }
            m_drag = DragMode::Move;
            m_beforeEdit = a->clone();
            m_editOrigin = a->bounds();
            return;
        }
        if (h != SelectionHandle::Outside) {
            m_drag = DragMode::Resize;
            m_dragHandle = h;
            m_beforeEdit = m_doc.find(m_selectedId)->clone();
            m_editOrigin = m_doc.find(m_selectedId)->bounds();
            return;
        }
    }
    // 点在已有标注上：候选选中（松开时若没拖动则选中）
    if (secondEdit && (m_tool == Tool::None || m_doc.hitTest(p, kHitTolerance))) {
        m_drag = DragMode::Pending;
        return;
    }
    if (m_tool == Tool::Text) {
        startTextInput(p, nullptr);
        return;
    }
    m_draft = makeDraft(p);
    if (m_draft) {
        m_drag = DragMode::Create;
        select(-1);
        emit changed();
    }
}

void AnnotationEditor::moveTo(QPointF p)
{
    if (!m_active)
        return;
    switch (m_drag) {
    case DragMode::Pending:
        if ((p - m_dragStart).manhattanLength() > kClickTolerance) {
            // 在已有标注上拖动：若当前有工具则开始创建，否则移动命中的标注
            Annotation* hit = m_doc.hitTest(m_dragStart, kHitTolerance);
            if (hit && (m_tool == Tool::None || !m_settings.boolValue(QStringLiteral("annotation.toolOverridesDrag")))) {
                select(hit->id());
                m_drag = DragMode::Move;
                m_beforeEdit = hit->clone();
                m_editOrigin = hit->bounds();
                moveTo(p);
            } else if (m_tool == Tool::Text) {
                m_drag = DragMode::None;
            } else {
                m_draft = makeDraft(m_dragStart);
                m_drag = m_draft ? DragMode::Create : DragMode::None;
                if (m_draft)
                    updateDraft(p);
            }
        }
        break;
    case DragMode::Create:
        updateDraft(p);
        break;
    case DragMode::Move: {
        Annotation* a = m_doc.find(m_selectedId);
        if (a) {
            const QPointF delta = p - m_lastPoint;
            a->translate(delta);
            m_lastPoint = p;
            emit changed();
        }
        break;
    }
    case DragMode::Resize: {
        Annotation* a = m_doc.find(m_selectedId);
        if (a) {
            // 把鼠标位移换算到未旋转坐标系
            QTransform t;
            t.translate(m_editOrigin.center().x(), m_editOrigin.center().y());
            t.rotate(-a->rotation());
            t.translate(-m_editOrigin.center().x(), -m_editOrigin.center().y());
            const QPointF local = t.map(p) - t.map(m_dragStart);
            QRectF r = resizeSelection(m_editOrigin.toRect(), m_dragHandle, local.toPoint());
            if (m_shift && a->usesFill()) {
                const double side = std::max(r.width(), r.height());
                r.setSize(QSizeF(side, side));
            }
            a->setBounds(r);
            emit changed();
        }
        break;
    }
    case DragMode::Rotate: {
        Annotation* a = m_doc.find(m_selectedId);
        if (a) {
            const QPointF c = a->bounds().center();
            double angle = std::atan2(p.y() - c.y(), p.x() - c.x()) * 180.0 / M_PI + 90.0;
            if (m_shift)
                angle = 0; // Shift 回到水平（ANN-32）
            a->setRotation(angle);
            emit changed();
        }
        break;
    }
    case DragMode::None:
        if (m_polylineActive) {
            if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get())) {
                line->points().last() = constrain(line->points()[line->points().size() - 2], p);
                emit changed();
            }
        }
        break;
    }
}

void AnnotationEditor::releaseAt(QPointF p, Qt::MouseButton button)
{
    if (!m_active || button != Qt::LeftButton)
        return;
    const DragMode mode = m_drag;
    m_drag = DragMode::None;
    switch (mode) {
    case DragMode::Pending: {
        Annotation* hit = m_doc.hitTest(m_dragStart, kHitTolerance);
        select(hit ? hit->id() : -1);
        if (!hit && m_tool == Tool::Text)
            startTextInput(p, nullptr);
        break;
    }
    case DragMode::Create:
        updateDraft(p);
        if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get())) {
            // 单击（没拖动）开始折线；拖拽则是单段线
            if ((p - m_dragStart).manhattanLength() <= kClickTolerance) {
                m_polylineActive = true;
                line->points().append(p);
                emit changed();
                return;
            }
        }
        commitDraft();
        break;
    case DragMode::Move:
    case DragMode::Resize:
    case DragMode::Rotate:
        if (m_beforeEdit)
            m_doc.commitModification(m_selectedId, std::move(m_beforeEdit));
        break;
    case DragMode::None:
        break;
    }
}

void AnnotationEditor::doubleClickAt(QPointF p)
{
    if (!m_active)
        return;
    if (m_polylineActive) {
        finishDraft();
        return;
    }
    if (Annotation* a = m_doc.hitTest(p, kHitTolerance); a && a->type() == QLatin1String("text")) {
        select(a->id());
        auto* text = static_cast<TextAnnotation*>(a);
        startTextInput(text->position(), text);
    }
}

// ---- 编辑 ----

void AnnotationEditor::select(int id)
{
    if (m_selectedId == id)
        return;
    m_selectedId = m_doc.find(id) ? id : -1;
    if (Annotation* a = m_doc.find(m_selectedId)) {
        m_style.stroke = a->style().stroke;
        m_style.fill = a->style().fill;
        m_style.width = a->style().width;
        m_style.filled = a->style().filled;
    }
    emit selectionChanged(m_selectedId);
    emit changed();
}

void AnnotationEditor::deleteSelected()
{
    if (m_selectedId >= 0) {
        m_doc.remove(m_selectedId);
        select(-1);
    }
}

void AnnotationEditor::copySelected()
{
    if (const Annotation* a = m_doc.find(m_selectedId))
        m_copied = a->clone();
}

bool AnnotationEditor::pasteCopied()
{
    if (!m_copied)
        return false;
    auto copy = m_copied->clone();
    copy->translate(QPointF(10, 10));
    m_copied->translate(QPointF(10, 10));
    select(m_doc.add(std::move(copy)));
    return true;
}

void AnnotationEditor::undo()
{
    finishDraft();
    m_doc.undo();
    if (!m_doc.find(m_selectedId))
        select(-1);
    emit changed();
}

void AnnotationEditor::redo()
{
    m_doc.redo();
    emit changed();
}

void AnnotationEditor::clearAll()
{
    finishDraft();
    select(-1);
    m_doc.clearAll();
}

// ---- 文字 ----

void AnnotationEditor::setTextHost(QWidget* host, QPointF hostTopLeft, qreal scale)
{
    m_textHost = host;
    m_hostTopLeft = hostTopLeft;
    m_hostScale = scale;
    positionTextEdit();
}

void AnnotationEditor::startTextInput(QPointF p, TextAnnotation* existing)
{
    cancelText();
    if (!m_textHost)
        return;
    m_textPos = existing ? existing->position() : p;
    m_textEditingId = existing ? existing->id() : -1;
    auto* edit = new InlineTextEdit(this, m_textHost);
    m_textEdit = edit;
    QFont f = existing ? existing->font() : m_textFont;
    // 输入框按逻辑像素显示：字号 / 缩放
    QFont widgetFont = f;
    widgetFont.setPixelSize(std::max(1, static_cast<int>(std::round(f.pixelSize() / m_hostScale))));
    edit->setFont(widgetFont);
    const QColor color = existing ? existing->style().stroke : m_style.stroke;
    edit->setStyleSheet(QStringLiteral("QPlainTextEdit { background: rgba(255,255,255,200); color: %1; border: 1px dashed #3078dc; }").arg(color.name(QColor::HexArgb)));
    if (existing)
        edit->setPlainText(existing->text());
    connect(edit, &QPlainTextEdit::textChanged, this, [this] { positionTextEdit(); });
    positionTextEdit();
    edit->show();
    edit->setFocus();
    emit changed();
}

void AnnotationEditor::positionTextEdit()
{
    if (!m_textEdit || !m_textHost)
        return;
    const QFontMetrics fm(m_textEdit->font());
    const QStringList lines = m_textEdit->toPlainText().split(QLatin1Char('\n'));
    int w = 60;
    for (const auto& l : lines)
        w = std::max(w, fm.horizontalAdvance(l) + 20);
    const int h = std::max(1, static_cast<int>(lines.size())) * fm.lineSpacing() + 12;
    const QPointF logical = m_hostTopLeft + m_textPos / m_hostScale;
    m_textEdit->setGeometry(QRect(logical.toPoint(), QSize(w, h)));
}

void AnnotationEditor::commitText()
{
    if (!m_textEdit)
        return;
    const QString text = m_textEdit->toPlainText();
    QPlainTextEdit* edit = m_textEdit;
    m_textEdit = nullptr;
    edit->hide();
    edit->deleteLater();
    if (m_textEditingId >= 0) {
        if (Annotation* a = m_doc.find(m_textEditingId)) {
            if (text.trimmed().isEmpty()) {
                m_doc.remove(m_textEditingId);
                select(-1);
            } else {
                auto before = a->clone();
                static_cast<TextAnnotation*>(a)->setText(text);
                m_doc.commitModification(m_textEditingId, std::move(before));
            }
        }
    } else if (!text.trimmed().isEmpty()) {
        auto t = std::make_unique<TextAnnotation>();
        t->setText(text);
        t->setPosition(m_textPos);
        t->setFont(m_textFont);
        AnnotationStyle s = m_style;
        t->setStyle(s);
        const int id = m_doc.add(std::move(t));
        if (m_settings.boolValue(QStringLiteral("annotation.selectAfterCreate")))
            select(id);
    }
    m_textEditingId = -1;
    emit changed();
}

void AnnotationEditor::cancelText()
{
    if (!m_textEdit)
        return;
    QPlainTextEdit* edit = m_textEdit;
    m_textEdit = nullptr;
    edit->hide();
    edit->deleteLater();
    m_textEditingId = -1;
    emit changed();
}

// ---- 渲染 ----

QImage AnnotationEditor::renderForExport() const
{
    QImage out = m_doc.render();
    if (m_draft && !out.isNull()) {
        QPainter p(&out);
        m_doc.paintItem(p, *m_draft, out);
    }
    return out;
}

void AnnotationEditor::paint(QPainter& p, QPointF topLeft, qreal scale) const
{
    if (!m_active)
        return;
    // 拖动 / 缩放选中项时把它从缓存里排除，实时画，其余用缓存（NFR-05）
    const bool editingSelected = m_drag == DragMode::Move || m_drag == DragMode::Resize || m_drag == DragMode::Rotate;
    QImage base = m_doc.cachedRender(editingSelected ? m_selectedId : -1);
    if (base.isNull())
        return;
    if (editingSelected || m_draft) {
        QPainter ip(&base);
        if (editingSelected)
            if (const Annotation* a = m_doc.find(m_selectedId))
                m_doc.paintItem(ip, *a, base);
        if (m_draft)
            m_doc.paintItem(ip, *m_draft, base);
    }
    base.setDevicePixelRatio(scale);
    p.drawImage(topLeft, base);
    // 选中项的锚点与旋转柄
    if (m_selectedId >= 0) {
        p.save();
        QPen pen(QColor(40, 120, 220));
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::white);
        for (const QPointF& h : selectionHandlesImage()) {
            const QPointF l = topLeft + h / scale;
            p.drawRect(QRectF(l.x() - 3.5, l.y() - 3.5, 7, 7));
        }
        const QPointF r = rotateHandleImage();
        if (r.x() > -1e8) {
            const QPointF l = topLeft + r / scale;
            p.drawEllipse(l, 4, 4);
        }
        p.restore();
    }
}

} // namespace ss
