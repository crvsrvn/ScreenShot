#include "app/PinWindow.h"

#include "core/fx/Blur.h"
#include "core/util/Clock.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

namespace ss {

PinWindow::PinWindow(const QImage& image)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_mipmap(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMouseTracking(true);
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(150);
    connect(&m_settleTimer, &QTimer::timeout, this, &PinWindow::finishInteraction);
    relayout(QPointF(), QPointF());
}

QSizeF PinWindow::contentSize() const
{
    const QImage& img = m_mipmap.original();
    return QSizeF(img.width() * m_scale, img.height() * m_scale);
}

void PinWindow::relayout(QPointF keepAnchorLogical, QPointF keepAnchorRatio)
{
    const QSizeF cs = contentSize();
    const QSize winSize(static_cast<int>(std::ceil(cs.width())) + 2 * kShadowMargin,
                        static_cast<int>(std::ceil(cs.height())) + 2 * kShadowMargin);
    QPoint pos = this->pos();
    if (!keepAnchorRatio.isNull()) {
        // 保持锚点在内容中的相对位置不变
        const QPointF newAnchor(kShadowMargin + keepAnchorRatio.x() * cs.width(),
                                kShadowMargin + keepAnchorRatio.y() * cs.height());
        pos += (keepAnchorLogical - newAnchor).toPoint();
    }
    setGeometry(QRect(pos, winSize));
}

void PinWindow::moveToPhysical(QPoint physicalTopLeft)
{
    // 原型阶段按逻辑坐标放置；精确物理定位在 M3 做
    move(physicalTopLeft - QPoint(kShadowMargin, kShadowMargin));
}

void PinWindow::setScale(qreal scale, bool interactive)
{
    zoomAt(QPointF(kShadowMargin, kShadowMargin), scale / m_scale, interactive);
}

void PinWindow::zoomAt(QPointF anchorLogical, qreal factor, bool interactive)
{
    const qreal next = std::clamp(m_scale * factor, 0.05, 32.0);
    if (qFuzzyCompare(next, m_scale))
        return;
    const QSizeF before = contentSize();
    const QPointF ratio((anchorLogical.x() - kShadowMargin) / before.width(),
                        (anchorLogical.y() - kShadowMargin) / before.height());
    m_scale = next;
    m_interactive = interactive;
    m_smooth = QPixmap();
    relayout(anchorLogical, ratio);
    if (interactive)
        m_settleTimer.start();
    update();
}

void PinWindow::finishInteraction()
{
    m_interactive = false;
    update();
}

void PinWindow::ensureShadow()
{
    const QSize cs = contentSize().toSize();
    if (m_shadowFor == cs && !m_shadow.isNull())
        return;
    // 阴影：内容矩形的 alpha 蒙版做高斯模糊，按内容尺寸缓存，只在尺寸变化时重算
    const int blurRadius = 12;
    QImage mask(cs + QSize(2 * kShadowMargin, 2 * kShadowMargin), QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.fillRect(QRect(QPoint(kShadowMargin, kShadowMargin + 4), cs), QColor(0, 0, 0, 150));
    }
    m_shadow = QPixmap::fromImage(fx::gaussianBlur(mask, blurRadius));
    m_shadowFor = cs;
}

void PinWindow::paintEvent(QPaintEvent*)
{
    const qint64 t0 = monotonicNs();
    QPainter p(this);
    ensureShadow();
    p.drawPixmap(0, 0, m_shadow);

    const QRectF target(QPointF(kShadowMargin, kShadowMargin), contentSize());
    if (m_interactive || m_mipmap.original().width() * m_scale > 8192 || m_mipmap.original().height() * m_scale > 8192) {
        // 交互中：取最接近的缩小级别 + 快速变换
        const QImage& level = m_mipmap.levelForScale(m_scale);
        p.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1.0);
        p.drawImage(target, level);
    } else {
        if (m_smooth.isNull()) {
            const QImage& level = m_mipmap.levelForScale(m_scale);
            m_smooth = QPixmap::fromImage(level.scaled(target.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
        p.drawPixmap(target.topLeft(), m_smooth);
    }
    // 激活边框
    QPen pen(QColor(40, 120, 220, 200));
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(target.adjusted(0, 0, -1, -1));
    p.end();

    ++m_paintCount;
    m_lastPaintNs = monotonicNs();
    m_paintDurationsMs.append(nsToMs(m_lastPaintNs - t0));
}

QList<double> PinWindow::takePaintDurationsMs()
{
    QList<double> out;
    out.swap(m_paintDurationsMs);
    return out;
}

void PinWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - pos();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging)
        move(event->globalPosition().toPoint() - m_dragOffset); // 只移动窗口，不重绘
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent*)
{
    hide();
    emit closed();
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    zoomAt(event->position(), std::pow(1.1, steps), true);
}

void PinWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
    } else {
        QWidget::keyPressEvent(event);
    }
}

} // namespace ss
