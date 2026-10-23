#include "app/OverlayWindow.h"

#include "app/CaptureSession.h"
#include "core/util/Clock.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#if defined(Q_OS_WIN)
#include "platform/windows/WinWindowTools.h"
#elif defined(Q_OS_MACOS)
#include "platform/macos/MacWindowTools.h"
#endif

namespace ss {

namespace {

QScreen* findQScreen(const ScreenInfo& info)
{
    const auto screens = QGuiApplication::screens();
    for (QScreen* s : screens) {
        if (s->name() == info.id)
            return s;
    }
    // Qt 6 的逻辑屏幕原点与物理原点一致，按原点兜底匹配
    for (QScreen* s : screens) {
        if (s->geometry().topLeft() == info.physicalGeometry.topLeft())
            return s;
    }
    return QGuiApplication::primaryScreen();
}

} // namespace

OverlayWindow::OverlayWindow(CaptureSession* session)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_session(session)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setFocusPolicy(Qt::StrongFocus);
}

void OverlayWindow::setScreenInfo(const ScreenInfo& info)
{
    m_screen = info;
    if (QScreen* qs = findQScreen(info)) {
        if (windowHandle())
            windowHandle()->setScreen(qs);
        setGeometry(qs->geometry());
    }
}

void OverlayWindow::setSnapshot(const ScreenSnapshot& snapshot)
{
    m_image = snapshot.image;
    if (!qFuzzyCompare(m_image.devicePixelRatio(), m_screen.scale))
        m_image.setDevicePixelRatio(m_screen.scale); // 捕获实现未遵守约定时的兜底（会拷贝）
    m_awaitingFirstPaint = true;
    update();
}

void OverlayWindow::clearSnapshot()
{
    m_image = QImage();
}

void OverlayWindow::showOnScreen()
{
    QScreen* qs = findQScreen(m_screen);
    if (!qs)
        return;
    // 先确保原生窗口存在并落在目标显示器上，再显示，避免 Qt 按鼠标所在屏幕放置
    winId();
    windowHandle()->setScreen(qs);
    setGeometry(qs->geometry());
    show();
    raise();
#if defined(Q_OS_WIN)
    // 用系统接口按物理像素定位，消除逻辑尺寸取整带来的偏差（R-03）
    win::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_screen.physicalGeometry);
    const QPoint cursor = win::cursorPhysicalPos();
#elif defined(Q_OS_MACOS)
    mac::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_screen.physicalGeometry);
    const QPoint cursor = mac::cursorPhysicalPos();
#else
    const QPoint cursor = QCursor::pos();
#endif
    if (m_screen.physicalGeometry.contains(cursor)) // 鼠标所在屏幕取焦点
        activateWindow();
}

QPoint OverlayWindow::toPhysical(QPointF logical) const
{
    // 窗口物理原点 == 显示器物理原点；逻辑坐标 × 缩放后应为整数，用四舍五入消除浮点误差
    return m_screen.physicalGeometry.topLeft()
        + QPoint(qRound(logical.x() * m_screen.scale), qRound(logical.y() * m_screen.scale));
}

QRectF OverlayWindow::toLogical(const QRect& physical) const
{
    const QPoint rel = physical.topLeft() - m_screen.physicalGeometry.topLeft();
    return QRectF(rel.x() / m_screen.scale, rel.y() / m_screen.scale, physical.width() / m_screen.scale,
                  physical.height() / m_screen.scale);
}

void OverlayWindow::paintContent(QPainter& painter)
{
    if (m_image.isNull()) {
        painter.fillRect(rect(), Qt::black);
        return;
    }
    painter.drawImage(QPointF(0, 0), m_image);

    const QRect selection = m_session->selection();
    const QRect visible = selection.intersected(m_screen.physicalGeometry);
    const QColor dim(0, 0, 0, 110);
    if (visible.isEmpty()) {
        painter.fillRect(rect(), dim);
        return;
    }
    const QRectF sel = toLogical(visible);
    // 选区外四块遮罩
    const QRectF full(0, 0, width(), height());
    painter.fillRect(QRectF(0, 0, full.width(), sel.top()), dim);
    painter.fillRect(QRectF(0, sel.bottom(), full.width(), full.height() - sel.bottom()), dim);
    painter.fillRect(QRectF(0, sel.top(), sel.left(), sel.height()), dim);
    painter.fillRect(QRectF(sel.right(), sel.top(), full.width() - sel.right(), sel.height()), dim);

    // 边框与尺寸
    QPen pen(QColor(40, 120, 220));
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawRect(sel.adjusted(0, 0, -1.0 / m_screen.scale, -1.0 / m_screen.scale));
    if (selection.topLeft() == visible.topLeft()) {
        const QString text = QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height());
        QRectF label(sel.left(), sel.top() - 22, 120, 20);
        if (label.top() < 0)
            label.moveTop(sel.top() + 2);
        painter.fillRect(label, QColor(0, 0, 0, 160));
        painter.setPen(Qt::white);
        painter.drawText(label.adjusted(4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
    }
}

void OverlayWindow::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    paintContent(painter);
    painter.end();
    ++m_paintCount;
    if (m_awaitingFirstPaint) {
        m_awaitingFirstPaint = false;
        emit firstPainted(monotonicNs());
    }
}

QImage OverlayWindow::renderToImage()
{
    QImage img(m_screen.physicalGeometry.size(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(m_screen.scale);
    img.fill(Qt::transparent);
    QPainter painter(&img);
    paintContent(painter);
    return img;
}

void OverlayWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_awaitingFirstPaint = true;
}

void OverlayWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_session->beginDrag(toPhysical(event->position()));
    } else if (event->button() == Qt::RightButton) {
        if (m_session->selection().isEmpty())
            m_session->cancel();
        else
            m_session->setSelection(QRect());
    }
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
    if (m_session->dragging())
        m_session->dragTo(toPhysical(event->position()));
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_session->dragging())
        m_session->endDrag(toPhysical(event->position()));
}

void OverlayWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_session->selection().contains(toPhysical(event->position())))
        m_session->accept();
}

void OverlayWindow::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        m_session->cancel();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        m_session->accept();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

} // namespace ss
