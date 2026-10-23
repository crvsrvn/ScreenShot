#include "app/CaptureSession.h"

#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "core/config/Settings.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMessageBox>
#include <QtConcurrent>

namespace ss {

namespace {
constexpr int kClickTolerance = 3; // 像素内视为单击而不是拖拽
constexpr int kGrip = 8;           // 锚点命中半径（物理像素）
} // namespace

CaptureSession::CaptureSession(IScreenCapture* capture, IWindowDetector* windows, IElementDetector* elements, Settings& settings, QObject* parent)
    : QObject(parent)
    , m_capture(capture)
    , m_windows(windows)
    , m_elements(elements)
    , m_settings(settings)
{
    connect(&m_elementWatcher, &QFutureWatcher<std::optional<QRect>>::finished, this, &CaptureSession::onElementResult);
    m_toolbar = new CaptureToolbar(this);
}

CaptureSession::~CaptureSession()
{
    if (m_elementPending)
        m_elementWatcher.waitForFinished();
    qDeleteAll(m_overlays);
    delete m_toolbar;
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
    qDeleteAll(m_overlays);
    m_overlays = next;
}

bool CaptureSession::start(qint64 triggerNs, bool showOverlays)
{
    if (isActive() || !m_capture)
        return false;
    m_timings = Timings{};
    m_timings.triggerNs = triggerNs;
    m_showOverlays = showOverlays;

    QList<ScreenSnapshot> snapshots = m_capture->captureAll();
    if (snapshots.isEmpty())
        return false;
    m_timings.captureDoneNs = monotonicNs();

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
    m_windowFinder = WindowFinder(m_windows ? m_windows->topLevelWindows() : QList<WindowInfo>{}, m_desktop.bounds());
    m_selection = QRect();
    m_highlight.reset();
    m_windowHighlight.reset();
    m_drag = Drag::None;
    m_altHeld = false;
    m_fullScreenAll = m_settings.boolValue(QStringLiteral("capture.fullScreenAllMonitors"));
    const QString mode = m_settings.stringValue(QStringLiteral("capture.detectMode"));
    m_detectMode = mode == QLatin1String("window") ? DetectMode::Window : mode == QLatin1String("off") ? DetectMode::Off : DetectMode::Element;
    m_colorFormat = colorFormatFromName(m_settings.stringValue(QStringLiteral("capture.colorFormat")));
    m_historyCursor = m_history ? std::make_unique<HistoryCursor>(*m_history) : nullptr;
    m_state = State::Detecting;

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
    emit stateChanged(m_state);
    return true;
}

void CaptureSession::onOverlayFirstPaint(qint64 atNs)
{
    if (isActive() && m_timings.firstPaintNs == 0)
        m_timings.firstPaintNs = atNs;
}

void CaptureSession::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
    layoutToolbar();
    updateOverlays();
}

void CaptureSession::updateOverlays()
{
    for (auto* w : std::as_const(m_overlays))
        w->update();
}

// ---- 选区 ----

void CaptureSession::setSelection(const QRect& physical)
{
    m_selection = physical.normalized();
    if (!isActive())
        return;
    if (m_state == State::History)
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
    else if (m_state != State::Selecting)
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
    emit selectionChanged(m_selection);
    layoutToolbar();
    updateOverlays();
}

QRect CaptureSession::screenRectAt(QPoint physical) const
{
    if (auto s = m_desktop.screenAt(physical))
        return s->physicalGeometry;
    return m_desktop.bounds();
}

void CaptureSession::selectFullScreen()
{
    if (!isActive())
        return;
    const QRect current = screenRectAt(m_cursor);
    const QRect all = m_desktop.bounds();
    // 已经是当前屏幕 → 切到全部；已经是全部 → 切回当前；否则按设置的默认值
    QRect next;
    if (m_selection == current && m_desktop.snapshots().size() > 1)
        next = all;
    else if (m_selection == all)
        next = current;
    else
        next = m_fullScreenAll ? all : current;
    setSelection(next);
}

// ---- 检测 ----

void CaptureSession::setDetectMode(DetectMode mode)
{
    m_detectMode = mode;
    updateHighlight();
}

void CaptureSession::toggleDetectMode()
{
    // Tab：元素 ↔ 窗口；关闭状态下 Tab 临时打开窗口检测
    setDetectMode(m_detectMode == DetectMode::Element ? DetectMode::Window : DetectMode::Element);
}

void CaptureSession::hoverAt(QPoint physical)
{
    if (physical == m_cursor && m_state != State::Detecting)
        return;
    m_cursor = physical;
    emit cursorChanged(physical);
    if (m_state == State::Detecting)
        updateHighlight();
    updateOverlays();
}

void CaptureSession::updateHighlight()
{
    if (m_state != State::Detecting || m_detectMode == DetectMode::Off) {
        if (m_highlight) {
            m_highlight.reset();
            emit highlightChanged();
        }
        return;
    }
    const auto window = m_windowFinder.windowAt(m_cursor);
    m_windowHighlight = window ? std::optional<QRect>(window->frame) : std::nullopt;
    if (m_detectMode == DetectMode::Element && m_elements) {
        requestElementAt(m_cursor);
        // 元素结果未到之前先显示窗口框
        if (!m_highlight || (m_windowHighlight && !m_windowHighlight->contains(*m_highlight)))
            m_highlight = m_windowHighlight;
    } else {
        m_highlight = m_windowHighlight;
    }
    emit highlightChanged();
    updateOverlays();
}

void CaptureSession::requestElementAt(QPoint physical)
{
    if (m_elementPending) {
        m_elementQueued = physical; // 只保留最新的位置
        return;
    }
    m_elementPending = true;
    m_elementRequested = physical;
    IElementDetector* detector = m_elements;
    m_elementWatcher.setFuture(QtConcurrent::run([detector, physical] { return detector->elementAt(physical, std::chrono::milliseconds(300)); }));
}

void CaptureSession::onElementResult()
{
    m_elementPending = false;
    const std::optional<QRect> result = m_elementWatcher.result();
    if (m_state == State::Detecting && m_detectMode == DetectMode::Element) {
        if (result && result->contains(m_elementRequested)) {
            QRect r = *result;
            if (m_windowHighlight)
                r = r.intersected(*m_windowHighlight); // 元素不超出其窗口
            if (!r.isEmpty()) {
                m_highlight = r;
                emit highlightChanged();
                updateOverlays();
            }
        } else if (!result) {
            // 超时或没有元素：退回窗口框
            m_highlight = m_windowHighlight;
            emit highlightChanged();
            updateOverlays();
        }
    }
    if (m_elementQueued) {
        const QPoint next = *m_elementQueued;
        m_elementQueued.reset();
        if (next != m_elementRequested && m_state == State::Detecting)
            requestElementAt(next);
    }
}

// ---- 鼠标 ----

SelectionHandle CaptureSession::handleAt(QPoint physical) const
{
    if (m_state != State::Selected && m_state != State::History)
        return SelectionHandle::Outside;
    return hitTestSelection(m_selection, physical, kGrip);
}

void CaptureSession::pressAt(QPoint physical, Qt::MouseButton button)
{
    if (!isActive())
        return;
    m_cursor = physical;
    if (button == Qt::RightButton) {
        // 非编辑状态右键：有选区则重新框选，没有则退出（CAP-03、CAP-10）
        if (m_state == State::Selected || m_state == State::History)
            setSelection(QRect());
        else
            cancel();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    m_dragStart = physical;
    m_dragCurrent = physical;
    if (m_state == State::Selected || m_state == State::History) {
        const SelectionHandle h = handleAt(physical);
        if (h == SelectionHandle::Inside) {
            m_drag = Drag::Move;
            m_dragOrigin = m_selection;
            return;
        }
        if (h != SelectionHandle::Outside) {
            m_drag = Drag::Resize;
            m_dragHandle = h;
            m_dragOrigin = m_selection;
            return;
        }
        // 选区外按下：重新框选
        setSelection(QRect());
    }
    // 检测状态：可能是单击选中高亮的窗口 / 元素，也可能是拖拽框选
    m_drag = Drag::ClickCandidate;
}

void CaptureSession::dragTo(QPoint physical)
{
    if (!isActive())
        return;
    m_cursor = physical;
    emit cursorChanged(physical);
    switch (m_drag) {
    case Drag::ClickCandidate:
        if ((physical - m_dragStart).manhattanLength() > kClickTolerance) {
            m_drag = Drag::Create;
            m_highlight.reset();
            setState(State::Selecting);
            m_dragCurrent = physical;
            m_selection = QRect(QPoint(std::min(m_dragStart.x(), physical.x()), std::min(m_dragStart.y(), physical.y())),
                                QPoint(std::max(m_dragStart.x(), physical.x()), std::max(m_dragStart.y(), physical.y())));
            emit selectionChanged(m_selection);
        }
        break;
    case Drag::Create:
        m_dragCurrent = physical;
        m_selection = QRect(QPoint(std::min(m_dragStart.x(), physical.x()), std::min(m_dragStart.y(), physical.y())),
                            QPoint(std::max(m_dragStart.x(), physical.x()), std::max(m_dragStart.y(), physical.y())));
        emit selectionChanged(m_selection);
        break;
    case Drag::Move:
        m_selection = moveSelection(m_dragOrigin, physical - m_dragStart, m_desktop.bounds());
        emit selectionChanged(m_selection);
        layoutToolbar();
        break;
    case Drag::Resize:
        m_selection = resizeSelection(m_dragOrigin, m_dragHandle, physical - m_dragStart).intersected(m_desktop.bounds());
        emit selectionChanged(m_selection);
        layoutToolbar();
        break;
    case Drag::None:
        hoverAt(physical);
        return;
    }
    updateOverlays();
}

void CaptureSession::releaseAt(QPoint physical, Qt::MouseButton button)
{
    if (!isActive() || button != Qt::LeftButton)
        return;
    const Drag drag = m_drag;
    m_drag = Drag::None;
    switch (drag) {
    case Drag::ClickCandidate:
        // 单击：选中高亮的窗口 / 元素（CAP-11）
        if (m_highlight)
            setSelection(*m_highlight);
        break;
    case Drag::Create:
        dragTo(physical);
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
        emit selectionChanged(m_selection);
        break;
    case Drag::Move:
    case Drag::Resize:
        dragTo(physical);
        layoutToolbar();
        break;
    case Drag::None:
        break;
    }
    updateOverlays();
}

void CaptureSession::doubleClickAt(QPoint physical)
{
    if (!isActive())
        return;
    m_drag = Drag::None;
    if ((m_state == State::Selected || m_state == State::History) && m_selection.contains(physical)) {
        accept(); // 双击复制（OUT-01）
    } else if (m_state == State::Detecting && m_highlight && m_highlight->contains(physical)) {
        setSelection(*m_highlight);
        accept(); // 双击窗口直接复制（CAP-11）
    }
}

// ---- 键盘 ----

bool CaptureSession::handleKeyPress(QKeyEvent* e)
{
    if (!isActive())
        return false;
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers();
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    if (key != Qt::Key_Shift)
        m_shiftAlone = false;
    switch (key) {
    case Qt::Key_Escape:
        if (confirmCancel())
            cancel();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        accept();
        return true;
    case Qt::Key_Insert:
        if (ctrl) {
            accept();
            return true;
        }
        return false;
    case Qt::Key_C:
        if (ctrl)
            accept();
        else
            copyColor();
        return true;
    case Qt::Key_S:
        if (ctrl && shift) {
            quickSave();
            return true;
        }
        if (ctrl) {
            save();
            return true;
        }
        break;
    case Qt::Key_T:
        if (ctrl) {
            pin();
            return true;
        }
        break;
    case Qt::Key_A:
        if (ctrl) {
            selectFullScreen();
            return true;
        }
        break;
    case Qt::Key_Tab:
        toggleDetectMode();
        return true;
    case Qt::Key_Comma:
        historyOlder();
        return true;
    case Qt::Key_Period:
        historyNewer();
        return true;
    case Qt::Key_Shift:
        m_shiftAlone = true;
        return true;
    case Qt::Key_Alt:
        setAltHeld(true);
        return true;
    default:
        break;
    }
    // WASD 与方向键（CAP-24）；可互换
    const bool swap = m_settings.boolValue(QStringLiteral("capture.swapWasdArrows"));
    Qt::Key arrow = Qt::Key_unknown;
    bool isWasd = false;
    switch (key) {
    case Qt::Key_W: arrow = Qt::Key_Up; isWasd = true; break;
    case Qt::Key_A: arrow = Qt::Key_Left; isWasd = true; break;
    case Qt::Key_S: arrow = Qt::Key_Down; isWasd = true; break;
    case Qt::Key_D: arrow = Qt::Key_Right; isWasd = true; break;
    case Qt::Key_Up: case Qt::Key_Down: case Qt::Key_Left: case Qt::Key_Right: arrow = static_cast<Qt::Key>(key); break;
    default: return false;
    }
    const bool cursorRole = isWasd != swap; // 默认 WASD 动光标，方向键动选区
    const QPoint delta = arrow == Qt::Key_Left ? QPoint(-1, 0) : arrow == Qt::Key_Right ? QPoint(1, 0) : arrow == Qt::Key_Up ? QPoint(0, -1) : QPoint(0, 1);
    if (cursorRole) {
        if (m_drag == Drag::Create) {
            dragTo(m_dragCurrent + delta); // 按住左键 + WASD 调整选区
            emit cursorMoveRequested(m_dragCurrent);
        } else {
            moveCursorBy(delta);
        }
        return true;
    }
    if (m_state == State::Selected || m_state == State::History) {
        nudge(arrow, ctrl ? NudgeKind::Expand : shift ? NudgeKind::Shrink : NudgeKind::Move);
        return true;
    }
    return false;
}

bool CaptureSession::handleKeyRelease(QKeyEvent* e)
{
    if (!isActive())
        return false;
    if (e->key() == Qt::Key_Alt) {
        setAltHeld(false);
        return true;
    }
    if (e->key() == Qt::Key_Shift) {
        if (m_shiftAlone)
            cycleColorFormat(); // 单独按一下 Shift 轮换颜色格式
        m_shiftAlone = false;
        return true;
    }
    return false;
}

void CaptureSession::setAltHeld(bool held)
{
    if (m_altHeld == held)
        return;
    m_altHeld = held;
    updateOverlays();
}

bool CaptureSession::magnifierVisible() const
{
    if (!isActive())
        return false;
    if (m_altHeld)
        return true;
    if (!m_settings.boolValue(QStringLiteral("capture.magnifierAuto")))
        return false;
    return m_state == State::Detecting || m_state == State::Selecting || m_drag == Drag::Resize;
}

void CaptureSession::moveCursorBy(QPoint delta)
{
    m_cursor += delta;
    emit cursorMoveRequested(m_cursor);
    hoverAt(m_cursor);
}

void CaptureSession::nudge(Qt::Key key, NudgeKind kind)
{
    const QRect next = nudgeSelection(m_selection, key, kind, m_desktop.bounds());
    if (next != m_selection)
        setSelection(next);
}

// ---- 取色 ----

void CaptureSession::cycleColorFormat()
{
    m_colorFormat = nextColorFormat(m_colorFormat);
    m_settings.setValue(QStringLiteral("capture.colorFormat"), colorFormatName(m_colorFormat));
    updateOverlays();
}

QString CaptureSession::colorTextAtCursor() const
{
    const auto px = m_desktop.pixelAt(m_cursor);
    if (!px)
        return {};
    return formatColor(QColor(*px), m_colorFormat, m_settings.boolValue(QStringLiteral("capture.hexUppercase")));
}

void CaptureSession::copyColor()
{
    const QString text = colorTextAtCursor();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
    emit colorCopied(text);
}

// ---- 历史 ----

void CaptureSession::historyOlder()
{
    if (!m_historyCursor || !isActive() || m_drag != Drag::None)
        return;
    if (m_historyCursor->older()) {
        const HistoryEntry& e = m_history->at(m_historyCursor->index());
        m_selection = e.physicalRegion;
        m_state = State::History;
        emit stateChanged(m_state);
        emit selectionChanged(m_selection);
        layoutToolbar();
        updateOverlays();
    }
}

void CaptureSession::historyNewer()
{
    if (!m_historyCursor || !isActive() || m_state != State::History)
        return;
    if (!m_historyCursor->newer())
        return;
    if (m_historyCursor->viewingHistory()) {
        const HistoryEntry& e = m_history->at(m_historyCursor->index());
        m_selection = e.physicalRegion;
        emit selectionChanged(m_selection);
    } else {
        // 回到当前画面：历史的选区保留为当前选区
        m_state = State::Selected;
        emit stateChanged(m_state);
    }
    layoutToolbar();
    updateOverlays();
}

QImage CaptureSession::historyImage() const
{
    if (m_state == State::History && m_historyCursor && m_historyCursor->viewingHistory())
        return m_history->at(m_historyCursor->index()).image;
    return {};
}

// ---- 输出 ----

QImage CaptureSession::currentImage() const
{
    if (m_state == State::History)
        return historyImage();
    if (m_selection.isEmpty())
        return {};
    return m_desktop.crop(m_selection);
}

void CaptureSession::recordHistory(const QImage& image, const QRect& region)
{
    if (m_history && m_state != State::History)
        m_history->add({image, region, QDateTime::currentDateTime()});
}

QImage CaptureSession::accept()
{
    if (!isActive())
        return {};
    const QImage image = currentImage();
    if (image.isNull())
        return {};
    const QRect region = m_selection;
    if (m_output)
        m_output->copy(image);
    else
        QApplication::clipboard()->setImage(image);
    recordHistory(image, region);
    emit captured(image, region);
    finish();
    return image;
}

void CaptureSession::hideOverlaysTemporarily(bool hide)
{
    for (auto* w : std::as_const(m_overlays))
        w->setVisible(!hide && m_showOverlays);
    m_toolbar->setVisible(!hide && m_showOverlays && (m_state == State::Selected || m_state == State::History));
}

void CaptureSession::save()
{
    if (!isActive() || !m_output)
        return;
    const QImage image = currentImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    hideOverlaysTemporarily(true);
    const QString path = m_output->saveWithDialog(image, nullptr);
    if (path.isEmpty()) {
        if (isActive())
            hideOverlaysTemporarily(false);
        return;
    }
    recordHistory(image, region);
    emit captured(image, region);
    finish();
}

void CaptureSession::quickSave()
{
    if (!isActive() || !m_output)
        return;
    const QImage image = currentImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    if (m_output->quickSave(image).isEmpty())
        return;
    recordHistory(image, region);
    emit captured(image, region);
    finish();
}

void CaptureSession::pin()
{
    if (!isActive())
        return;
    const QImage image = currentImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    recordHistory(image, region);
    emit captured(image, region);
    finish();
    emit pinRequested(image, region);
}

bool CaptureSession::confirmCancel()
{
    if (!m_settings.boolValue(QStringLiteral("capture.confirmEscape")) || m_selection.isEmpty())
        return true;
    return QMessageBox::question(nullptr, tr("Cancel capture"), tr("Discard this capture?")) == QMessageBox::Yes;
}

void CaptureSession::cancel()
{
    if (!isActive())
        return;
    emit cancelled();
    finish();
}

void CaptureSession::onFocusLost()
{
    if (!isActive() || !m_settings.boolValue(QStringLiteral("capture.exitOnFocusLoss")))
        return;
    cancel();
}

void CaptureSession::finish()
{
    m_state = State::Idle;
    m_drag = Drag::None;
    m_highlight.reset();
    m_elementQueued.reset();
    m_toolbar->hide();
    for (auto* w : std::as_const(m_overlays)) {
        w->hide();
        w->clearSnapshot();
    }
    m_desktop = VirtualDesktop(); // 整份快照一次释放
    m_windowFinder = WindowFinder();
    m_historyCursor.reset();
    emit stateChanged(m_state);
}

OverlayWindow* CaptureSession::overlayAt(QPoint physical) const
{
    for (auto* w : m_overlays) {
        if (w->screenInfo().physicalGeometry.contains(physical))
            return w;
    }
    return nullptr;
}

void CaptureSession::layoutToolbar()
{
    const bool show = isActive() && m_showOverlays && (m_state == State::Selected || m_state == State::History) && !m_selection.isEmpty() && !dragging();
    if (!show) {
        m_toolbar->hide();
        return;
    }
    // 放在选区右下方；没有空间则放上方；再没有就放选区内
    OverlayWindow* anchor = overlayAt(m_selection.bottomLeft());
    if (!anchor)
        anchor = overlayAt(m_selection.topLeft());
    if (!anchor)
        return;
    const QRectF selLogical = anchor->toLogicalGlobal(m_selection);
    const QRectF screen = anchor->geometry();
    const QSize size = m_toolbar->sizeHint();
    QPointF pos(selLogical.right() - size.width() + 1, selLogical.bottom() + 6);
    if (pos.y() + size.height() > screen.bottom())
        pos.setY(selLogical.top() - 6 - size.height());
    if (pos.y() < screen.top())
        pos.setY(selLogical.bottom() - 6 - size.height());
    pos.setX(std::max(pos.x(), screen.left()));
    m_toolbar->move(pos.toPoint());
    m_toolbar->show();
    m_toolbar->raise();
}

} // namespace ss
