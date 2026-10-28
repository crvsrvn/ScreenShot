#include "app/PreferencesWidgets.h"

#include "app/Theme.h"

#include <QLayout>
#include <QPainter>
#include <QPainterPath>

namespace ss::prefs {

namespace {

QColor blend(const QColor& a, const QColor& b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

QString css(const QColor& c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

} // namespace

QString styleSheet()
{
    const theme::Colors& c = theme::colors();
    return QStringLiteral(R"(
QListWidget#nav { background: transparent; border: none; outline: 0; }
QWidget#page { background: transparent; }
QLabel#pageTitle { font-size: 24px; font-weight: 600; color: %1; }
QLabel#rowTitle { font-weight: 600; color: %1; }
QLabel#rowDesc, QLabel#status, QLabel#empty { color: %2; }
)")
        .arg(css(c.text), css(c.muted));
}

void NavDelegate::paint(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const theme::Colors& c = theme::colors();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(option.rect).adjusted(4, 2, -4, -2);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    if (selected || hover) {
        p->setPen(Qt::NoPen);
        p->setBrush(selected && hover ? c.subtlePressed : c.subtleHover);
        p->drawRoundedRect(r, 4, 4);
    }
    if (selected) {
        p->setPen(Qt::NoPen);
        p->setBrush(c.systemAccent);
        p->drawRoundedRect(QRectF(r.left(), r.center().y() - 8, 3, 16), 1.5, 1.5);
    }
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    icon.paint(p, QRect(option.rect.left() + 16, option.rect.center().y() - 7, 16, 16));
    p->setPen(c.controlText);
    p->setFont(option.font);
    p->drawText(option.rect.adjusted(48, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
    p->restore();
}

QSize NavDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    return {QStyledItemDelegate::sizeHint(option, index).width(), 40};
}

SwitchButton::SwitchButton(QWidget* parent)
    : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
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
    return {42, 24};
}

bool SwitchButton::hitButton(const QPoint& pos) const
{
    return rect().contains(pos);
}

void SwitchButton::paintEvent(QPaintEvent*)
{
    // WinUI ToggleSwitch：40×20 轨道；关为描边 + 深色小圆点，开为强调色底 + 白色圆点；悬停时圆点放大
    const theme::Colors& c = theme::colors();
    const qreal t = m_anim.state() == QAbstractAnimation::Running ? m_anim.currentValue().toReal() : (isChecked() ? 1.0 : 0.0);
    const bool hover = underMouse() && isEnabled();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track(0.5, (height() - 20) / 2.0 + 0.5, 39, 19);
    const QColor accent = isEnabled() ? c.systemAccent : c.accentDisabled;
    const QColor off = hover ? c.subtleHover : c.altFill;
    p.setPen(t < 1 ? QPen(blend(isEnabled() ? c.strongStroke : c.strongStrokeDisabled, accent, t), 1) : Qt::NoPen);
    p.setBrush(blend(off, accent, t));
    p.drawRoundedRect(track, 9.5, 9.5);
    const qreal radius = hover ? 7 : 6;
    const QPointF center(track.left() + 10 + t * 19, track.center().y());
    const QColor knobOff = isEnabled() ? c.controlTextSecondary : c.disabledText;
    p.setPen(Qt::NoPen);
    p.setBrush(blend(knobOff, c.dark ? Qt::black : Qt::white, t));
    p.drawEllipse(center, radius, radius);
}

void Card::paintEvent(QPaintEvent*)
{
    const theme::Colors& c = theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(c.border, 1));
    p.setBrush(theme::gradient(box, c.surface, 2));
    p.drawRoundedRect(box, 6, 6);

    if (!layout())
        return;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(c.border);
    bool first = true;
    for (int i = 0; i < layout()->count(); ++i) {
        QWidget* row = layout()->itemAt(i)->widget();
        if (!row || row->isHidden())
            continue;
        if (!first)
            p.drawLine(18, row->y(), width() - 19, row->y());
        first = false;
    }
}

void ContentPanel::paintEvent(QPaintEvent*)
{
    const theme::Colors& c = theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    // 只有左上角是圆角：右、下边画到窗口外
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, 20, 20);
    QPainterPath path;
    path.addRoundedRect(box, 8, 8);
    p.setPen(QPen(c.layerStroke, 1));
    p.setBrush(c.layer);
    p.drawPath(path);
    if (m_header > 0) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(c.border);
        p.drawLine(1, m_header - 1, width(), m_header - 1);
    }
}

void StatusBar::paintEvent(QPaintEvent*)
{
    const theme::Colors& c = theme::colors();
    QPainter p(this);
    p.fillRect(rect(), theme::gradient(QRectF(rect()), c.subtle, 2));
    p.setPen(c.border);
    p.drawLine(0, 0, width(), 0);
}

} // namespace ss::prefs
