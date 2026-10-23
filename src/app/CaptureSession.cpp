#include "app/CaptureSession.h"

#include "app/OverlayWindow.h"
#include "core/util/Clock.h"

#include <QGuiApplication>
#include <QScreen>

namespace ss {

CaptureSession::CaptureSession(IScreenCapture* capture, QObject* parent)
    : QObject(parent)
    , m_capture(capture)
{
}

CaptureSession::~CaptureSession()
{
    qDeleteAll(m_overlays);
}

void CaptureSession::prepareOverlays()
{
    if (!m_capture)
        return;
    QHash<QString, OverlayWindow*> next;
    for (const auto& s : m_capture->screens()) {
        OverlayWindow* w = m_overlays.take(s.id);
        if (!w) {
            w = new OverlayWindow(this);
            connect(w, &OverlayWindow::firstPainted, this, &CaptureSession::onOverlayFirstPaint);
        }
        w->setScreenInfo(s);
        next.insert(s.id, w);
    }
    qDeleteAll(m_overlays); // 已拔掉的显示器
    m_overlays = next;
}

bool CaptureSession::start(qint64 triggerNs, bool showOverlays)
{
    if (m_active || !m_capture)
        return false;
    m_timings = Timings{};
    m_timings.triggerNs = triggerNs;

    QList<ScreenSnapshot> snapshots = m_capture->captureAll();
    if (snapshots.isEmpty())
        return false;
    m_timings.captureDoneNs = monotonicNs();

    // 布局变化时同步覆盖窗口
    bool layoutChanged = snapshots.size() != m_overlays.size();
    for (const auto& s : snapshots) {
        auto it = m_overlays.find(s.screen.id);
        if (it == m_overlays.end() || (*it)->screenInfo() != s.screen) {
            layoutChanged = true;
            break;
        }
    }
    if (layoutChanged)
        prepareOverlays();

    m_desktop = VirtualDesktop(std::move(snapshots));
    m_selection = QRect();
    m_dragging = false;
    m_active = true;

    for (const auto& snap : m_desktop.snapshots()) {
        if (auto* w = m_overlays.value(snap.screen.id))
            w->setSnapshot(snap);
    }
    if (showOverlays) {
        for (auto* w : std::as_const(m_overlays))
            w->showOnScreen();
    }
    m_timings.shownNs = monotonicNs();
    emit started();
    return true;
}

void CaptureSession::onOverlayFirstPaint(qint64 atNs)
{
    if (m_active && m_timings.firstPaintNs == 0)
        m_timings.firstPaintNs = atNs;
}

void CaptureSession::setSelection(const QRect& physical)
{
    m_selection = physical.normalized();
    emit selectionChanged(m_selection);
    for (auto* w : std::as_const(m_overlays))
        w->update();
}

void CaptureSession::beginDrag(QPoint physical)
{
    m_dragging = true;
    m_dragStart = physical;
    setSelection(QRect(physical, physical));
}

void CaptureSession::dragTo(QPoint physical)
{
    if (!m_dragging)
        return;
    // 两端像素都包含在选区内
    const QPoint tl(std::min(m_dragStart.x(), physical.x()), std::min(m_dragStart.y(), physical.y()));
    const QPoint br(std::max(m_dragStart.x(), physical.x()), std::max(m_dragStart.y(), physical.y()));
    setSelection(QRect(tl, br));
}

void CaptureSession::endDrag(QPoint physical)
{
    dragTo(physical);
    m_dragging = false;
}

QImage CaptureSession::accept()
{
    if (!m_active || m_selection.isEmpty())
        return {};
    const QRect region = m_selection;
    QImage image = m_desktop.crop(region);
    emit captured(image, region);
    finish();
    return image;
}

void CaptureSession::cancel()
{
    if (!m_active)
        return;
    emit cancelled();
    finish();
}

void CaptureSession::finish()
{
    m_active = false;
    m_dragging = false;
    for (auto* w : std::as_const(m_overlays)) {
        w->hide();
        w->clearSnapshot();
    }
    m_desktop = VirtualDesktop(); // 整份快照一次释放
}

OverlayWindow* CaptureSession::overlayAt(QPoint physical) const
{
    for (auto* w : m_overlays) {
        if (w->screenInfo().physicalGeometry.contains(physical))
            return w;
    }
    return nullptr;
}

} // namespace ss
