#include "app/PreferencesWidgets.h"

#include <QAbstractItemView>
#include <QHash>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

namespace ss::prefs {

namespace {

QColor blend(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QString css(const QColor& c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 3);
}

} // namespace

Colors Colors::make(const QColor& accent)
{
    Colors c;
    c.windowTop = QColor(0xfc, 0xf6, 0xda);
    c.windowBottom = QColor(0xf3, 0xd8, 0x86);
    c.panel = QColor(255, 253, 242, 140);
    c.panelBorder = QColor(190, 160, 70, 90);
    c.card = QColor(0xff, 0xfd, 0xf4);
    c.cardEnd = QColor(0xfd, 0xf1, 0xc6);
    c.cardBorder = QColor(0xd9, 0xbf, 0x6b);
    c.divider = QColor(190, 160, 70, 72);
    c.text = QColor(0x2e, 0x25, 0x00);
    c.secondary = QColor(0x7a, 0x64, 0x22);
    c.control = QColor(255, 254, 248, 235);
    c.controlHover = QColor(0xff, 0xf8, 0xdf);
    c.controlBorder = QColor(0xe2, 0xd6, 0xa8);
    c.hover = QColor(140, 105, 0, 18);
    c.selected = QColor(140, 105, 0, 28);
    c.switchOff = QColor(0xe6, 0xda, 0xae);
    c.scroll = QColor(120, 95, 20, 70);
    c.accent = accent;
    c.accent.setAlpha(255);
    return c;
}

QString styleSheet(const Colors& c)
{
    QString s = QStringLiteral(R"(
QDialog { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 @windowTop, stop:1 @windowBottom); }
QWidget { font-size: 13px; color: @text; }
QWidget#sidebar, QListWidget#nav { background: transparent; border: none; outline: 0; }
QStackedWidget#content {
    background: @panel; border: 1px solid @panelBorder; border-right: none; border-bottom: none;
    border-top-left-radius: 8px;
}
QScrollArea, QScrollArea > QWidget, QWidget#page { background: transparent; border: none; }
QLabel#pageTitle { font-size: 26px; padding-bottom: 6px; }
QLabel#rowTitle { font-size: 14px; font-weight: 600; }
QLabel#rowDesc { color: @secondary; }
QLabel#empty { font-size: 14px; color: @secondary; }
QLineEdit, QAbstractSpinBox {
    background: @control; border: 1px solid @controlBorder; border-bottom-color: @cardBorder; border-radius: 5px;
    padding: 4px 8px; min-height: 20px; selection-background-color: @accent; selection-color: white;
}
QLineEdit:hover, QAbstractSpinBox:hover { background: @controlHover; }
QLineEdit:focus, QAbstractSpinBox:focus { background: @control; border-bottom: 2px solid @accent; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { subcontrol-origin: border; width: 0; height: 0; border: none; }
QPushButton {
    background: @control; border: 1px solid @controlBorder; border-bottom-color: @cardBorder; border-radius: 5px;
    padding: 5px 12px; min-height: 18px;
}
QPushButton:hover { background: @controlHover; }
QPushButton:pressed { background: @selected; }
QComboBox QAbstractItemView {
    background: @card; border: 1px solid @controlBorder; padding: 4px; outline: 0;
    selection-background-color: @selected; selection-color: @text;
}
QComboBox QAbstractItemView::item { min-height: 28px; padding: 0 8px; border-radius: 4px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @scroll; border-radius: 3px; min-height: 32px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QToolTip { background: @card; color: @text; border: 1px solid @cardBorder; padding: 6px 8px; }
)");
    const QHash<QString, QColor> tokens{
        {QStringLiteral("@windowTop"), c.windowTop}, {QStringLiteral("@windowBottom"), c.windowBottom},
        {QStringLiteral("@panelBorder"), c.panelBorder}, {QStringLiteral("@panel"), c.panel},
        {QStringLiteral("@cardBorder"), c.cardBorder}, {QStringLiteral("@card"), c.card},
        {QStringLiteral("@text"), c.text}, {QStringLiteral("@secondary"), c.secondary},
        {QStringLiteral("@controlHover"), c.controlHover}, {QStringLiteral("@controlBorder"), c.controlBorder},
        {QStringLiteral("@control"), c.control}, {QStringLiteral("@selected"), c.selected},
        {QStringLiteral("@scroll"), c.scroll}, {QStringLiteral("@accent"), c.accent},
    };
    // 长名字先替换（@cardBorder 先于 @card，@controlBorder 先于 @control）
    QStringList names = tokens.keys();
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& name : names)
        s.replace(name, css(tokens.value(name)));
    return s;
}

NavDelegate::NavDelegate(const Colors& colors, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_colors(colors)
{
}

void NavDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(option.rect).adjusted(0, 2, 0, -2);
    const bool selected = option.state & QStyle::State_Selected;
    if (selected || option.state & QStyle::State_MouseOver) {
        p->setPen(Qt::NoPen);
        p->setBrush(selected ? m_colors.selected : m_colors.hover);
        p->drawRoundedRect(r, 5, 5);
    }
    if (selected) {
        p->setBrush(m_colors.accent);
        p->drawRoundedRect(QRectF(r.left(), r.center().y() - 8, 3, 16), 1.5, 1.5);
    }
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QRect iconRect(option.rect.left() + 14, option.rect.center().y() - 9, 18, 18);
    icon.paint(p, iconRect);
    p->setPen(m_colors.text);
    p->setFont(option.font);
    p->drawText(option.rect.adjusted(44, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
    p->restore();
}

QSize NavDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    return {QStyledItemDelegate::sizeHint(option, index).width(), 40};
}

SwitchButton::SwitchButton(const Colors& colors, QWidget* parent)
    : QCheckBox(parent)
    , m_colors(colors)
{
    setCursor(Qt::PointingHandCursor);
    m_anim.setDuration(140);
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(&m_anim, &QVariantAnimation::valueChanged, this, [this] { update(); });
    QObject::connect(this, &QCheckBox::toggled, this, [this](bool on) {
        if (!isVisible())
            return;
        m_anim.stop();
        m_anim.setStartValue(on ? 0.0 : 1.0);
        m_anim.setEndValue(on ? 1.0 : 0.0);
        m_anim.start();
    });
}

QSize SwitchButton::sizeHint() const
{
    return {40, 24};
}

bool SwitchButton::hitButton(const QPoint& pos) const
{
    return rect().contains(pos);
}

void SwitchButton::paintEvent(QPaintEvent*)
{
    const qreal t = m_anim.state() == QAbstractAnimation::Running ? m_anim.currentValue().toReal() : (isChecked() ? 1.0 : 0.0);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        p.setOpacity(0.45);
    const QRectF track(0, (height() - 22) / 2.0, 38, 22);
    p.setPen(Qt::NoPen);
    p.setBrush(blend(m_colors.switchOff, m_colors.accent, t));
    p.drawRoundedRect(track, 11, 11);
    const QPointF center(track.left() + 11 + t * 16, track.center().y());
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawEllipse(center + QPointF(0, 0.8), 9.5, 9.5);
    p.setBrush(Qt::white);
    p.drawEllipse(center, 9, 9);
}

ComboBox::ComboBox(const Colors& colors, QWidget* parent)
    : QComboBox(parent)
    , m_colors(colors)
{
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedHeight(30);
    setItemDelegate(new QStyledItemDelegate(this)); // 让样式表的 item 行高与圆角生效
}

void ComboBox::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!isEnabled())
        p.setOpacity(0.45);
    p.setPen(QPen(m_colors.controlBorder, 1));
    p.setBrush(underMouse() ? m_colors.controlHover : m_colors.control);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
    // 底边加深；聚焦或弹出时为主题色（与输入框一致）
    const bool active = hasFocus() || view()->isVisible();
    const qreal edge = active ? 2 : 1;
    p.fillRect(QRectF(4, height() - edge, width() - 8, edge), active ? m_colors.accent : m_colors.cardBorder);

    const QRect textRect = rect().adjusted(10, 0, -28, 0);
    p.setPen(m_colors.text);
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, fontMetrics().elidedText(currentText(), Qt::ElideRight, textRect.width()));

    const QPointF c(width() - 15, height() / 2.0);
    QPainterPath chevron;
    chevron.moveTo(c + QPointF(-4, -2));
    chevron.lineTo(c + QPointF(0, 2));
    chevron.lineTo(c + QPointF(4, -2));
    p.setPen(QPen(m_colors.text, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(chevron);
}

void ComboBox::wheelEvent(QWheelEvent* event)
{
    if (hasFocus())
        QComboBox::wheelEvent(event);
    else
        event->ignore();
}

Card::Card(const Colors& colors, QWidget* parent)
    : QWidget(parent)
    , m_colors(colors)
{
}

void Card::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QLinearGradient fill(box.topLeft(), box.bottomRight());
    fill.setColorAt(0, m_colors.card);
    fill.setColorAt(1, m_colors.cardEnd);
    p.setPen(QPen(m_colors.cardBorder, 1));
    p.setBrush(fill);
    p.drawRoundedRect(box, 6, 6);

    if (!layout())
        return;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(m_colors.divider);
    bool first = true;
    for (int i = 0; i < layout()->count(); ++i) {
        QWidget* row = layout()->itemAt(i)->widget();
        if (!row || row->isHidden())
            continue;
        if (!first)
            p.drawLine(16, row->y(), width() - 17, row->y());
        first = false;
    }
}

} // namespace ss::prefs
