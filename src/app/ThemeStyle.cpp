#include "app/ThemeStyle.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QHash>
#include <QItemDelegate>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <algorithm>

namespace ss::theme {

namespace {

QString css(const QColor& c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString cssGradient(const QColor* stops, int count)
{
    QString s = QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1").arg(css(stops[0]));
    if (count == 3)
        s += QStringLiteral(", stop:0.56 %1").arg(css(stops[1]));
    return s + QStringLiteral(", stop:1 %1)").arg(css(stops[count - 1]));
}

// 细箭头（Fluent 的 Chevron）：朝下 / 上 / 左 / 右
void drawChevron(QPainter* p, const QRectF& r, Qt::ArrowType dir, const QColor& color)
{
    const QPointF c = r.center();
    const qreal w = std::min<qreal>(4.5, std::min(r.width(), r.height()) / 2 - 1);
    const qreal h = w / 2;
    QPainterPath path;
    switch (dir) {
    case Qt::UpArrow:
        path.moveTo(c + QPointF(-w, h));
        path.lineTo(c + QPointF(0, -h));
        path.lineTo(c + QPointF(w, h));
        break;
    case Qt::LeftArrow:
        path.moveTo(c + QPointF(h, -w));
        path.lineTo(c + QPointF(-h, 0));
        path.lineTo(c + QPointF(h, w));
        break;
    case Qt::RightArrow:
        path.moveTo(c + QPointF(-h, -w));
        path.lineTo(c + QPointF(h, 0));
        path.lineTo(c + QPointF(-h, w));
        break;
    default:
        path.moveTo(c + QPointF(-w, -h));
        path.lineTo(c + QPointF(0, h));
        path.lineTo(c + QPointF(w, -h));
        break;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    p->drawPath(path);
    p->restore();
}

void drawCheckMark(QPainter* p, const QRectF& box, const QColor& color, bool partial)
{
    p->setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    const qreal s = box.width() / 20;
    if (partial) {
        p->drawLine(box.topLeft() + QPointF(6 * s, 10 * s), box.topLeft() + QPointF(14 * s, 10 * s));
        return;
    }
    QPainterPath path;
    path.moveTo(box.topLeft() + QPointF(5.5 * s, 10.5 * s));
    path.lineTo(box.topLeft() + QPointF(8.5 * s, 13.5 * s));
    path.lineTo(box.topLeft() + QPointF(14.5 * s, 7 * s));
    p->drawPath(path);
}

// 样式表里的箭头只能用图片：按颜色画成 PNG（1x、2x）写到临时目录，返回 url() 用的路径
QString arrowImage(Qt::ArrowType dir, const QColor& color)
{
    const QString folder = QDir::tempPath() + QStringLiteral("/ScreenShot-theme");
    QDir().mkpath(folder);
    const QString base = QStringLiteral("%1/chevron-%2-%3").arg(folder).arg(static_cast<int>(dir)).arg(color.rgba(), 8, 16, QLatin1Char('0'));
    for (int scale : {1, 2}) {
        const QString path = base + (scale == 1 ? QStringLiteral(".png") : QStringLiteral("@2x.png"));
        if (QFile::exists(path))
            continue;
        QImage image(12 * scale, 12 * scale, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.scale(scale, scale);
        drawChevron(&p, QRectF(0, 0, 12, 12), dir, color);
        p.end();
        image.save(path);
    }
    return base + QStringLiteral(".png");
}

} // namespace

QString styleSheet(const Colors& c)
{
    QString s = QStringLiteral(R"(
QDialog { background: @background; }
QToolTip { background: @flyout; color: @controlText; border: 1px solid @flyoutStroke; padding: 5px 8px; }

QPushButton, QToolButton {
    background: @controlFill; color: @controlText;
    border: 1px solid @controlStroke; border-bottom-color: @controlStrokeBottom; border-radius: 4px;
}
QPushButton { padding: 5px 11px 6px 11px; min-height: 19px; }
QToolButton { padding: 5px; }
QPushButton:hover, QToolButton:hover { background: @controlFillHover; }
QPushButton:pressed, QToolButton:pressed { background: @controlFillPressed; color: @controlTextSecondary; border-bottom-color: @controlStroke; }
QPushButton:disabled, QToolButton:disabled { background: @controlFillDisabled; color: @disabledText; border-bottom-color: @controlStroke; }
QPushButton:default, QPushButton[primary="true"], QToolButton:checked {
    background: @accent; color: @onAccent; border: 1px solid @border;
}
QPushButton:default:hover, QPushButton[primary="true"]:hover, QToolButton:checked:hover { background: @accentHover; }
QPushButton:default:pressed, QPushButton[primary="true"]:pressed, QToolButton:checked:pressed { background: @accentPressed; color: @onAccent; }
QPushButton:default:disabled, QPushButton[primary="true"]:disabled { background: @accentDisabled; color: @onAccentDisabled; border-color: transparent; }
QToolButton::menu-indicator { image: none; width: 0; }
QLineEdit QToolButton, QAbstractSpinBox QToolButton, QTabBar QToolButton, QToolButton[flat="true"] {
    background: transparent; border: none; padding: 0;
}

QLineEdit, QAbstractSpinBox, QTextEdit, QPlainTextEdit {
    background: @controlFill; color: @controlText;
    border: 1px solid @controlStroke; border-bottom-color: @strongStroke; border-radius: 4px;
    selection-background-color: @systemAccent; selection-color: @onSystemAccent;
}
QLineEdit, QAbstractSpinBox { padding: 4px 10px 5px 10px; min-height: 20px; }
QTextEdit, QPlainTextEdit { padding: 2px 4px; }
QLineEdit:hover, QAbstractSpinBox:hover, QTextEdit:hover, QPlainTextEdit:hover { background: @controlFillHover; }
QLineEdit:focus, QAbstractSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {
    background: @inputActive; border-bottom: 2px solid @systemAccent;
}
QLineEdit:focus, QAbstractSpinBox:focus { padding-bottom: 4px; }
QLineEdit:disabled, QAbstractSpinBox:disabled, QTextEdit:disabled, QPlainTextEdit:disabled {
    background: @controlFillDisabled; color: @disabledText; border-bottom-color: @controlStroke;
}
QLineEdit[readOnly="true"] { background: @controlFillDisabled; border-bottom-color: @controlStroke; }
QAbstractSpinBox QLineEdit, QComboBox QLineEdit, QAbstractSpinBox QLineEdit:focus, QComboBox QLineEdit:focus {
    background: transparent; border: none; padding: 0; min-height: 0;
}
QAbstractSpinBox { padding-right: 22px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border; width: 20px; border: none; background: transparent;
}
QAbstractSpinBox::up-button { subcontrol-position: top right; margin: 3px 2px 0 0; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; margin: 0 2px 3px 0; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background: @subtleHover; border-radius: 3px; }
QAbstractSpinBox::up-arrow { image: url("@arrowUp"); width: 10px; height: 10px; }
QAbstractSpinBox::down-arrow { image: url("@arrowDown"); width: 10px; height: 10px; }
QAbstractSpinBox::up-arrow:disabled { image: url("@arrowOffUp"); }
QAbstractSpinBox::down-arrow:disabled { image: url("@arrowOffDown"); }

QComboBox {
    background: @controlFill; color: @controlText;
    border: 1px solid @controlStroke; border-bottom-color: @controlStrokeBottom; border-radius: 4px;
    padding: 4px 30px 5px 11px; min-height: 21px; combobox-popup: 0;
}
QComboBox:hover { background: @controlFillHover; }
QComboBox:on { background: @controlFillPressed; }
QComboBox:disabled { background: @controlFillDisabled; color: @disabledText; border-bottom-color: @controlStroke; }
QComboBox:editable { background: @controlFill; border-bottom-color: @strongStroke; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; width: 30px; border: none; background: transparent; }
QComboBox::down-arrow { image: url("@arrowDown"); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url("@arrowOffDown"); }
QComboBoxPrivateContainer { background: transparent; border: none; }
QComboBox QAbstractItemView {
    background: @flyout; color: @controlText; border: 1px solid @flyoutStroke; border-radius: 8px;
    padding: 3px; outline: 0; selection-background-color: @subtleHover; selection-color: @controlText;
}
QComboBox QAbstractItemView::item { min-height: 32px; padding: 0 11px; margin: 1px 0; border-radius: 4px; color: @controlText; }
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: @subtleHover; }

QCheckBox, QRadioButton { spacing: 8px; color: @controlText; }
QCheckBox:disabled, QRadioButton:disabled { color: @disabledText; }

QMenu {
    background: @flyout; color: @controlText; border: 1px solid @flyoutStroke; border-radius: 8px; padding: 3px 0;
}
QMenu::item { padding: 7px 28px 8px 12px; margin: 1px 4px; border-radius: 4px; background: transparent; }
QMenu::item:selected { background: @subtleHover; }
QMenu::item:disabled { color: @disabledText; }
QMenu::icon { padding-left: 12px; }
QMenu::separator { height: 1px; background: @controlStroke; margin: 3px 0; }
QMenu::right-arrow { width: 12px; height: 12px; right: 10px; }

QAbstractItemView {
    background: @controlFill; color: @controlText; border: 1px solid @controlStroke; border-radius: 4px; outline: 0;
    selection-background-color: @subtleHover; selection-color: @controlText; alternate-background-color: @subtlePressed;
}
QAbstractItemView::item { min-height: 28px; padding: 0 6px; border-radius: 4px; }
QAbstractItemView::item:hover, QAbstractItemView::item:selected { background: @subtleHover; color: @controlText; }
QHeaderView::section {
    background: transparent; color: @controlTextSecondary; border: none; border-bottom: 1px solid @controlStroke; padding: 4px 8px;
}

QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget#qt_scrollarea_viewport { background: transparent; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:vertical { background: @scrollThumb; border-radius: 3px; min-height: 32px; margin: 2px 3px; }
QScrollBar::handle:horizontal { background: @scrollThumb; border-radius: 3px; min-width: 32px; margin: 3px 2px; }
QScrollBar::handle:hover { background: @controlTextSecondary; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QSlider::groove:horizontal { height: 4px; background: @strongStroke; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @systemAccent; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 18px; height: 18px; margin: -8px 0; border-radius: 10px; border: 1px solid @controlStroke;
    background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 @systemAccent, stop:0.58 @systemAccent, stop:0.62 @thumb, stop:1 @thumb);
}
QSlider::handle:horizontal:disabled { background: @thumb; }

QProgressBar { background: @controlStrokeBottom; border: none; border-radius: 2px; color: @controlText; text-align: center; max-height: 4px; }
QProgressBar::chunk { background: @systemAccent; border-radius: 2px; }

QGroupBox {
    background: @surface; border: 1px solid @border; border-radius: 6px; margin-top: 26px; padding: 14px 12px 12px 12px;
}
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 2px; top: 2px; color: @text; font-weight: 600; }

QTabWidget::pane { border: none; }
QTabBar::tab { background: transparent; color: @controlTextSecondary; padding: 6px 12px; border: none; border-bottom: 3px solid transparent; }
QTabBar::tab:hover { color: @controlText; }
QTabBar::tab:selected { color: @controlText; border-bottom-color: @systemAccent; font-weight: 600; }

QFrame[frameShape="4"], QFrame[frameShape="5"], QFrame[frameShape="HLine"], QFrame[frameShape="VLine"] { border: none; background: @border; }
QFrame[frameShape="4"], QFrame[frameShape="HLine"] { max-height: 1px; min-height: 1px; }
QFrame[frameShape="5"], QFrame[frameShape="VLine"] { max-width: 1px; min-width: 1px; margin: 5px 3px; }

QMessageBox QLabel#qt_msgbox_label { min-width: 280px; }
)");
    const QHash<QString, QString> tokens{
        {QStringLiteral("@background"), cssGradient(c.background, 3)},
        {QStringLiteral("@surface"), cssGradient(c.surface, 2)},
        {QStringLiteral("@accent"), cssGradient(c.accent, 2)},
        {QStringLiteral("@accentHover"), cssGradient(c.accentHover, 2)},
        {QStringLiteral("@accentPressed"), cssGradient(c.accentPressed, 2)},
        {QStringLiteral("@accentDisabled"), css(c.accentDisabled)},
        {QStringLiteral("@onAccent"), css(c.onAccent)},
        {QStringLiteral("@onAccentDisabled"), css(c.dark ? QColor(255, 255, 255, 135) : QColor(Qt::white))},
        {QStringLiteral("@border"), css(c.border)},
        {QStringLiteral("@text"), css(c.text)},
        {QStringLiteral("@muted"), css(c.muted)},
        {QStringLiteral("@systemAccent"), css(c.systemAccent)},
        {QStringLiteral("@onSystemAccent"), css(c.dark ? QColor(Qt::black) : QColor(Qt::white))},
        {QStringLiteral("@controlText"), css(c.controlText)},
        {QStringLiteral("@controlTextSecondary"), css(c.controlTextSecondary)},
        {QStringLiteral("@disabledText"), css(c.disabledText)},
        {QStringLiteral("@controlFill"), css(c.controlFill)},
        {QStringLiteral("@controlFillHover"), css(c.controlFillHover)},
        {QStringLiteral("@controlFillPressed"), css(c.controlFillPressed)},
        {QStringLiteral("@controlFillDisabled"), css(c.controlFillDisabled)},
        {QStringLiteral("@inputActive"), css(c.inputActive)},
        {QStringLiteral("@controlStroke"), css(c.controlStroke)},
        {QStringLiteral("@controlStrokeBottom"), css(c.controlStrokeBottom)},
        {QStringLiteral("@strongStroke"), css(c.strongStroke)},
        {QStringLiteral("@subtleHover"), css(c.subtleHover)},
        {QStringLiteral("@subtlePressed"), css(c.subtlePressed)},
        {QStringLiteral("@flyout"), css(c.flyout)},
        {QStringLiteral("@flyoutStroke"), css(c.flyoutStroke)},
        {QStringLiteral("@scrollThumb"), css(c.disabledText)},
        {QStringLiteral("@arrowDown"), arrowImage(Qt::DownArrow, c.controlTextSecondary)},
        {QStringLiteral("@arrowUp"), arrowImage(Qt::UpArrow, c.controlTextSecondary)},
        {QStringLiteral("@arrowOffDown"), arrowImage(Qt::DownArrow, c.disabledText)},
        {QStringLiteral("@arrowOffUp"), arrowImage(Qt::UpArrow, c.disabledText)},
        {QStringLiteral("@thumb"), css(c.dark ? QColor(0x45, 0x45, 0x45) : QColor(Qt::white))},
    };
    // 长名字先替换（@accentHover 先于 @accent，@controlFillHover 先于 @controlFill）
    QStringList names = tokens.keys();
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& name : names)
        s.replace(name, tokens.value(name));
    return s;
}

void ThemeStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* p, const QWidget* widget) const
{
    const Colors& c = colors();
    const bool enabled = option->state & State_Enabled;
    const bool hover = enabled && (option->state & State_MouseOver);
    switch (element) {
    case PE_IndicatorArrowDown:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight: {
        const Qt::ArrowType dir = element == PE_IndicatorArrowUp ? Qt::UpArrow
            : element == PE_IndicatorArrowLeft                    ? Qt::LeftArrow
            : element == PE_IndicatorArrowRight                   ? Qt::RightArrow
                                                                  : Qt::DownArrow;
        drawChevron(p, option->rect, dir, enabled ? c.controlTextSecondary : c.disabledText);
        return;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorMenuCheckMark: {
        const bool on = option->state & (State_On | State_NoChange);
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (element == PE_IndicatorMenuCheckMark) {
            if (on)
                drawCheckMark(p, box, enabled ? c.controlText : c.disabledText, false);
            p->restore();
            return;
        }
        if (on) {
            p->setPen(Qt::NoPen);
            p->setBrush(enabled ? (hover ? c.systemAccent.lighter(110) : c.systemAccent) : c.accentDisabled);
            p->drawRoundedRect(box, 4, 4);
            drawCheckMark(p, box, c.dark ? Qt::black : Qt::white, option->state & State_NoChange);
        } else {
            p->setPen(QPen(enabled ? c.strongStroke : c.strongStrokeDisabled, 1));
            p->setBrush(hover ? c.subtleHover : c.altFill);
            p->drawRoundedRect(box, 4, 4);
        }
        p->restore();
        return;
    }
    case PE_IndicatorRadioButton: {
        const bool on = option->state & State_On;
        const QRectF box = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (on) {
            p->setPen(Qt::NoPen);
            p->setBrush(enabled ? c.systemAccent : c.accentDisabled);
            p->drawEllipse(box);
            p->setBrush(c.dark ? Qt::black : Qt::white);
            const qreal r = box.width() * (hover ? 0.3 : 0.24);
            p->drawEllipse(box.center(), r, r);
        } else {
            p->setPen(QPen(enabled ? c.strongStroke : c.strongStrokeDisabled, 1));
            p->setBrush(hover ? c.subtleHover : c.altFill);
            p->drawEllipse(box);
        }
        p->restore();
        return;
    }
    case PE_FrameFocusRect:
        return; // 与 WinUI 一致：鼠标操作不画虚线焦点框
    default:
        break;
    }
    QProxyStyle::drawPrimitive(element, option, p, widget);
}

int ThemeStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    switch (metric) {
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return 20;
    case PM_SmallIconSize:
        return 16;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int ThemeStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
{
    if (hint == SH_ComboBox_Popup)
        return 0; // 下拉列表在下方展开
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

void ThemeStyle::polish(QWidget* widget)
{
    QProxyStyle::polish(widget);
    // 菜单、下拉列表圆角：窗口须透明（与 Qt 的 Windows 11 样式做法相同）
    if (qobject_cast<QMenu*>(widget) || widget->inherits("QComboBoxPrivateContainer"))
        widget->setAttribute(Qt::WA_TranslucentBackground);
    // 样式表的 item 行高与圆角只对 QStyledItemDelegate 生效
    if (auto* combo = qobject_cast<QComboBox*>(widget); combo && qobject_cast<QItemDelegate*>(combo->itemDelegate()))
        combo->setItemDelegate(new QStyledItemDelegate(combo));
    if (qobject_cast<QComboBox*>(widget) || qobject_cast<QAbstractButton*>(widget))
        widget->setAttribute(Qt::WA_Hover);
}

} // namespace ss::theme
