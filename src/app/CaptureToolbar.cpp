#include "app/CaptureToolbar.h"

#include "app/ToolbarIcons.h"

#include "core/config/Settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QColorDialog>
#include <QCursor>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QWheelEvent>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <tuple>

namespace ss {

namespace {

const QList<QColor> kPalette{QColor(230, 60, 60), QColor(255, 160, 0), QColor(255, 230, 0), QColor(60, 180, 75),
                             QColor(40, 120, 220), QColor(150, 60, 220), QColor(255, 255, 255), QColor(0, 0, 0)};

struct ToolDef {
    AnnotationEditor::Tool tool;
    const char* name;
    const char* text;
    const char* tip;
};

const ToolDef kTools[] = {
    {AnnotationEditor::Tool::Rect, "rect", QT_TRANSLATE_NOOP("CaptureToolbar", "Rect"), QT_TRANSLATE_NOOP("CaptureToolbar", "Rectangle (Ctrl+1)")},
    {AnnotationEditor::Tool::Ellipse, "ellipse", QT_TRANSLATE_NOOP("CaptureToolbar", "Ellipse"), QT_TRANSLATE_NOOP("CaptureToolbar", "Ellipse (Ctrl+2)")},
    {AnnotationEditor::Tool::Line, "line", QT_TRANSLATE_NOOP("CaptureToolbar", "Line"), QT_TRANSLATE_NOOP("CaptureToolbar", "Line / polyline (Ctrl+3); Tab switches to arrow")},
    {AnnotationEditor::Tool::Arrow, "arrow", QT_TRANSLATE_NOOP("CaptureToolbar", "Arrow"), QT_TRANSLATE_NOOP("CaptureToolbar", "Arrow (Ctrl+4)")},
    {AnnotationEditor::Tool::Pencil, "pencil", QT_TRANSLATE_NOOP("CaptureToolbar", "Pencil"), QT_TRANSLATE_NOOP("CaptureToolbar", "Pencil (B); Shift for a straight line")},
    {AnnotationEditor::Tool::Highlighter, "highlighter", QT_TRANSLATE_NOOP("CaptureToolbar", "Marker"), QT_TRANSLATE_NOOP("CaptureToolbar", "Highlighter (Ctrl+5)")},
    {AnnotationEditor::Tool::Mosaic, "mosaic", QT_TRANSLATE_NOOP("CaptureToolbar", "Mosaic"), QT_TRANSLATE_NOOP("CaptureToolbar", "Mosaic (Ctrl+6)")},
    {AnnotationEditor::Tool::Blur, "blur", QT_TRANSLATE_NOOP("CaptureToolbar", "Blur"), QT_TRANSLATE_NOOP("CaptureToolbar", "Gaussian blur (Ctrl+7)")},
    {AnnotationEditor::Tool::Text, "text", QT_TRANSLATE_NOOP("CaptureToolbar", "Text"), QT_TRANSLATE_NOOP("CaptureToolbar", "Text (T); right-click to finish")},
    {AnnotationEditor::Tool::Eraser, "eraser", QT_TRANSLATE_NOOP("CaptureToolbar", "Eraser"), QT_TRANSLATE_NOOP("CaptureToolbar", "Eraser (Ctrl+8): erases annotations only")},
    {AnnotationEditor::Tool::Step, "step", QT_TRANSLATE_NOOP("CaptureToolbar", "Step"), QT_TRANSLATE_NOOP("CaptureToolbar", "Step number (N): click to add, drag to add a pointer, double-click to add a note")},
    {AnnotationEditor::Tool::Magnifier, "magnifier", QT_TRANSLATE_NOOP("CaptureToolbar", "Magnify"), QT_TRANSLATE_NOOP("CaptureToolbar", "Magnifier (M): drag the area to enlarge")},
    {AnnotationEditor::Tool::Spotlight, "spotlight", QT_TRANSLATE_NOOP("CaptureToolbar", "Spotlight"), QT_TRANSLATE_NOOP("CaptureToolbar", "Spotlight: darken everything outside the area")},
    {AnnotationEditor::Tool::SmartErase, "smartErase", QT_TRANSLATE_NOOP("CaptureToolbar", "Smart erase"), QT_TRANSLATE_NOOP("CaptureToolbar", "Smart erase: fill the area from its surroundings")},
};

const char* const kHeadNames[] = {"none", "triangle", "open", "dot", "bar"};

// 工具表里的文案按声明时的上下文查翻译
QString trTool(const char* source) { return QCoreApplication::translate("CaptureToolbar", source); }

} // namespace

void CaptureToolbar::rebuildTools()
{
    auto* row = static_cast<QHBoxLayout*>(m_tools->layout());
    const QStringList common = m_target->settings().stringValue(QStringLiteral("annotation.toolbarCommon")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    QList<const ToolDef*> ordered;
    for (const QString& name : common)
        for (const auto& d : kTools)
            if (name.trimmed() == QLatin1String(d.name) && !ordered.contains(&d))
                ordered.append(&d);
    // 先全部摘下，再按常用区顺序插到行首
    for (const auto& d : kTools) {
        QToolButton* b = m_toolButtons.value(QString::fromLatin1(d.name));
        row->removeWidget(b);
        b->setVisible(ordered.contains(&d));
    }
    int index = 0;
    for (const ToolDef* d : ordered)
        row->insertWidget(index++, m_toolButtons.value(QString::fromLatin1(d->name)));
    QMenu* moreMenu = m_moreButton->menu();
    moreMenu->clear();
    for (const auto& d : kTools) {
        if (ordered.contains(&d))
            continue;
        const AnnotationEditor::Tool tool = d.tool;
        moreMenu->addAction(toolbarIcon(QString::fromLatin1(d.name)), trTool(d.text), this, [this, tool] { m_target->setTool(tool); });
    }
    m_moreButton->setVisible(!moreMenu->isEmpty());
    m_recentButton->setVisible(false);
    m_recentTool = AnnotationEditor::Tool::None;
    adjustSize();
}

const char* CaptureToolbar::kToolText(AnnotationEditor::Tool tool)
{
    for (const auto& d : kTools)
        if (d.tool == tool)
            return d.text;
    return "";
}

QString CaptureToolbar::toolName(AnnotationEditor::Tool tool)
{
    for (const auto& d : kTools)
        if (d.tool == tool)
            return QString::fromLatin1(d.name);
    return QStringLiteral("none");
}

AnnotationEditor::Tool CaptureToolbar::toolFromName(const QString& name)
{
    for (const auto& d : kTools)
        if (name == QLatin1String(d.name))
            return d.tool;
    return AnnotationEditor::Tool::None;
}

CaptureToolbar::CaptureToolbar(ToolbarTarget* target)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_target(target)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet(QStringLiteral("QWidget { background: #2b2b2b; color: white; } QToolButton { color: white; padding: 4px 7px; border: none; border-radius: 3px; }"
                                 "QToolButton:hover { background: #3d6fb4; } QToolButton:checked { background: #1f5aa6; }"
                                 "QSpinBox, QFontComboBox { background: #3a3a3a; color: white; border: 1px solid #555; }"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(3);

    m_tools = new QWidget(this);
    auto* row = new QHBoxLayout(m_tools);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    layout->addWidget(m_tools);
    // 自定义工具栏（ANN-14）：常用区按设置顺序；其余折叠进“更多”；最近从折叠区用过的工具有一个浮动按钮
    for (const auto& d : kTools) {
        QToolButton* b = addButton(m_tools, QString::fromLatin1(d.name), trTool(d.text), trTool(d.tip), true);
        m_toolButtons.insert(QString::fromLatin1(d.name), b);
        const AnnotationEditor::Tool tool = d.tool;
        connect(b, &QToolButton::clicked, this, [this, tool, b](bool checked) {
            if (m_syncing)
                return;
            m_target->setTool(checked ? tool : AnnotationEditor::Tool::None);
            if (!checked)
                b->setChecked(false);
        });
    }
    m_recentButton = addButton(m_tools, QStringLiteral("recent"), QString(), tr("Last tool used from More"), true);
    m_recentButton->setVisible(false);
    connect(m_recentButton, &QToolButton::clicked, this, [this](bool checked) {
        if (!m_syncing)
            m_target->setTool(checked ? m_recentTool : AnnotationEditor::Tool::None);
    });
    m_moreButton = addButton(m_tools, QStringLiteral("more"), tr("More"), tr("Tools not on the toolbar"));
    m_moreButton->setMenu(new QMenu(m_moreButton));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    rebuildTools();
    connect(&target->settings(), &Settings::changed, this, [this](const QString& key, const QVariant&) {
        if (key == QLatin1String("annotation.toolbarCommon"))
            rebuildTools();
        else if (key == QLatin1String("annotation.palette") && m_paletteRow)
            rebuildPalette();
    });
    auto* sep = new QFrame(m_tools);
    sep->setFrameShape(QFrame::VLine);
    row->addWidget(sep);
    connect(addButton(m_tools, QStringLiteral("undo"), tr("Undo"), tr("Undo (Ctrl+Z)")), &QToolButton::clicked, this, [this] { m_target->editor().undo(); });
    connect(addButton(m_tools, QStringLiteral("redo"), tr("Redo"), tr("Redo (Ctrl+Y)")), &QToolButton::clicked, this, [this] { m_target->editor().redo(); });
    connect(addButton(m_tools, QStringLiteral("clear"), tr("Clear"), tr("Remove all annotations (undoable)")), &QToolButton::clicked, this, [this] { m_target->editor().clearAll(); });
    QToolButton* watermark = addButton(m_tools, QStringLiteral("watermark"), tr("Watermark"), tr("Add or remove the watermark (settings: Annotation)"), true);
    connect(watermark, &QToolButton::clicked, this, [this](bool on) {
        if (m_syncing)
            return;
        if (on)
            m_target->editor().addWatermark();
        else
            m_target->editor().removeWatermark();
    });
    auto* sep2 = new QFrame(m_tools);
    sep2->setFrameShape(QFrame::VLine);
    row->addWidget(sep2);
    connect(addButton(m_tools, QStringLiteral("copy"), tr("Copy"), tr("Copy to clipboard (Enter)")), &QToolButton::clicked, this, [this] { m_target->copy(); });
    connect(addButton(m_tools, QStringLiteral("save"), tr("Save"), tr("Save to file (Ctrl+S); Shift+click for quick save")), &QToolButton::clicked, this, [this] {
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
            m_target->quickSave();
        else
            m_target->save();
    });
    connect(addButton(m_tools, QStringLiteral("quickSave"), tr("Quick save"), tr("Save to the quick save folder (Ctrl+Shift+S)")), &QToolButton::clicked, this, [this] { m_target->quickSave(); });
    QToolButton* pinButton = addButton(m_tools, QStringLiteral("pin"), tr("Pin"), tr("Pin to screen (Ctrl+T)"));
    connect(pinButton, &QToolButton::clicked, this, [this] { m_target->pin(); });
    pinButton->setVisible(target->supportsPin());
    QToolButton* printButton = addButton(m_tools, QStringLiteral("print"), tr("Print"), tr("Print (Ctrl+P)"));
    connect(printButton, &QToolButton::clicked, this, [this] { m_target->print(); });
    printButton->setVisible(target->supportsCaptureOptions());
    QToolButton* codesButton = addButton(m_tools, QStringLiteral("codes"), tr("QR"), tr("Decode QR codes / barcodes in the selection"));
    connect(codesButton, &QToolButton::clicked, this, [this] { m_target->detectCodes(); });
    codesButton->setVisible(false);
    QToolButton* modeButton = addButton(m_tools, QStringLiteral("mode"), tr("Capture"), tr("Mode: capture / scrolling capture / recording (Ctrl+Tab)"));
    connect(modeButton, &QToolButton::clicked, this, [this] { m_target->cycleMode(); });
    modeButton->setVisible(target->supportsCaptureOptions());
    QToolButton* scrollButton = addButton(m_tools, QStringLiteral("scroll"), tr("Scroll"), tr("Scrolling capture (Ctrl+L)"));
    connect(scrollButton, &QToolButton::clicked, this, [this] { m_target->startScroll(); });
    scrollButton->setVisible(target->supportsScroll());
    for (const auto& [name, text, tip] : QList<std::tuple<QString, QString, QString>>{{QStringLiteral("ratio"), tr("1:1 lock"), tr("Keep the current aspect ratio while resizing (Shift drags a square)")},
                                                                                     {QStringLiteral("shadow"), tr("Shadow"), tr("Add a shadow to the output")},
                                                                                     {QStringLiteral("border"), tr("Border"), tr("Add a border to the output")}}) {
        QToolButton* b = addButton(m_tools, name, text, tip, true);
        b->setVisible(target->supportsCaptureOptions());
        const QString n = name;
        connect(b, &QToolButton::clicked, this, [this, n](bool checked) {
            if (n == QLatin1String("ratio"))
                m_target->setRatioLocked(checked);
            else if (n == QLatin1String("shadow"))
                m_target->setShadowEnabled(checked);
            else
                m_target->setBorderEnabled(checked);
        });
    }
    connect(addButton(m_tools, QStringLiteral("cancel"), tr("Cancel"), tr("Cancel (Esc)")), &QToolButton::clicked, this, [this] { m_target->cancel(); });

    buildOptions();
    layout->addWidget(m_options);
    m_options->hide();

    connect(&target->editor(), &AnnotationEditor::toolChanged, this, [this] { syncFromEditor(); });
    connect(&target->editor(), &AnnotationEditor::selectionChanged, this, [this] { syncFromEditor(); });
    connect(&target->editor(), &AnnotationEditor::styleChanged, this, [this] {
        if (!m_syncing)
            syncFromEditor();
    });
}

QToolButton* CaptureToolbar::addButton(QWidget* row, const QString& name, const QString& text, const QString& tooltip, bool checkable)
{
    auto* b = new QToolButton(row);
    b->setObjectName(name);
    if (hasToolbarIcon(name)) {
        b->setIcon(toolbarIcon(name));
        b->setIconSize(QSize(20, 20));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(tooltip.isEmpty() || tooltip.startsWith(text) ? tooltip : text + QStringLiteral(" - ") + tooltip);
    } else {
        b->setText(text);
        b->setToolTip(tooltip);
    }
    b->setCheckable(checkable);
    b->setFocusPolicy(Qt::NoFocus);
    row->layout()->addWidget(b);
    return b;
}

void CaptureToolbar::buildOptions()
{
    m_options = new QWidget(this);
    auto* row = new QHBoxLayout(m_options);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    AnnotationEditor& editor = m_target->editor();

    // 颜色（ANN-03、ANN-04）：色板来自设置，可编辑 / 排序 / 导入导出；滚轮调透明度
    m_paletteRow = new QWidget(m_options);
    auto* paletteLayout = new QHBoxLayout(m_paletteRow);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    paletteLayout->setSpacing(2);
    row->addWidget(m_paletteRow);
    rebuildPalette();
    m_customColor = addButton(m_options, QStringLiteral("customColor"), tr("Custom"), tr("Pick a color (with opacity)"));
    connect(m_customColor, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_target->editor().style().stroke, nullptr, tr("Annotation color"), QColorDialog::ShowAlphaChannel);
        if (c.isValid()) {
            m_target->editor().setColor(c);
            refreshColorButtons();
        }
    });

    // 笔宽（ANN-06）：预设 + 微调
    auto* widthLabel = new QLabel(tr("Width"), m_options);
    row->addWidget(widthLabel);
    const QStringList presets = m_target->settings().stringValue(QStringLiteral("annotation.widthPresets")).split(QLatin1Char(','), Qt::SkipEmptyParts);
    int shown = 0;
    for (const QString& p : presets) {
        const int w = p.trimmed().toInt();
        if (w <= 0 || shown >= 5)
            continue;
        QToolButton* b = addButton(m_options, QStringLiteral("width_%1").arg(w), QString::number(w), tr("Pen width %1").arg(w));
        connect(b, &QToolButton::clicked, this, [this, w] { m_target->editor().setWidth(w); });
        ++shown;
    }
    m_width = new QSpinBox(m_options);
    m_width->setObjectName(QStringLiteral("width"));
    m_width->setRange(1, 100);
    m_width->setFocusPolicy(Qt::NoFocus);
    connect(m_width, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_target->editor().setWidth(v);
    });
    row->addWidget(m_width);

    // 填充（ANN-20）
    m_fillRow = new QWidget(m_options);
    auto* fillLayout = new QHBoxLayout(m_fillRow);
    fillLayout->setContentsMargins(0, 0, 0, 0);
    QToolButton* fill = addButton(m_fillRow, QStringLiteral("fill"), tr("Fill"), tr("Filled shape"), true);
    connect(fill, &QToolButton::clicked, this, [this](bool checked) { m_target->editor().setFilled(checked); });
    fillLayout->addWidget(new QLabel(tr("Radius"), m_fillRow));
    m_radius = new QSpinBox(m_fillRow);
    m_radius->setObjectName(QStringLiteral("radius"));
    m_radius->setRange(0, 500);
    m_radius->setFocusPolicy(Qt::NoFocus);
    connect(m_radius, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_target->editor().setCornerRadius(v);
    });
    fillLayout->addWidget(m_radius);
    row->addWidget(m_fillRow);

    // 线型（ANN-21）
    m_styleRow = new QWidget(m_options);
    auto* styleLayout = new QHBoxLayout(m_styleRow);
    styleLayout->setContentsMargins(0, 0, 0, 0);
    for (const auto& [name, text, style] : QList<std::tuple<QString, QString, Qt::PenStyle>>{{QStringLiteral("style_solid"), tr("Solid"), Qt::SolidLine},
                                                                                           {QStringLiteral("style_dash"), tr("Dash"), Qt::DashLine},
                                                                                           {QStringLiteral("style_dot"), tr("Dot"), Qt::DotLine}}) {
        QToolButton* b = addButton(m_styleRow, name, text, tr("Line style"), true);
        const Qt::PenStyle s = style;
        connect(b, &QToolButton::clicked, this, [this, s] {
            m_target->editor().setPenStyle(s);
            syncFromEditor();
        });
    }
    // 端点 / 连接（ANN-24）
    QToolButton* capSquare = addButton(m_styleRow, QStringLiteral("capSquare"), tr("Square ends"), tr("Square line ends (off: round)"), true);
    connect(capSquare, &QToolButton::clicked, this, [this](bool on) { m_target->editor().setCapStyle(on ? Qt::SquareCap : Qt::RoundCap); });
    QToolButton* joinMiter = addButton(m_styleRow, QStringLiteral("joinMiter"), tr("Sharp corners"), tr("Sharp line joins (off: round)"), true);
    connect(joinMiter, &QToolButton::clicked, this, [this](bool on) { m_target->editor().setJoinStyle(on ? Qt::MiterJoin : Qt::RoundJoin); });
    row->addWidget(m_styleRow);

    // 箭头样式（ANN-25）：起点 / 终点
    m_arrowRow = new QWidget(m_options);
    auto* arrowLayout = new QHBoxLayout(m_arrowRow);
    arrowLayout->setContentsMargins(0, 0, 0, 0);
    auto makeHeadCombo = [this, arrowLayout](const QString& name, const QString& label) {
        arrowLayout->addWidget(new QLabel(label, m_arrowRow));
        auto* combo = new QComboBox(m_arrowRow);
        combo->setObjectName(name);
        combo->setFocusPolicy(Qt::NoFocus);
        combo->addItems({tr("None"), tr("Triangle"), tr("Open"), tr("Dot"), tr("Bar")});
        arrowLayout->addWidget(combo);
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            if (m_syncing)
                return;
            m_target->editor().setArrowHeads(LineAnnotation::headFromName(QLatin1String(kHeadNames[m_arrowStart->currentIndex()])),
                                             LineAnnotation::headFromName(QLatin1String(kHeadNames[m_arrowEnd->currentIndex()])));
        });
        return combo;
    };
    m_arrowStart = makeHeadCombo(QStringLiteral("arrowStart"), tr("Start"));
    m_arrowEnd = makeHeadCombo(QStringLiteral("arrowEnd"), tr("End"));
    row->addWidget(m_arrowRow);

    // 序号（ANN-35）
    m_stepRow = new QWidget(m_options);
    auto* stepLayout = new QHBoxLayout(m_stepRow);
    stepLayout->setContentsMargins(0, 0, 0, 0);
    m_stepStyle = new QComboBox(m_stepRow);
    m_stepStyle->setObjectName(QStringLiteral("stepStyle"));
    m_stepStyle->setFocusPolicy(Qt::NoFocus);
    m_stepStyle->addItems({tr("1, 2, 3"), tr("A, B, C"), tr("I, II, III")});
    connect(m_stepStyle, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (!m_syncing)
            m_target->editor().setStepStyle(static_cast<StepAnnotation::NumberStyle>(idx));
    });
    stepLayout->addWidget(m_stepStyle);
    stepLayout->addWidget(new QLabel(tr("Value"), m_stepRow));
    m_stepValue = new QSpinBox(m_stepRow);
    m_stepValue->setObjectName(QStringLiteral("stepValue"));
    m_stepValue->setRange(1, 9999);
    m_stepValue->setFocusPolicy(Qt::NoFocus);
    connect(m_stepValue, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_target->editor().setStepNumber(v);
    });
    stepLayout->addWidget(m_stepValue);
    row->addWidget(m_stepRow);

    // 放大（ANN-37）
    m_magnifierRow = new QWidget(m_options);
    auto* magLayout = new QHBoxLayout(m_magnifierRow);
    magLayout->setContentsMargins(0, 0, 0, 0);
    magLayout->addWidget(new QLabel(tr("Zoom"), m_magnifierRow));
    m_zoom = new QSpinBox(m_magnifierRow);
    m_zoom->setObjectName(QStringLiteral("zoom"));
    m_zoom->setRange(1, 8);
    m_zoom->setFocusPolicy(Qt::NoFocus);
    connect(m_zoom, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_target->editor().setMagnifierZoom(v);
    });
    magLayout->addWidget(m_zoom);
    QToolButton* includeBelow = addButton(m_magnifierRow, QStringLiteral("includeBelow"), tr("With annotations"), tr("Magnify the annotations underneath too"), true);
    connect(includeBelow, &QToolButton::clicked, this, [this](bool on) { m_target->editor().setMagnifierIncludeAnnotations(on); });
    QToolButton* magShadow = addButton(m_magnifierRow, QStringLiteral("magShadow"), tr("Shadow"), tr("Drop shadow under the magnified area"), true);
    connect(magShadow, &QToolButton::clicked, this, [this](bool on) { m_target->editor().setMagnifierShadow(on); });
    row->addWidget(m_magnifierRow);

    // 形状（荧光笔 / 效果 / 橡皮擦）
    m_shapeRow = new QWidget(m_options);
    auto* shapeLayout = new QHBoxLayout(m_shapeRow);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    for (const char* n : {"shape_brush", "shape_rect", "shape_ellipse"}) {
        const QString name = QString::fromLatin1(n);
        QToolButton* b = addButton(m_shapeRow, name, name == QLatin1String("shape_brush") ? tr("Brush") : name == QLatin1String("shape_rect") ? tr("Rect") : tr("Ellipse"), tr("Shape"), true);
        connect(b, &QToolButton::clicked, this, [this, name] {
            AnnotationEditor& e = m_target->editor();
            const int idx = name == QLatin1String("shape_brush") ? 0 : name == QLatin1String("shape_rect") ? 1 : 2;
            if (e.tool() == AnnotationEditor::Tool::Highlighter)
                e.setHighlighterShape(idx == 0 ? HighlighterAnnotation::Shape::Brush : HighlighterAnnotation::Shape::Rect);
            else if (e.tool() == AnnotationEditor::Tool::Eraser)
                e.setEraserShape(static_cast<EraserAnnotation::Shape>(idx));
            else if (e.tool() == AnnotationEditor::Tool::Magnifier)
                e.setMagnifierShape(idx == 2 ? MagnifierAnnotation::Shape::Ellipse : MagnifierAnnotation::Shape::Rect);
            else if (e.tool() == AnnotationEditor::Tool::Spotlight)
                e.setSpotlightShape(idx == 2 ? SpotlightAnnotation::Shape::Ellipse : SpotlightAnnotation::Shape::Rect);
            else
                e.setEffectShape(static_cast<EffectAnnotation::Shape>(idx));
            syncFromEditor();
        });
    }
    row->addWidget(m_shapeRow);

    // 强度（ANN-29）
    m_strengthRow = new QWidget(m_options);
    auto* strengthLayout = new QHBoxLayout(m_strengthRow);
    strengthLayout->setContentsMargins(0, 0, 0, 0);
    strengthLayout->addWidget(new QLabel(tr("Strength"), m_strengthRow));
    m_strength = new QSlider(Qt::Horizontal, m_strengthRow);
    m_strength->setObjectName(QStringLiteral("strength"));
    m_strength->setRange(1, 100);
    m_strength->setFixedWidth(90);
    m_strength->setFocusPolicy(Qt::NoFocus);
    connect(m_strength, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_target->editor().setEffectStrength(v);
    });
    strengthLayout->addWidget(m_strength);
    row->addWidget(m_strengthRow);

    // 文字（ANN-32）
    m_textRow = new QWidget(m_options);
    auto* textLayout = new QHBoxLayout(m_textRow);
    textLayout->setContentsMargins(0, 0, 0, 0);
    m_fontCombo = new QFontComboBox(m_textRow);
    m_fontCombo->setObjectName(QStringLiteral("font"));
    m_fontCombo->setFocusPolicy(Qt::NoFocus);
    m_fontCombo->setFixedWidth(150);
    m_fontSize = new QSpinBox(m_textRow);
    m_fontSize->setObjectName(QStringLiteral("fontSize"));
    m_fontSize->setRange(6, 200);
    m_fontSize->setFocusPolicy(Qt::NoFocus);
    QToolButton* bold = addButton(m_textRow, QStringLiteral("bold"), tr("B"), tr("Bold"), true);
    QToolButton* italic = addButton(m_textRow, QStringLiteral("italic"), tr("I"), tr("Italic"), true);
    textLayout->insertWidget(0, m_fontCombo);
    textLayout->insertWidget(1, m_fontSize);
    // 文字装饰（ANN-33）
    for (const auto& [name, text, tip] : QList<std::tuple<QString, QString, QString>>{{QStringLiteral("outline"), tr("Outline"), tr("Outline the text")},
                                                                                     {QStringLiteral("background"), tr("Background"), tr("Fill a box behind the text")},
                                                                                     {QStringLiteral("textShadow"), tr("Shadow"), tr("Drop shadow under the text")}}) {
        QToolButton* b = addButton(m_textRow, name, text, tip, true);
        const QString n = name;
        connect(b, &QToolButton::clicked, this, [this, n](bool on) {
            TextDecor d = m_target->editor().textDecor();
            if (n == QLatin1String("outline"))
                d.outline = on;
            else if (n == QLatin1String("background"))
                d.background = on;
            else
                d.shadow = on;
            m_target->editor().setTextDecor(d);
        });
    }
    auto applyFont = [this, bold, italic] {
        if (m_syncing)
            return;
        QFont f = m_fontCombo->currentFont();
        f.setPixelSize(m_fontSize->value());
        f.setBold(bold->isChecked());
        f.setItalic(italic->isChecked());
        m_target->editor().setTextFont(f);
    };
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, applyFont);
    connect(m_fontSize, &QSpinBox::valueChanged, this, applyFont);
    connect(bold, &QToolButton::clicked, this, applyFont);
    connect(italic, &QToolButton::clicked, this, applyFont);
    row->addWidget(m_textRow);
}

QList<QColor> CaptureToolbar::paletteColors() const
{
    QList<QColor> colors;
    for (const QString& s : m_target->settings().stringValue(QStringLiteral("annotation.palette")).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QColor c = QColor::fromString(s.trimmed());
        if (c.isValid())
            colors.append(c);
    }
    if (colors.isEmpty())
        colors = kPalette;
    return colors;
}

void CaptureToolbar::savePalette(const QList<QColor>& colors)
{
    QStringList names;
    for (const QColor& c : colors)
        names.append(c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb));
    m_target->settings().setValue(QStringLiteral("annotation.palette"), names.join(QLatin1Char(',')));
    rebuildPalette();
}

bool CaptureToolbar::exportPalette(const QString& path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    for (const QColor& c : paletteColors())
        f.write((c.name(QColor::HexArgb) + QLatin1Char('\n')).toUtf8());
    return true;
}

bool CaptureToolbar::importPalette(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QList<QColor> colors;
    for (const QByteArray& line : f.readAll().split('\n')) {
        const QColor c = QColor::fromString(QString::fromUtf8(line).trimmed());
        if (c.isValid())
            colors.append(c);
    }
    if (colors.isEmpty())
        return false;
    savePalette(colors);
    return true;
}

void CaptureToolbar::rebuildPalette()
{
    for (QToolButton* b : m_colorButtons)
        delete b;
    m_colorButtons.clear();
    auto* layout = static_cast<QHBoxLayout*>(m_paletteRow->layout());
    const QList<QColor> colors = paletteColors();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor c = colors[i];
        auto* b = new QToolButton(m_paletteRow);
        b->setObjectName(QStringLiteral("color_%1").arg(i));
        b->setFixedSize(20, 20);
        b->setStyleSheet(QStringLiteral("QToolButton { background: %1; border: 1px solid #888; } QToolButton:checked { border: 2px solid white; }").arg(c.name(QColor::HexArgb)));
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setContextMenuPolicy(Qt::CustomContextMenu);
        b->installEventFilter(this);
        connect(b, &QToolButton::clicked, this, [this, i] {
            m_target->editor().setColor(paletteColors().value(i));
            refreshColorButtons();
        });
        connect(b, &QToolButton::customContextMenuRequested, this, [this, i](QPoint pos) {
            // 色板编辑（ANN-04）
            QList<QColor> colors = paletteColors();
            QMenu menu;
            QAction* edit = menu.addAction(tr("Edit color..."));
            QAction* add = menu.addAction(tr("Add color..."));
            QAction* remove = menu.addAction(tr("Remove"));
            QAction* left = menu.addAction(tr("Move left"));
            QAction* right = menu.addAction(tr("Move right"));
            menu.addSeparator();
            QAction* imp = menu.addAction(tr("Import palette..."));
            QAction* exp = menu.addAction(tr("Export palette..."));
            QAction* chosen = menu.exec(m_colorButtons.value(i) ? m_colorButtons[i]->mapToGlobal(pos) : QCursor::pos());
            if (chosen == edit) {
                const QColor c = QColorDialog::getColor(colors[i], nullptr, tr("Palette color"), QColorDialog::ShowAlphaChannel);
                if (c.isValid()) {
                    colors[i] = c;
                    savePalette(colors);
                }
            } else if (chosen == add) {
                const QColor c = QColorDialog::getColor(colors[i], nullptr, tr("Palette color"), QColorDialog::ShowAlphaChannel);
                if (c.isValid()) {
                    colors.insert(i + 1, c);
                    savePalette(colors);
                }
            } else if (chosen == remove && colors.size() > 1) {
                colors.removeAt(i);
                savePalette(colors);
            } else if (chosen == left && i > 0) {
                colors.swapItemsAt(i, i - 1);
                savePalette(colors);
            } else if (chosen == right && i + 1 < colors.size()) {
                colors.swapItemsAt(i, i + 1);
                savePalette(colors);
            } else if (chosen == imp) {
                const QString path = QFileDialog::getOpenFileName(nullptr, tr("Import palette"), QString(), tr("Palette (*.txt)"));
                if (!path.isEmpty())
                    importPalette(path);
            } else if (chosen == exp) {
                const QString path = QFileDialog::getSaveFileName(nullptr, tr("Export palette"), QStringLiteral("palette.txt"), tr("Palette (*.txt)"));
                if (!path.isEmpty())
                    exportPalette(path);
            }
        });
        layout->addWidget(b);
        m_colorButtons.append(b);
    }
    refreshColorButtons();
}

bool CaptureToolbar::eventFilter(QObject* watched, QEvent* event)
{
    // 颜色按钮上滚轮：调整该色的透明度并应用（ANN-04）
    if (event->type() == QEvent::Wheel) {
        const int index = static_cast<int>(m_colorButtons.indexOf(qobject_cast<QToolButton*>(watched)));
        if (index >= 0) {
            const int steps = static_cast<QWheelEvent*>(event)->angleDelta().y() / 120;
            QList<QColor> colors = paletteColors();
            QColor c = colors[index];
            c.setAlpha(std::clamp(c.alpha() + steps * 10, 10, 255));
            colors[index] = c;
            savePalette(colors);
            m_target->editor().setColor(c);
            refreshColorButtons();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CaptureToolbar::refreshColorButtons()
{
    const QColor current = m_target->editor().style().stroke;
    const QList<QColor> colors = paletteColors();
    for (int i = 0; i < m_colorButtons.size() && i < colors.size(); ++i)
        m_colorButtons[i]->setChecked(colors[i].rgba() == current.rgba());
    if (m_customColor)
        m_customColor->setStyleSheet(QStringLiteral("QToolButton { border: 2px solid %1; }").arg(current.name()));
}

void CaptureToolbar::syncFromEditor()
{
    m_syncing = true;
    AnnotationEditor& e = m_target->editor();
    const bool annotating = m_target->annotating();
    const AnnotationEditor::Tool tool = e.tool();
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        it.value()->setChecked(annotating && toolFromName(it.key()) == tool);
    // 折叠区工具被使用：显示“最近”浮动按钮（ANN-14）
    if (tool != AnnotationEditor::Tool::None && m_toolButtons.contains(toolName(tool)) && !m_toolButtons.value(toolName(tool))->isVisibleTo(m_tools)) {
        m_recentTool = tool;
        m_recentButton->setIcon(toolbarIcon(toolName(tool)));
        m_recentButton->setToolTip(trTool(kToolText(tool)));
        m_recentButton->setVisible(true);
    }
    m_recentButton->setChecked(annotating && m_recentButton->isVisibleTo(m_tools) && tool == m_recentTool);
    if (auto* b = button(QStringLiteral("watermark")))
        b->setChecked(e.hasWatermark());
    if (auto* b = button(QStringLiteral("ratio")))
        b->setChecked(m_target->ratioLocked());
    if (auto* b = button(QStringLiteral("shadow")))
        b->setChecked(m_target->shadowEnabled());
    if (auto* b = button(QStringLiteral("border")))
        b->setChecked(m_target->borderEnabled());
    if (auto* b = button(QStringLiteral("mode")); b && m_target->supportsCaptureOptions()) {
        b->setIcon(toolbarIcon(m_target->modeIconName()));
        b->setToolTip(m_target->modeName() + QStringLiteral(" - ") + tr("Mode: capture / scrolling capture / recording (Ctrl+Tab)"));
    }
    if (auto* b = button(QStringLiteral("codes"))) {
        const QString mode = m_target->settings().stringValue(QStringLiteral("capture.qrDetect"));
        b->setVisible(m_target->supportsCaptureOptions() && (mode == QLatin1String("manual") || m_target->hasCodes()));
        b->setToolTip(m_target->hasCodes() ? tr("QR code / barcode found - click to decode") : tr("Decode QR codes / barcodes in the selection"));
    }
    const bool showOptions = annotating && (tool != AnnotationEditor::Tool::None || e.selectedId() >= 0);
    m_options->setVisible(showOptions);
    if (showOptions) {
        refreshColorButtons();
        m_width->setValue(e.style().width);
        const Annotation* selected = e.document().find(e.selectedId());
        const QString selectedType = selected ? selected->type() : QString();
        m_fillRow->setVisible(tool == AnnotationEditor::Tool::Rect || tool == AnnotationEditor::Tool::Ellipse);
        if (auto* fill = m_fillRow->findChild<QToolButton*>(QStringLiteral("fill")))
            fill->setChecked(e.style().filled);
        const bool radius = tool == AnnotationEditor::Tool::Rect || selectedType == QLatin1String("rect");
        m_radius->setVisible(radius);
        m_fillRow->findChild<QLabel*>()->setVisible(radius);
        m_radius->setValue(static_cast<int>(std::round(e.cornerRadius())));
        const bool styles = tool == AnnotationEditor::Tool::Rect || tool == AnnotationEditor::Tool::Ellipse || tool == AnnotationEditor::Tool::Line || tool == AnnotationEditor::Tool::Arrow
            || tool == AnnotationEditor::Tool::Pencil || tool == AnnotationEditor::Tool::Magnifier || tool == AnnotationEditor::Tool::Spotlight;
        m_styleRow->setVisible(styles);
        m_styleRow->findChild<QToolButton*>(QStringLiteral("style_solid"))->setChecked(e.style().penStyle == Qt::SolidLine);
        m_styleRow->findChild<QToolButton*>(QStringLiteral("style_dash"))->setChecked(e.style().penStyle == Qt::DashLine);
        m_styleRow->findChild<QToolButton*>(QStringLiteral("style_dot"))->setChecked(e.style().penStyle == Qt::DotLine);
        m_styleRow->findChild<QToolButton*>(QStringLiteral("capSquare"))->setChecked(e.style().capStyle == Qt::SquareCap);
        m_styleRow->findChild<QToolButton*>(QStringLiteral("joinMiter"))->setChecked(e.style().joinStyle == Qt::MiterJoin);
        m_arrowRow->setVisible(tool == AnnotationEditor::Tool::Arrow || tool == AnnotationEditor::Tool::Line);
        m_arrowStart->setCurrentIndex(static_cast<int>(e.arrowHeadStart()));
        m_arrowEnd->setCurrentIndex(static_cast<int>(e.arrowHeadEnd()));
        m_stepRow->setVisible(tool == AnnotationEditor::Tool::Step || selectedType == QLatin1String("step"));
        m_stepStyle->setCurrentIndex(static_cast<int>(e.stepStyle()));
        m_stepValue->setVisible(selectedType == QLatin1String("step"));
        m_stepRow->findChild<QLabel*>()->setVisible(selectedType == QLatin1String("step"));
        if (const auto* step = dynamic_cast<const StepAnnotation*>(selected))
            m_stepValue->setValue(step->number());
        m_magnifierRow->setVisible(tool == AnnotationEditor::Tool::Magnifier || selectedType == QLatin1String("magnifier"));
        m_zoom->setValue(static_cast<int>(std::round(e.magnifierZoom())));
        m_magnifierRow->findChild<QToolButton*>(QStringLiteral("includeBelow"))->setChecked(e.magnifierIncludeAnnotations());
        m_magnifierRow->findChild<QToolButton*>(QStringLiteral("magShadow"))->setChecked(e.magnifierShadow());
        const bool shapes = tool == AnnotationEditor::Tool::Highlighter || tool == AnnotationEditor::Tool::Mosaic || tool == AnnotationEditor::Tool::Blur || tool == AnnotationEditor::Tool::Eraser
            || tool == AnnotationEditor::Tool::Magnifier || tool == AnnotationEditor::Tool::Spotlight || tool == AnnotationEditor::Tool::SmartErase;
        m_shapeRow->setVisible(shapes);
        if (shapes) {
            int idx = 0;
            if (tool == AnnotationEditor::Tool::Highlighter)
                idx = e.highlighterShape() == HighlighterAnnotation::Shape::Brush ? 0 : 1;
            else if (tool == AnnotationEditor::Tool::Eraser)
                idx = static_cast<int>(e.eraserShape());
            else if (tool == AnnotationEditor::Tool::Magnifier)
                idx = e.magnifierShape() == MagnifierAnnotation::Shape::Ellipse ? 2 : 1;
            else if (tool == AnnotationEditor::Tool::Spotlight)
                idx = e.spotlightShape() == SpotlightAnnotation::Shape::Ellipse ? 2 : 1;
            else
                idx = static_cast<int>(e.effectShape());
            m_shapeRow->findChild<QToolButton*>(QStringLiteral("shape_brush"))->setChecked(idx == 0);
            m_shapeRow->findChild<QToolButton*>(QStringLiteral("shape_rect"))->setChecked(idx == 1);
            m_shapeRow->findChild<QToolButton*>(QStringLiteral("shape_ellipse"))->setChecked(idx == 2);
            m_shapeRow->findChild<QToolButton*>(QStringLiteral("shape_ellipse"))->setVisible(tool != AnnotationEditor::Tool::Highlighter);
            m_shapeRow->findChild<QToolButton*>(QStringLiteral("shape_brush"))->setVisible(tool != AnnotationEditor::Tool::Magnifier && tool != AnnotationEditor::Tool::Spotlight && tool != AnnotationEditor::Tool::SmartErase);
        }
        m_strengthRow->setVisible(tool == AnnotationEditor::Tool::Mosaic || tool == AnnotationEditor::Tool::Blur);
        m_strength->setValue(e.effectStrength());
        m_textRow->setVisible(tool == AnnotationEditor::Tool::Text || selectedType == QLatin1String("text"));
        if (m_textRow->isVisibleTo(m_options)) {
            m_fontCombo->setCurrentFont(e.textFont());
            m_fontSize->setValue(std::max(6, e.textFont().pixelSize()));
            m_textRow->findChild<QToolButton*>(QStringLiteral("bold"))->setChecked(e.textFont().bold());
            m_textRow->findChild<QToolButton*>(QStringLiteral("italic"))->setChecked(e.textFont().italic());
            m_textRow->findChild<QToolButton*>(QStringLiteral("outline"))->setChecked(e.textDecor().outline);
            m_textRow->findChild<QToolButton*>(QStringLiteral("background"))->setChecked(e.textDecor().background);
            m_textRow->findChild<QToolButton*>(QStringLiteral("textShadow"))->setChecked(e.textDecor().shadow);
        }
    }
    adjustSize();
    m_syncing = false;
}

QToolButton* CaptureToolbar::button(const QString& name) const
{
    return findChild<QToolButton*>(name);
}

} // namespace ss
