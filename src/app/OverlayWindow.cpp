#include "app/OverlayWindow.h"

#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "core/capture/Magnifier.h"
#include "core/capture/SelectionGeometry.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWheelEvent>
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
    for (QScreen* s : screens) {
        if (s->geometry().topLeft() == info.physicalGeometry.topLeft())
            return s;
    }
    return QGuiApplication::primaryScreen();
}

const QColor kAccent(40, 120, 220);
const QColor kDim(0, 0, 0, 110);

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
    // 键盘移动光标的请求由会话发出；只由光标所在屏幕的覆盖窗口执行
    connect(session, &CaptureSession::cursorMoveRequested, this, [this](QPoint physical) {
        if (m_screen.physicalGeometry.contains(physical) && isVisible())
            moveCursorTo(physical);
    });
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
    winId();
    windowHandle()->setScreen(qs);
    setGeometry(qs->geometry());
    show();
    raise();
#if defined(Q_OS_WIN)
    win::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_screen.physicalGeometry);
    const QPoint cursor = win::cursorPhysicalPos();
#elif defined(Q_OS_MACOS)
    mac::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_screen.physicalGeometry);
    const QPoint cursor = mac::cursorPhysicalPos();
#else
    const QPoint cursor = QCursor::pos();
#endif
    if (m_screen.physicalGeometry.contains(cursor)) {
        activateWindow();
        setFocus();
        m_session->hoverAt(cursor);
    }
}

QPoint OverlayWindow::toPhysical(QPointF logical) const
{
    return m_screen.physicalGeometry.topLeft() + QPoint(qRound(logical.x() * m_screen.scale), qRound(logical.y() * m_screen.scale));
}

QRectF OverlayWindow::toLogical(const QRect& physical) const
{
    const QPoint rel = physical.topLeft() - m_screen.physicalGeometry.topLeft();
    return QRectF(rel.x() / m_screen.scale, rel.y() / m_screen.scale, physical.width() / m_screen.scale, physical.height() / m_screen.scale);
}

QRectF OverlayWindow::toLogicalGlobal(const QRect& physical) const
{
    return toLogical(physical).translated(geometry().topLeft());
}

void OverlayWindow::paintSelection(QPainter& p, const QRect& selection)
{
    const QRect visible = selection.intersected(m_screen.physicalGeometry);
    if (visible.isEmpty()) {
        p.fillRect(rect(), kDim);
        return;
    }
    const QRectF sel = toLogical(visible);
    const QRectF full(0, 0, width(), height());
    p.fillRect(QRectF(0, 0, full.width(), sel.top()), kDim);
    p.fillRect(QRectF(0, sel.bottom(), full.width(), full.height() - sel.bottom()), kDim);
    p.fillRect(QRectF(0, sel.top(), sel.left(), sel.height()), kDim);
    p.fillRect(QRectF(sel.right(), sel.top(), full.width() - sel.right(), sel.height()), kDim);

    QPen pen(kAccent);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(sel.adjusted(0, 0, -1.0 / m_screen.scale, -1.0 / m_screen.scale));

    // 锚点（CAP-10）：只在已选定状态画
    const auto state = m_session->state();
    if ((state == CaptureSession::State::Selected || state == CaptureSession::State::History) && !m_session->dragging()) {
        p.setBrush(Qt::white);
        for (const QPoint& hp : selectionHandlePoints(selection)) {
            if (!m_screen.physicalGeometry.adjusted(-8, -8, 8, 8).contains(hp))
                continue;
            const QPointF c = toLogical(QRect(hp, QSize(1, 1))).topLeft() + QPointF(0.5, 0.5);
            p.drawRect(QRectF(c.x() - 3.5, c.y() - 3.5, 7, 7));
        }
    }
    // 尺寸标签
    if (selection.topLeft() == visible.topLeft()) {
        const QString text = QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height());
        QRectF label(sel.left(), sel.top() - 22, 130, 20);
        if (label.top() < 0)
            label.moveTop(sel.top() + 2);
        p.fillRect(label, QColor(0, 0, 0, 160));
        p.setPen(Qt::white);
        p.drawText(label.adjusted(4, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text);
        m_sizeLabel = label;
    } else {
        m_sizeLabel = QRectF();
    }
    // 指针预览（CAP-30）
    if (m_session->includeCursor()) {
        if (auto cur = m_session->cursorImage(); cur && !cur->image.isNull()) {
            const QRect curRect(cur->physicalTopLeft, cur->image.size());
            if (curRect.intersects(m_screen.physicalGeometry)) {
                QImage img = cur->image;
                img.setDevicePixelRatio(m_screen.scale);
                p.drawImage(toLogical(curRect).topLeft(), img);
            }
        }
    }
}

void OverlayWindow::paintMagnifierIfNeeded(QPainter& p)
{
    if (!m_session->magnifierVisible())
        return;
    const QPoint cursor = m_session->cursor();
    if (!m_screen.physicalGeometry.contains(cursor))
        return;
    MagnifierOptions o;
    o.zoom = m_session->settings().intValue(QStringLiteral("capture.magnifierZoom"));
    o.colorFormat = m_session->colorFormat();
    o.hexUppercase = m_session->settings().boolValue(QStringLiteral("capture.hexUppercase"));
    const QPointF cursorLogical = toLogical(QRect(cursor, QSize(1, 1))).topLeft();
    const QPointF pos = magnifierPlacement(cursorLogical, magnifierSize(o), QRectF(0, 0, width(), height()));
    paintMagnifier(p, pos, m_session->desktop(), cursor, m_session->selection(), o, m_session->colorTextAtCursor());
}

void OverlayWindow::paintContent(QPainter& p)
{
    if (m_image.isNull()) {
        p.fillRect(rect(), Qt::black);
        return;
    }
    p.drawImage(QPointF(0, 0), m_image);
    const auto state = m_session->state();

    if (state == CaptureSession::State::History) {
        // 历史回放（CAP-40）：在原位置显示历史截图
        const QRect sel = m_session->selection();
        const QImage hist = m_session->historyImage();
        const QRect visible = sel.intersected(m_screen.physicalGeometry);
        if (!hist.isNull() && !visible.isEmpty()) {
            const QRect src = visible.translated(-sel.topLeft());
            QImage piece = hist.copy(src);
            piece.setDevicePixelRatio(m_screen.scale);
            p.drawImage(toLogical(visible).topLeft(), piece);
        }
        paintSelection(p, sel);
    } else if (state == CaptureSession::State::Annotating) {
        const QRect sel = m_session->selection();
        paintSelection(p, sel);
        const QRect visible = sel.intersected(m_screen.physicalGeometry);
        if (!visible.isEmpty()) {
            p.save();
            p.setClipRect(toLogical(visible));
            m_session->editor().paint(p, toLogical(QRect(sel.topLeft(), QSize(1, 1))).topLeft(), m_screen.scale);
            p.restore();
        }
    } else if (state == CaptureSession::State::Detecting) {
        p.fillRect(rect(), kDim);
        if (auto h = m_session->highlight()) {
            const QRect visible = h->intersected(m_screen.physicalGeometry);
            if (!visible.isEmpty()) {
                const QRectF r = toLogical(visible);
                // 高亮区域不压暗（重画原图），画边框
                p.drawImage(r, m_image, QRectF(r.topLeft() * m_screen.scale, r.size() * m_screen.scale));
                QPen pen(kAccent, 2);
                pen.setCosmetic(true);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(r.adjusted(0, 0, -1.0 / m_screen.scale, -1.0 / m_screen.scale));
            }
        }
    } else {
        paintSelection(p, m_session->selection());
    }
    paintMagnifierIfNeeded(p);
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

bool OverlayWindow::event(QEvent* e)
{
    if (e->type() == QEvent::WindowDeactivate && m_session->isActive()) {
        // 其他程序窗口被激活时退出（CAP-03）；本程序的窗口（工具栏、对话框、其他覆盖窗口）不算
        QTimer::singleShot(80, this, [this] {
            if (m_session->isActive() && !QApplication::activeWindow() && !QApplication::activeModalWidget() && !QApplication::activePopupWidget())
                m_session->onFocusLost();
        });
    }
    return QWidget::event(e);
}

void OverlayWindow::updateCursorShape(QPoint physical)
{
    if (m_session->state() == CaptureSession::State::Annotating && m_session->editor().tool() != AnnotationEditor::Tool::None
        && m_session->handleAt(physical) == SelectionHandle::None) {
        setCursor(Qt::CrossCursor);
        return;
    }
    switch (m_session->handleAt(physical)) {
    case SelectionHandle::TopLeft:
    case SelectionHandle::BottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case SelectionHandle::TopRight:
    case SelectionHandle::BottomLeft: setCursor(Qt::SizeBDiagCursor); break;
    case SelectionHandle::Top:
    case SelectionHandle::Bottom: setCursor(Qt::SizeVerCursor); break;
    case SelectionHandle::Left:
    case SelectionHandle::Right: setCursor(Qt::SizeHorCursor); break;
    case SelectionHandle::Inside: setCursor(Qt::SizeAllCursor); break;
    default: setCursor(Qt::CrossCursor); break;
    }
}

void OverlayWindow::moveCursorTo(QPoint physical)
{
#if defined(Q_OS_WIN)
    win::setCursorPhysicalPos(physical);
#else
    QCursor::setPos(toLogicalGlobal(QRect(physical, QSize(1, 1))).topLeft().toPoint());
#endif
}

void OverlayWindow::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_sizeLabel.isNull() && m_sizeLabel.contains(event->position())
        && (m_session->state() == CaptureSession::State::Selected || m_session->state() == CaptureSession::State::History)) {
        emit m_session->sizeInputRequested(); // CAP-17：点击尺寸文字输入宽高
        return;
    }
    m_session->pressAt(toPhysical(event->position()), event->button());
}

void OverlayWindow::wheelEvent(QWheelEvent* event)
{
    // CAP-13：检测状态下滚轮在父 / 子元素间切换（向上 = 父元素）
    const int steps = event->angleDelta().y() / 120;
    if (steps != 0 && m_session->state() == CaptureSession::State::Detecting)
        m_session->cycleElement(steps > 0 ? 1 : -1);
}

void OverlayWindow::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint phys = toPhysical(event->position());
    if (m_session->dragging() || m_session->dragKind() == CaptureSession::Drag::ClickCandidate || m_session->state() == CaptureSession::State::Annotating)
        m_session->dragTo(phys);
    else
        m_session->hoverAt(phys);
    updateCursorShape(phys);
}

void OverlayWindow::mouseReleaseEvent(QMouseEvent* event)
{
    const QPoint phys = toPhysical(event->position());
    m_session->releaseAt(phys, event->button());
    updateCursorShape(phys);
}

void OverlayWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_session->doubleClickAt(toPhysical(event->position()));
}

void OverlayWindow::keyPressEvent(QKeyEvent* event)
{
    if (!m_session->handleKeyPress(event))
        QWidget::keyPressEvent(event);
}

void OverlayWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (!m_session->handleKeyRelease(event))
        QWidget::keyReleaseEvent(event);
}

} // namespace ss
