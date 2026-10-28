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
constexpr double kRadiusHandleInset = 12.0; // 圆角控制点离左上角的最小距离
constexpr double kStepTailMin = 10.0;       // 拖出指示箭头的最小距离
constexpr QPointF kFar(-1e9, -1e9);
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
    m_style.penStyle = Qt::SolidLine;
    m_cornerRadius = 0;
    const QString step = m_settings.stringValue(QStringLiteral("annotation.stepStyle"));
    m_stepStyle = step == QLatin1String("alpha") ? StepAnnotation::NumberStyle::Alpha : step == QLatin1String("roman") ? StepAnnotation::NumberStyle::Roman : StepAnnotation::NumberStyle::Numeric;
    m_magnifierZoom = std::clamp(m_settings.intValue(QStringLiteral("annotation.magnifierZoom")), 1, 8);
    m_autoMosaic = m_settings.boolValue(QStringLiteral("annotation.autoMosaic"));
    m_multi.clear();
    m_marquee = QRectF();
    m_textDecor = TextDecor();
    m_textDecor.outlineWidth = m_settings.intValue(QStringLiteral("annotation.textOutlineWidth"));
    m_textDecor.backgroundColor = QColor::fromString(m_settings.stringValue(QStringLiteral("annotation.textBackground")));
    m_textDecor.backgroundRadius = m_settings.intValue(QStringLiteral("annotation.textBackgroundRadius"));
    m_textDecor.backgroundPadding = m_settings.intValue(QStringLiteral("annotation.textBackgroundPadding"));
    m_textDecor.shadowColor = QColor::fromString(m_settings.stringValue(QStringLiteral("annotation.textShadowColor")));
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
        line->setHeads(m_tool == Tool::Arrow ? m_headStart : LineAnnotation::Head::None, m_tool == Tool::Arrow ? m_headEnd : LineAnnotation::Head::None);
}

void AnnotationEditor::setPenStyle(Qt::PenStyle s)
{
    m_style.penStyle = s;
    applyStyleToSelected();
}

void AnnotationEditor::setCapStyle(Qt::PenCapStyle s)
{
    m_style.capStyle = s;
    applyStyleToSelected();
}

void AnnotationEditor::setJoinStyle(Qt::PenJoinStyle s)
{
    m_style.joinStyle = s;
    applyStyleToSelected();
}

void AnnotationEditor::setArc(double startDegrees, double spanDegrees)
{
    if (auto* e = dynamic_cast<EllipseAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = e->clone();
        e->setArc(startDegrees, spanDegrees);
        m_doc.commitModification(m_selectedId, std::move(before));
        emit changed();
    }
}

QPointF AnnotationEditor::arcHandleImage(bool end) const
{
    const auto* e = dynamic_cast<const EllipseAnnotation*>(m_doc.find(m_selectedId));
    if (!e || !qFuzzyIsNull(e->rotation()))
        return kFar;
    return e->pointAtAngle(end ? e->startAngle() + e->spanAngle() : e->startAngle());
}

bool AnnotationEditor::arcHandleAt(QPointF p, bool& end) const
{
    for (bool which : {true, false}) {
        const QPointF h = arcHandleImage(which);
        if (h.x() > -1e8 && std::abs(h.x() - p.x()) <= kHandleGrip && std::abs(h.y() - p.y()) <= kHandleGrip) {
            end = which;
            return true;
        }
    }
    return false;
}

QPointF AnnotationEditor::pointerHandleImage() const
{
    const auto* t = dynamic_cast<const TextAnnotation*>(m_doc.find(m_selectedId));
    if (!t || !qFuzzyIsNull(t->rotation()))
        return kFar;
    if (t->pointer())
        return *t->pointer();
    const QRectF b = t->textBox();
    return QPointF(b.center().x(), b.bottom() + kRotateOffset);
}

bool AnnotationEditor::pointerHandleAt(QPointF p) const
{
    const QPointF h = pointerHandleImage();
    return h.x() > -1e8 && std::abs(h.x() - p.x()) <= kHandleGrip && std::abs(h.y() - p.y()) <= kHandleGrip;
}

void AnnotationEditor::setTextPointer(std::optional<QPointF> target)
{
    if (auto* t = dynamic_cast<TextAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = t->clone();
        t->setPointer(target);
        m_doc.commitModification(m_selectedId, std::move(before));
        emit changed();
    }
}

QList<int> AnnotationEditor::selectedIds() const
{
    QList<int> ids;
    if (m_selectedId >= 0)
        ids.append(m_selectedId);
    for (int id : m_multi)
        if (id != m_selectedId && m_doc.find(id))
            ids.append(id);
    return ids;
}

void AnnotationEditor::selectMany(const QList<int>& ids)
{
    m_multi.clear();
    select(-1);
    if (ids.isEmpty())
        return;
    select(ids.first());
    for (int i = 1; i < ids.size(); ++i)
        if (m_doc.find(ids[i]))
            m_multi.append(ids[i]);
    emit changed();
}

void AnnotationEditor::removeGroup(int groupId)
{
    for (int i = m_doc.count() - 1; i >= 0; --i)
        if (const auto* e = dynamic_cast<const EffectAnnotation*>(m_doc.at(i)); e && e->groupId() == groupId)
            m_doc.remove(e->id());
}

void AnnotationEditor::addAutoMosaics(const EffectAnnotation& original)
{
    // 自动马赛克（ANN-31）：在底图里找与原区域内容相同的位置，一起打码
    const QImage& base = m_doc.baseImage();
    const QRect src = original.bounds().toAlignedRect().intersected(base.rect());
    if (src.width() < 4 || src.height() < 4 || base.isNull())
        return;
    const QImage gray = base.convertToFormat(QImage::Format_Grayscale8);
    const QImage patch = gray.copy(src);
    const int pw = patch.width(), ph = patch.height();
    const int stepX = std::max(1, pw / 8), stepY = std::max(1, ph / 8);
    QList<QRect> matches;
    const int tolerance = 12;
    for (int y = 0; y + ph <= gray.height(); ++y) {
        for (int x = 0; x + pw <= gray.width(); ++x) {
            if (QRect(x, y, pw, ph).intersects(src))
                continue;
            bool same = true;
            for (int py = 0; py < ph && same; py += stepY) {
                const uchar* a = patch.constScanLine(py);
                const uchar* b = gray.constScanLine(y + py) + x;
                for (int px = 0; px < pw; px += stepX)
                    if (std::abs(static_cast<int>(a[px]) - static_cast<int>(b[px])) > tolerance) {
                        same = false;
                        break;
                    }
            }
            if (!same)
                continue;
            bool overlaps = false;
            for (const QRect& m : matches)
                if (m.intersects(QRect(x, y, pw, ph)))
                    overlaps = true;
            if (!overlaps)
                matches.append(QRect(x, y, pw, ph));
            x += pw - 1;
        }
    }
    const int group = m_nextGroupId++;
    if (matches.isEmpty())
        return;
    if (auto* orig = dynamic_cast<EffectAnnotation*>(m_doc.find(original.id())))
        orig->setGroupId(group);
    for (const QRect& m : matches) {
        auto e = std::make_unique<EffectAnnotation>();
        e->setEffect(original.effect());
        e->setShape(EffectAnnotation::Shape::Rect);
        e->setStrength(original.strength());
        e->setStyle(original.style());
        e->setRect(QRectF(m));
        e->setGroupId(group);
        m_doc.add(std::move(e));
    }
}

void AnnotationEditor::setCornerRadius(double r)
{
    m_cornerRadius = std::max(0.0, r);
    if (auto* rect = dynamic_cast<RectAnnotation*>(m_doc.find(m_selectedId)); rect && rect->supportsRadius()) {
        auto before = rect->clone();
        rect->setRadius(m_cornerRadius);
        m_doc.commitModification(m_selectedId, std::move(before));
    }
    emit changed();
}

void AnnotationEditor::setArrowHeads(LineAnnotation::Head start, LineAnnotation::Head end)
{
    m_headStart = start;
    m_headEnd = end;
    if (auto* line = dynamic_cast<LineAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = line->clone();
        line->setHeads(start, end);
        m_doc.commitModification(m_selectedId, std::move(before));
    }
    if (auto* line = dynamic_cast<LineAnnotation*>(m_draft.get()); line && m_tool == Tool::Arrow)
        line->setHeads(start, end);
    emit changed();
}

void AnnotationEditor::setTextDecor(const TextDecor& d)
{
    m_textDecor = d;
    if (auto* text = dynamic_cast<TextAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = text->clone();
        text->setDecor(d);
        m_doc.commitModification(m_selectedId, std::move(before));
    }
    emit changed();
}

void AnnotationEditor::setStepStyle(StepAnnotation::NumberStyle s)
{
    m_stepStyle = s;
    // 样式对整组序号生效
    m_doc.undoStack().beginMacro(QStringLiteral("step style"));
    for (int i = 0; i < m_doc.count(); ++i) {
        if (auto* step = dynamic_cast<StepAnnotation*>(m_doc.at(i))) {
            auto before = step->clone();
            step->setNumberStyle(s);
            m_doc.commitModification(step->id(), std::move(before));
        }
    }
    m_doc.undoStack().endMacro();
    emit changed();
}

void AnnotationEditor::setStepNumber(int n)
{
    if (auto* step = dynamic_cast<StepAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = step->clone();
        step->setNumber(n);
        m_doc.commitModification(m_selectedId, std::move(before));
        emit changed();
    }
}

int AnnotationEditor::nextStepNumber() const
{
    int next = 1;
    for (int i = 0; i < m_doc.count(); ++i)
        if (const auto* step = dynamic_cast<const StepAnnotation*>(m_doc.at(i)))
            next = std::max(next, step->number() + 1);
    return next;
}

void AnnotationEditor::renumberStepsAfterRemoval(int removedNumber)
{
    if (!m_settings.boolValue(QStringLiteral("annotation.stepRenumber")))
        return;
    for (int i = 0; i < m_doc.count(); ++i) {
        if (auto* step = dynamic_cast<StepAnnotation*>(m_doc.at(i)); step && step->number() > removedNumber) {
            auto before = step->clone();
            step->setNumber(step->number() - 1);
            m_doc.commitModification(step->id(), std::move(before));
        }
    }
}

void AnnotationEditor::applyMagnifierToSelected()
{
    if (auto* mag = dynamic_cast<MagnifierAnnotation*>(m_doc.find(m_selectedId))) {
        auto before = mag->clone();
        mag->setShape(m_magnifierShape);
        mag->setIncludeAnnotations(m_magnifierInclude);
        mag->setShadow(m_magnifierShadow);
        if (!qFuzzyCompare(mag->zoom(), m_magnifierZoom))
            mag->setTarget(QRectF(mag->target().topLeft(), mag->source().size() * m_magnifierZoom));
        m_doc.commitModification(m_selectedId, std::move(before));
    }
    emit changed();
}

bool AnnotationEditor::createFromRect(const QRectF& r)
{
    const QRectF rect = r.normalized();
    if (rect.width() < 2 || rect.height() < 2)
        return false;
    finishDraft();
    m_dragStart = rect.topLeft();
    m_draft = makeDraft(rect.topLeft());
    if (!m_draft)
        return false;
    if (auto* hl = dynamic_cast<HighlighterAnnotation*>(m_draft.get())) {
        hl->setShape(HighlighterAnnotation::Shape::Rect);
        hl->setRect(rect);
    } else if (auto* eff = dynamic_cast<EffectAnnotation*>(m_draft.get())) {
        if (eff->shape() == EffectAnnotation::Shape::Brush)
            eff->setShape(EffectAnnotation::Shape::Rect);
        eff->setRect(rect);
    } else if (dynamic_cast<RectAnnotation*>(m_draft.get())) {
        m_draft->setBounds(rect);
    } else {
        m_draft.reset();
        return false;
    }
    commitDraft();
    return true;
}

void AnnotationEditor::applyStyleToSelected()
{
    emit styleChanged();
    Annotation* a = m_doc.find(m_selectedId);
    if (!a)
        return;
    auto before = a->clone();
    AnnotationStyle s = a->style();
    s.stroke = m_style.stroke;
    s.fill = m_style.fill;
    s.width = m_style.width;
    s.penStyle = m_style.penStyle;
    s.capStyle = m_style.capStyle;
    s.joinStyle = m_style.joinStyle;
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
    case Tool::Eraser:
    case Tool::Magnifier:
    case Tool::Spotlight:
    case Tool::SmartErase: {
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
    case Tool::Rect: {
        auto r = std::make_unique<RectAnnotation>();
        r->setRadius(m_cornerRadius);
        a = std::move(r);
        break;
    }
    case Tool::Ellipse: a = std::make_unique<EllipseAnnotation>(); break;
    case Tool::Line:
    case Tool::Arrow: {
        auto l = std::make_unique<LineAnnotation>();
        l->points() = {start, start};
        if (m_tool == Tool::Arrow)
            l->setHeads(m_headStart, m_headEnd);
        a = std::move(l);
        break;
    }
    case Tool::Step: {
        auto s = std::make_unique<StepAnnotation>();
        s->setCenter(start);
        s->setNumber(nextStepNumber());
        s->setNumberStyle(m_stepStyle);
        s->setFont(m_textFont);
        a = std::move(s);
        break;
    }
    case Tool::Magnifier: {
        auto m = std::make_unique<MagnifierAnnotation>();
        m->setSource(QRectF(start, start));
        m->setTarget(QRectF(start, start));
        m->setShape(m_magnifierShape);
        m->setIncludeAnnotations(m_magnifierInclude);
        m->setShadow(m_magnifierShadow);
        a = std::move(m);
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
    case Tool::Spotlight: {
        auto sp = std::make_unique<SpotlightAnnotation>();
        sp->setShape(m_spotlightShape);
        sp->setMaskOpacity(m_settings.intValue(QStringLiteral("annotation.spotlightOpacity")));
        sp->setBounds(QRectF(start, start));
        a = std::move(sp);
        break;
    }
    case Tool::SmartErase: {
        auto e = std::make_unique<EffectAnnotation>();
        e->setEffect(EffectAnnotation::Effect::Inpaint);
        e->setShape(m_effectShape == EffectAnnotation::Shape::Brush ? EffectAnnotation::Shape::Rect : m_effectShape);
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
    if (m_tool == Tool::Step || m_tool == Tool::Magnifier)
        s.penStyle = m_tool == Tool::Magnifier ? m_style.penStyle : Qt::SolidLine;
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
    } else if (auto* step = dynamic_cast<StepAnnotation*>(m_draft.get())) {
        // 拖离徽标一定距离：拖出指示箭头（ANN-35）
        step->setTail((raw - m_dragStart).manhattanLength() >= kStepTailMin ? std::optional<QPointF>(raw) : std::nullopt);
    } else if (auto* mag = dynamic_cast<MagnifierAnnotation*>(m_draft.get())) {
        mag->setSource(QRectF(m_dragStart, p));
        mag->setTarget(mag->source()); // 拖拽期间只显示源框
    } else if (auto* sp = dynamic_cast<SpotlightAnnotation*>(m_draft.get())) {
        sp->setBounds(QRectF(m_dragStart, p));
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
        && m_draft->type() != QLatin1String("eraser") && m_draft->type() != QLatin1String("step");
    if (pointish) {
        m_draft.reset();
        emit changed();
        return;
    }
    if (auto* mag = dynamic_cast<MagnifierAnnotation*>(m_draft.get())) {
        if (mag->source().width() < 4 || mag->source().height() < 4) {
            m_draft.reset();
            emit changed();
            return;
        }
        mag->placeTarget(m_magnifierZoom, QRectF(QPointF(0, 0), QSizeF(m_doc.size())));
    }
    const bool mosaicRect = m_tool == Tool::Mosaic && dynamic_cast<EffectAnnotation*>(m_draft.get()) && m_effectShape != EffectAnnotation::Shape::Brush;
    m_doc.undoStack().beginMacro(QStringLiteral("add"));
    const int id = m_doc.add(std::move(m_draft));
    m_draft.reset();
    if (mosaicRect && m_autoMosaic)
        if (const auto* e = dynamic_cast<const EffectAnnotation*>(m_doc.find(id)))
            addAutoMosaics(*e); // ANN-31
    m_doc.undoStack().endMacro();
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

QPointF AnnotationEditor::radiusHandleImage() const
{
    const auto* rect = dynamic_cast<const RectAnnotation*>(m_doc.find(m_selectedId));
    if (!rect || !rect->supportsRadius() || !qFuzzyIsNull(rect->rotation()))
        return kFar;
    const QRectF b = rect->bounds();
    return QPointF(b.left() + kRadiusHandleInset + rect->radius(), b.top());
}

bool AnnotationEditor::radiusHandleAt(QPointF p) const
{
    const QPointF r = radiusHandleImage();
    return r.x() > -1e8 && std::abs(r.x() - p.x()) <= kHandleGrip && std::abs(r.y() - p.y()) <= kHandleGrip;
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
        if (radiusHandleAt(p)) {
            m_drag = DragMode::Radius;
            m_beforeEdit = m_doc.find(m_selectedId)->clone();
            m_editOrigin = m_doc.find(m_selectedId)->bounds();
            return;
        }
        bool arcEnd = false;
        if (arcHandleAt(p, arcEnd)) {
            m_drag = arcEnd ? DragMode::ArcEnd : DragMode::ArcStart;
            m_beforeEdit = m_doc.find(m_selectedId)->clone();
            return;
        }
        if (pointerHandleAt(p)) {
            m_drag = DragMode::Pointer;
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
    if (secondEdit && m_doc.hitTest(p, kHitTolerance)) {
        m_drag = DragMode::Pending;
        return;
    }
    if (m_tool == Tool::None) {
        // 空白处拖动：框选多个标注（ANN-08）
        m_multi.clear();
        select(-1);
        m_marquee = QRectF(p, p);
        m_drag = DragMode::Marquee;
        emit changed();
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
            for (int id : m_multi)
                if (Annotation* other = m_doc.find(id))
                    other->translate(delta); // ANN-08：一起移动
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
    case DragMode::Marquee:
        m_marquee = QRectF(m_dragStart, p).normalized();
        emit changed();
        break;
    case DragMode::ArcStart:
    case DragMode::ArcEnd:
        if (auto* e = dynamic_cast<EllipseAnnotation*>(m_doc.find(m_selectedId))) {
            const QRectF b = e->bounds();
            // 按椭圆参数角：先归一到单位圆
            const double nx = (p.x() - b.center().x()) / std::max(1.0, b.width() / 2);
            const double ny = (b.center().y() - p.y()) / std::max(1.0, b.height() / 2);
            double deg = std::atan2(ny, nx) * 180.0 / M_PI;
            deg = std::fmod(deg + 360.0, 360.0);
            if (m_drag == DragMode::ArcStart) {
                const double endAngle = e->startAngle() + e->spanAngle();
                double span = std::fmod(endAngle - deg + 720.0, 360.0);
                if (span < 1)
                    span = 360;
                e->setArc(deg, span);
            } else {
                double span = std::fmod(deg - e->startAngle() + 720.0, 360.0);
                if (span < 1)
                    span = 360;
                e->setArc(e->startAngle(), span);
            }
            emit changed();
        }
        break;
    case DragMode::Pointer:
        if (auto* t = dynamic_cast<TextAnnotation*>(m_doc.find(m_selectedId))) {
            t->setPointer(p);
            emit changed();
        }
        break;
    case DragMode::Radius:
        if (auto* rect = dynamic_cast<RectAnnotation*>(m_doc.find(m_selectedId))) {
            rect->setRadius(std::clamp(p.x() - m_editOrigin.left() - kRadiusHandleInset, 0.0, std::min(m_editOrigin.width(), m_editOrigin.height()) / 2.0));
            m_cornerRadius = rect->radius();
            emit changed();
        }
        break;
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
        if (!hit || !m_multi.contains(hit->id()))
            m_multi.clear();
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
    case DragMode::Marquee: {
        QList<int> ids;
        for (int i = 0; i < m_doc.count(); ++i)
            if (m_marquee.intersects(m_doc.at(i)->rotatedBounds()))
                ids.append(m_doc.at(i)->id());
        m_marquee = QRectF();
        selectMany(ids);
        break;
    }
    case DragMode::Move:
        if (m_beforeEdit) {
            if (m_multi.isEmpty()) {
                m_doc.commitModification(m_selectedId, std::move(m_beforeEdit));
            } else {
                // 多选移动：一条宏命令；其他项用移动前的位置反推
                const QPointF delta = m_doc.find(m_selectedId) ? m_doc.find(m_selectedId)->bounds().topLeft() - m_beforeEdit->bounds().topLeft() : QPointF();
                m_doc.undoStack().beginMacro(QStringLiteral("move"));
                m_doc.commitModification(m_selectedId, std::move(m_beforeEdit));
                for (int id : m_multi) {
                    if (Annotation* other = m_doc.find(id)) {
                        auto before = other->clone();
                        before->translate(-delta);
                        m_doc.commitModification(id, std::move(before));
                    }
                }
                m_doc.undoStack().endMacro();
            }
        }
        break;
    case DragMode::Resize:
    case DragMode::Rotate:
    case DragMode::Radius:
    case DragMode::ArcStart:
    case DragMode::ArcEnd:
    case DragMode::Pointer:
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
    if (Annotation* a = m_doc.hitTest(p, kHitTolerance); a && (a->type() == QLatin1String("text") || a->type() == QLatin1String("step"))) {
        select(a->id());
        startTextInput(a->bounds().topLeft(), a);
    } else if (a && a->type() == QLatin1String("line") && m_settings.boolValue(QStringLiteral("annotation.arrowNote"))) {
        select(a->id()); // ANN-26：双击箭头添加注释
        startTextInput(a->bounds().topLeft(), a);
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
        m_style.penStyle = a->style().penStyle;
        m_style.capStyle = a->style().capStyle;
        m_style.joinStyle = a->style().joinStyle;
        if (const auto* rect = dynamic_cast<const RectAnnotation*>(a); rect && rect->supportsRadius())
            m_cornerRadius = rect->radius();
        if (const auto* line = dynamic_cast<const LineAnnotation*>(a); line && line->arrowEnd()) {
            m_headStart = line->headStart();
            m_headEnd = line->headEnd();
        }
        if (const auto* text = dynamic_cast<const TextAnnotation*>(a))
            m_textDecor = text->decor();
        if (const auto* mag = dynamic_cast<const MagnifierAnnotation*>(a)) {
            m_magnifierZoom = mag->zoom();
            m_magnifierShape = mag->shape();
            m_magnifierInclude = mag->includeAnnotations();
            m_magnifierShadow = mag->shadow();
        }
    }
    emit selectionChanged(m_selectedId);
    emit changed();
}

void AnnotationEditor::deleteSelected()
{
    if (m_selectedId < 0)
        return;
    const QList<int> ids = selectedIds();
    m_doc.undoStack().beginMacro(QStringLiteral("delete"));
    for (int id : ids) {
        const Annotation* a = m_doc.find(id);
        if (!a)
            continue;
        const auto* step = dynamic_cast<const StepAnnotation*>(a);
        const int removedNumber = step ? step->number() : -1;
        const auto* effect = dynamic_cast<const EffectAnnotation*>(a);
        const int group = effect ? effect->groupId() : 0;
        m_doc.remove(id);
        if (group > 0)
            removeGroup(group); // ANN-31：同组一起删
        if (removedNumber > 0)
            renumberStepsAfterRemoval(removedNumber); // ANN-35：删除中间序号后递补
    }
    m_doc.undoStack().endMacro();
    m_multi.clear();
    select(-1);
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

void AnnotationEditor::startTextInput(QPointF p, Annotation* existing)
{
    cancelText();
    if (!m_textHost)
        return;
    const auto* text = dynamic_cast<const TextAnnotation*>(existing);
    const auto* step = dynamic_cast<const StepAnnotation*>(existing);
    const auto* line = dynamic_cast<const LineAnnotation*>(existing);
    m_textPos = text ? text->position() : step ? step->badgeRect().topRight() + QPointF(6, 0) : line ? line->points().last() + QPointF(8, -10) : p;
    m_textEditingId = existing ? existing->id() : -1;
    auto* edit = new InlineTextEdit(this, m_textHost);
    m_textEdit = edit;
    QFont f = text ? text->font() : step ? step->font() : line ? line->font() : m_textFont;
    // 输入框按逻辑像素显示：字号 / 缩放
    QFont widgetFont = f;
    widgetFont.setPixelSize(std::max(1, static_cast<int>(std::round(f.pixelSize() / m_hostScale))));
    edit->setFont(widgetFont);
    const QColor color = existing ? existing->style().stroke : m_style.stroke;
    edit->setStyleSheet(QStringLiteral("QPlainTextEdit { background: rgba(255,255,255,200); color: %1; border: 1px dashed #3078dc; }").arg(color.name(QColor::HexArgb)));
    if (text)
        edit->setPlainText(text->text());
    else if (step)
        edit->setPlainText(step->text());
    else if (line)
        edit->setPlainText(line->text());
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
            if (auto* step = dynamic_cast<StepAnnotation*>(a)) {
                auto before = step->clone();
                step->setText(text.trimmed()); // 序号说明文字（ANN-35），可清空
                m_doc.commitModification(m_textEditingId, std::move(before));
            } else if (auto* line = dynamic_cast<LineAnnotation*>(a)) {
                auto before = line->clone();
                line->setText(text.trimmed()); // 箭头注释（ANN-26）
                m_doc.commitModification(m_textEditingId, std::move(before));
            } else if (text.trimmed().isEmpty()) {
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
        t->setDecor(m_textDecor);
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
    if (m_drag == DragMode::Marquee && !m_marquee.isEmpty()) {
        QPen band(QColor(40, 120, 220), 1, Qt::DashLine);
        band.setCosmetic(true);
        p.setPen(band);
        p.setBrush(QColor(40, 120, 220, 40));
        p.drawRect(QRectF(topLeft + m_marquee.topLeft() / scale, m_marquee.size() / scale));
    }
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
        const QPointF rh = radiusHandleImage();
        if (rh.x() > -1e8) {
            // 圆角控制点（ANN-21）：黄色菱形
            const QPointF l = topLeft + rh / scale;
            p.setBrush(QColor(255, 200, 0));
            p.drawPolygon(QPolygonF{l + QPointF(0, -5), l + QPointF(5, 0), l + QPointF(0, 5), l + QPointF(-5, 0)});
        }
        for (bool end : {false, true}) {
            const QPointF ah = arcHandleImage(end);
            if (ah.x() > -1e8) {
                // 弧控制点（ANN-22）：绿色圆点
                const QPointF l = topLeft + ah / scale;
                p.setBrush(QColor(60, 190, 90));
                p.drawEllipse(l, 5, 5);
            }
        }
        const QPointF ph = pointerHandleImage();
        if (ph.x() > -1e8) {
            // 指示箭头控制点（ANN-34）：橙色三角
            const QPointF l = topLeft + ph / scale;
            p.setBrush(QColor(255, 140, 0));
            p.drawPolygon(QPolygonF{l + QPointF(0, 6), l + QPointF(-5, -4), l + QPointF(5, -4)});
        }
        // 多选的其他项：只画包围框
        QPen multiPen(QColor(40, 120, 220, 160), 1, Qt::DashLine);
        multiPen.setCosmetic(true);
        p.setPen(multiPen);
        p.setBrush(Qt::NoBrush);
        for (int id : m_multi)
            if (const Annotation* other = m_doc.find(id))
                p.drawRect(QRectF(topLeft + other->rotatedBounds().topLeft() / scale, other->rotatedBounds().size() / scale));
        p.restore();
    }
}

} // namespace ss
