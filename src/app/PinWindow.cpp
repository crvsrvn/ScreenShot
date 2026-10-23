#include "app/PinWindow.h"

#include "core/fx/Blur.h"
#include "core/util/Clock.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <cmath>

namespace ss {

namespace {

constexpr int kMargin = 24;

// 九宫格阴影：一次性生成一小块模糊图，画的时候拉伸四边、填充中心
const QPixmap& shadowTile()
{
    static const QPixmap tile = [] {
        const int core = 2 * kMargin;
        const int size = core + 2 * kMargin;
        QImage mask(size, size, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter p(&mask);
            p.fillRect(QRect(kMargin, kMargin + 4, core, core), QColor(0, 0, 0, 150));
        }
        return QPixmap::fromImage(fx::gaussianBlur(mask, 12));
    }();
    return tile;
}

} // namespace

PinWindow::PinWindow(const QImage& image)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_mipmap(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(150);
    connect(&m_settleTimer, &QTimer::timeout, this, &PinWindow::finishInteraction);
    relayout();
}

QSizeF PinWindow::contentSize() const
{
    const QImage& img = m_mipmap.original();
    return QSizeF(img.width() * m_scale, img.height() * m_scale);
}

QRectF PinWindow::contentRect() const
{
    return QRectF(m_contentTopLeft, contentSize());
}

QRectF PinWindow::desktopBounds() const
{
    QRectF bounds;
    for (QScreen* s : QGuiApplication::screens())
        bounds = bounds.united(QRectF(s->geometry()));
    return bounds;
}

void PinWindow::relayout()
{
    const QRectF full = contentRect().adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const QRectF desktop = desktopBounds();
    QRectF window = desktop.isEmpty() ? full : full.intersected(desktop);
    if (window.isEmpty())
        window = QRectF(full.topLeft(), QSizeF(1, 1)); // 完全在屏幕外：保留一个最小窗口
    m_clipped = window != full;
    const QRect target = window.toAlignedRect();
    if (target.size() == size())
        move(target.topLeft()); // 尺寸不变：只移动，不重绘
    else
        setGeometry(target);
    if (m_clipped)
        update(); // 可见部分变了
}

void PinWindow::moveContentTo(QPointF logicalTopLeft)
{
    m_contentTopLeft = logicalTopLeft;
    relayout();
}

void PinWindow::moveToPhysical(QPoint physicalTopLeft)
{
    // 原型阶段按逻辑坐标放置；精确物理定位在 M3 做
    moveContentTo(QPointF(physicalTopLeft));
}

void PinWindow::setScale(qreal scale, bool interactive)
{
    zoomAt(contentRect().topLeft() - pos(), scale / m_scale, interactive);
}

void PinWindow::zoomAt(QPointF anchorLogical, qreal factor, bool interactive)
{
    const qreal next = std::clamp(m_scale * factor, 0.02, 32.0);
    if (qFuzzyCompare(next, m_scale))
        return;
    // 锚点在内容中的相对位置保持不变
    const QRectF before = contentRect();
    const QPointF anchorDesktop = anchorLogical + pos();
    const QPointF ratio((anchorDesktop.x() - before.left()) / before.width(), (anchorDesktop.y() - before.top()) / before.height());
    m_scale = next;
    m_interactive = interactive;
    m_smooth = QPixmap();
    const QSizeF after = contentSize();
    m_contentTopLeft = anchorDesktop - QPointF(ratio.x() * after.width(), ratio.y() * after.height());
    relayout();
    if (interactive)
        m_settleTimer.start();
    update();
}

void PinWindow::fitToScreen()
{
    QScreen* screen = QGuiApplication::screenAt(contentRect().center().toPoint());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const QSizeF avail = QSizeF(screen->availableGeometry().size()) * 0.9;
    const QSizeF cs = contentSize();
    if (cs.width() <= avail.width() && cs.height() <= avail.height())
        return;
    const qreal factor = std::min(avail.width() / cs.width(), avail.height() / cs.height());
    zoomAt(QPointF(kMargin, kMargin), factor, false);
}

void PinWindow::finishInteraction()
{
    m_interactive = false;
    update();
}

void PinWindow::drawShadow(QPainter& p, const QRectF& content)
{
    const QPixmap& tile = shadowTile();
    const int t = tile.width();
    const int c = kMargin + 8; // 四角保留的像素（含模糊过渡）
    const QRectF outer = content.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const qreal L = outer.left(), T = outer.top(), R = outer.right(), B = outer.bottom();
    const qreal w = outer.width(), h = outer.height();
    if (w < 2 * c || h < 2 * c) {
        p.drawPixmap(outer, tile, tile.rect());
        return;
    }
    // 四角
    p.drawPixmap(QRectF(L, T, c, c), tile, QRectF(0, 0, c, c));
    p.drawPixmap(QRectF(R - c, T, c, c), tile, QRectF(t - c, 0, c, c));
    p.drawPixmap(QRectF(L, B - c, c, c), tile, QRectF(0, t - c, c, c));
    p.drawPixmap(QRectF(R - c, B - c, c, c), tile, QRectF(t - c, t - c, c, c));
    // 四边
    p.drawPixmap(QRectF(L + c, T, w - 2 * c, c), tile, QRectF(c, 0, t - 2 * c, c));
    p.drawPixmap(QRectF(L + c, B - c, w - 2 * c, c), tile, QRectF(c, t - c, t - 2 * c, c));
    p.drawPixmap(QRectF(L, T + c, c, h - 2 * c), tile, QRectF(0, c, c, t - 2 * c));
    p.drawPixmap(QRectF(R - c, T + c, c, h - 2 * c), tile, QRectF(t - c, c, c, t - 2 * c));
    // 中心（被内容盖住，只需填充与中心像素相同的颜色）
    p.fillRect(QRectF(L + c, T + c, w - 2 * c, h - 2 * c), QColor(0, 0, 0, 150));
}

void PinWindow::paintEvent(QPaintEvent*)
{
    const qint64 t0 = monotonicNs();
    QPainter p(this);
    const QRectF content = contentRect().translated(-pos());
    drawShadow(p, content);

    const QRectF desktop = desktopBounds();
    const bool huge = content.width() * content.height() > desktop.width() * desktop.height() * 1.5;
    if (m_interactive || qFuzzyCompare(m_scale, 1.0) || huge) {
        // 交互中、1:1 或超大：直接从最接近的级别绘制（画家只处理可见区域）
        const QImage& level = m_mipmap.levelForScale(m_scale);
        p.setRenderHint(QPainter::SmoothPixmapTransform, !m_interactive && !qFuzzyCompare(m_scale, 1.0));
        p.drawImage(content, level);
    } else {
        if (m_smooth.isNull()) {
            const QImage& level = m_mipmap.levelForScale(m_scale);
            m_smooth = QPixmap::fromImage(level.scaled(content.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
        p.drawPixmap(content.topLeft(), m_smooth);
    }
    QPen pen(QColor(40, 120, 220, 200));
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(content.adjusted(0, 0, -1, -1));
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
        m_dragOffset = event->globalPosition() - m_contentTopLeft;
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging)
        moveContentTo(event->globalPosition() - m_dragOffset);
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
