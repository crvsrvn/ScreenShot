#include "app/FloatingBar.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QToolButton>

namespace ss {

FloatingBar::FloatingBar(QWidget* parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAcceptDrops(true);
    setObjectName(QStringLiteral("floatingBar"));
    setStyleSheet(QStringLiteral("QWidget#floatingBar { background: #2b2b2b; border: 1px solid #555; border-radius: 6px; }"
                                 "QToolButton { color: white; padding: 4px 8px; border: none; border-radius: 3px; } QToolButton:hover { background: #3d6fb4; }"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(2);
    auto* grip = new QToolButton(this);
    grip->setText(QStringLiteral("☰"));
    grip->setObjectName(QStringLiteral("grip"));
    grip->setToolTip(tr("Drag to move; drop images or text here to pin them"));
    grip->setAttribute(Qt::WA_TransparentForMouseEvents);
    layout->addWidget(grip);
    auto* capture = new QToolButton(this);
    capture->setObjectName(QStringLiteral("capture"));
    capture->setText(tr("Capture"));
    connect(capture, &QToolButton::clicked, this, &FloatingBar::captureRequested);
    layout->addWidget(capture);
    auto* pin = new QToolButton(this);
    pin->setObjectName(QStringLiteral("pin"));
    pin->setText(tr("Pin"));
    connect(pin, &QToolButton::clicked, this, &FloatingBar::pinRequested);
    layout->addWidget(pin);
    adjustSize();
}

void FloatingBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - pos();
    }
}

void FloatingBar::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging)
        move(e->globalPosition().toPoint() - m_dragOffset);
}

void FloatingBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        emit moved(pos());
    }
}

void FloatingBar::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasImage() || e->mimeData()->hasUrls() || e->mimeData()->hasText())
        e->acceptProposedAction();
}

void FloatingBar::dropEvent(QDropEvent* e)
{
    // 贴在条的下方（物理坐标按所在屏幕缩放换算）
    QScreen* s = screen();
    const qreal dpr = s ? s->devicePixelRatio() : 1.0;
    const QPoint origin = s ? s->geometry().topLeft() : QPoint();
    const QPoint logical = pos() + QPoint(0, height() + 8);
    emit dropped(e->mimeData(), origin + (logical - origin) * dpr);
    e->acceptProposedAction();
}

} // namespace ss
