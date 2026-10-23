#include "app/CaptureSession.h"

#include "app/CaptureToolbar.h"
#include "app/EncodePreviewDialog.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "core/config/Settings.h"
#include "core/config/Shortcuts.h"
#include "core/output/Decorate.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMessageBox>
#include <QDir>
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
    , m_editor(settings)
{
    connect(&m_elementWatcher, &QFutureWatcher<std::optional<QRect>>::finished, this, &CaptureSession::onElementResult);
    connect(&m_codesWatcher, &QFutureWatcher<QList<CodeResult>>::finished, this, &CaptureSession::onCodesResult);
    connect(&m_editor, &AnnotationEditor::changed, this, [this] { updateOverlays(); });
    m_toolbar = new CaptureToolbar(this);
}

// ---- 标注 ----

void CaptureSession::setTool(AnnotationEditor::Tool tool)
{
    if (!isActive())
        return;
    if (m_state == State::Selected || m_state == State::History) {
        if (m_selection.isEmpty())
            return;
        m_annotatedBase = currentImage();
        m_editor.begin(m_annotatedBase, m_selection);
        if (OverlayWindow* host = overlayAt(m_selection.topLeft()))
            m_editor.setTextHost(host, host->toLogical(QRect(m_selection.topLeft(), QSize(1, 1))).topLeft(), host->screenInfo().scale);
        setState(State::Annotating);
    }
    if (m_state == State::Annotating) {
        m_editor.setTool(tool);
        if (tool != AnnotationEditor::Tool::None && m_settings.boolValue(QStringLiteral("annotation.rememberTool")))
            m_settings.setValue(QStringLiteral("annotation.lastToolCapture"), CaptureToolbar::toolName(tool)); // ANN-15
    }
    layoutToolbar();
    updateOverlays();
}

bool CaptureSession::snapAnnotationToElement(QPoint physical)
{
    using Tool = AnnotationEditor::Tool;
    const Tool tool = m_editor.tool();
    if (tool != Tool::Rect && tool != Tool::Ellipse && tool != Tool::Mosaic && tool != Tool::Blur && tool != Tool::Highlighter)
        return false;
    if (!m_selection.contains(physical))
        return false;
    std::optional<QRect> rect;
    if (m_elements)
        rect = m_elements->elementAt(physical, std::chrono::milliseconds(300));
    if (!rect)
        if (const auto w = m_windowFinder.windowAt(physical))
            rect = w->frame;
    if (!rect)
        return false;
    const QRect clipped = rect->intersected(m_selection);
    if (clipped.isEmpty())
        return false;
    return m_editor.createFromRect(QRectF(clipped.translated(-m_selection.topLeft())));
}

void CaptureSession::leaveAnnotating()
{
    if (m_state != State::Annotating)
        return;
    m_editor.finishDraft();
    // 标注结果固化为当前画面的一部分：回到已选定状态，导出仍用标注后的图
    setState(m_historyCursor && m_historyCursor->viewingHistory() ? State::History : State::Selected);
    updateOverlays();
}

bool CaptureSession::handleAnnotationKey(QKeyEvent* e)
{
    const int key = e->key();
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    if (m_editor.textEditing())
        return false; // 输入框自己处理
    switch (key) {
    case Qt::Key_Z:
        if (ctrl) {
            shift ? m_editor.redo() : m_editor.undo();
            return true;
        }
        break;
    case Qt::Key_Y:
        if (ctrl) {
            m_editor.redo();
            return true;
        }
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        m_editor.deleteSelected();
        return true;
    case Qt::Key_C:
        if (ctrl && shift) {
            m_editor.copySelected();
            return true;
        }
        break;
    case Qt::Key_V:
        if (ctrl && shift) {
            m_editor.pasteCopied();
            return true;
        }
        break;
    case Qt::Key_Tab:
        m_editor.toggleArrow();
        return true;
    case Qt::Key_BracketLeft:
    case Qt::Key_1:
        if (!ctrl) {
            m_editor.adjustWidth(-1);
            return true;
        }
        break;
    case Qt::Key_BracketRight:
    case Qt::Key_2:
        if (!ctrl) {
            m_editor.adjustWidth(1);
            return true;
        }
        break;
    case Qt::Key_T:
        if (!ctrl) {
            m_editor.setTool(AnnotationEditor::Tool::Text);
            return true;
        }
        break;
    case Qt::Key_B:
        if (!ctrl) {
            m_editor.setTool(AnnotationEditor::Tool::Pencil);
            return true;
        }
        break;
    case Qt::Key_N:
        if (!ctrl) {
            m_editor.setTool(AnnotationEditor::Tool::Step);
            return true;
        }
        break;
    case Qt::Key_M:
        if (!ctrl) {
            m_editor.setTool(AnnotationEditor::Tool::Magnifier);
            return true;
        }
        break;
    case Qt::Key_Escape:
        if (m_editor.hasDraft()) {
            m_editor.cancelDraft();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
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
    QList<ScreenSnapshot> snapshots = m_capture->captureAll();
    if (snapshots.isEmpty())
        return false;
    m_timings.captureDoneNs = monotonicNs();
    m_includeCursor = m_settings.boolValue(QStringLiteral("capture.includeCursor"));
    m_cursorImage = m_capture->captureCursor();
    m_whiteboard = false;
    return startWithSnapshots(triggerNs, std::move(snapshots), showOverlays);
}

bool CaptureSession::startTransparentBoard(bool showOverlays)
{
    if (isActive() || !m_capture)
        return false;
    m_transparentBoard = true;
    for (auto* w : std::as_const(m_overlays))
        w->setTransparentMode(true);
    if (!startWhiteboard(Qt::transparent, false, showOverlays)) {
        m_transparentBoard = false;
        for (auto* w : std::as_const(m_overlays))
            w->setTransparentMode(false);
        return false;
    }
    return true;
}

bool CaptureSession::startWhiteboard(const QColor& color, bool currentScreenOnly, bool showOverlays)
{
    if (isActive() || !m_capture)
        return false;
    m_timings = Timings{};
    m_timings.triggerNs = monotonicNs();
    const QPoint cursor = m_cursor;
    QList<ScreenSnapshot> snapshots;
    const QList<ScreenInfo> screens = m_capture->screens();
    for (const auto& screen : screens) {
        if (currentScreenOnly && !screen.physicalGeometry.contains(cursor))
            continue;
        QImage canvas(screen.physicalGeometry.size(), color.alpha() == 0 ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
        canvas.fill(color);
        canvas.setDevicePixelRatio(screen.scale);
        snapshots.append(ScreenSnapshot{screen, canvas, monotonicNs()});
    }
    if (snapshots.isEmpty() && !screens.isEmpty()) {
        // 光标不在任何显示器上：用主屏
        for (const auto& screen : screens) {
            if (!screen.primary && screens.size() > 1)
                continue;
            QImage canvas(screen.physicalGeometry.size(), QImage::Format_RGB32);
            canvas.fill(color);
            canvas.setDevicePixelRatio(screen.scale);
            snapshots.append(ScreenSnapshot{screen, canvas, monotonicNs()});
            break;
        }
    }
    if (snapshots.isEmpty())
        return false;
    m_timings.captureDoneNs = monotonicNs();
    m_includeCursor = false;
    m_cursorImage.reset();
    m_whiteboard = true;
    if (!startWithSnapshots(m_timings.triggerNs, std::move(snapshots), showOverlays))
        return false;
    // 整块画布作为选区并直接进入标注
    setSelection(m_desktop.bounds());
    setTool(AnnotationEditor::Tool::Pencil);
    return true;
}

bool CaptureSession::startWithSnapshots(qint64 triggerNs, QList<ScreenSnapshot> snapshots, bool showOverlays)
{
    m_timings.triggerNs = triggerNs;
    m_showOverlays = showOverlays;
    bool layoutChanged = snapshots.size() != m_overlays.size();
    for (const auto& s : snapshots) {
        auto it = m_overlays.find(s.screen.id);
        if (it == m_overlays.end() || (*it)->screenInfo() != s.screen) {
            layoutChanged = true;
            break;
        }
    }
    if (layoutChanged) {
        // 按快照建覆盖窗口（白板可能只覆盖部分显示器）
        QHash<QString, OverlayWindow*> next;
        for (const auto& snap : snapshots) {
            OverlayWindow* w = m_overlays.take(snap.screen.id);
            if (!w) {
                w = new OverlayWindow(this);
                connect(w, &OverlayWindow::firstPainted, this, &CaptureSession::onOverlayFirstPaint);
            }
            w->setScreenInfo(snap.screen);
            next.insert(snap.screen.id, w);
        }
        qDeleteAll(m_overlays);
        m_overlays = next;
    }

    m_desktop = VirtualDesktop(std::move(snapshots));
    m_windowFinder = WindowFinder(m_windows && !m_whiteboard ? m_windows->topLevelWindows() : QList<WindowInfo>{}, m_desktop.bounds());
    m_selection = QRect();
    m_highlight.reset();
    m_windowHighlight.reset();
    m_elementChain.clear();
    m_elementLevel = 0;
    m_drag = Drag::None;
    m_altHeld = false;
    m_spaceHeld = false;
    m_ratioLocked = false;
    m_fullScreenAll = m_settings.boolValue(QStringLiteral("capture.fullScreenAllMonitors"));
    const QString mode = m_settings.stringValue(QStringLiteral("capture.detectMode"));
    m_detectMode = m_whiteboard ? DetectMode::Off : mode == QLatin1String("window") ? DetectMode::Window : mode == QLatin1String("off") ? DetectMode::Off : DetectMode::Element;
    m_colorFormat = colorFormatFromName(m_settings.stringValue(QStringLiteral("capture.colorFormat")));
    m_shadow = m_settings.boolValue(QStringLiteral("output.shadow"));
    m_border = m_settings.boolValue(QStringLiteral("output.border"));
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

bool CaptureSession::refresh()
{
    if (!isActive() || m_whiteboard || !m_capture)
        return false;
    // 隐藏覆盖窗口再抓，避免把自己抓进去
    for (auto* w : std::as_const(m_overlays))
        w->setVisible(false);
    QList<ScreenSnapshot> snapshots = m_capture->captureAll();
    if (snapshots.isEmpty()) {
        for (auto* w : std::as_const(m_overlays))
            w->setVisible(m_showOverlays);
        return false;
    }
    m_desktop = VirtualDesktop(std::move(snapshots));
    m_windowFinder = WindowFinder(m_windows ? m_windows->topLevelWindows() : QList<WindowInfo>{}, m_desktop.bounds());
    m_cursorImage = m_capture->captureCursor();
    for (const auto& snap : m_desktop.snapshots())
        if (auto* w = m_overlays.value(snap.screen.id))
            w->setSnapshot(snap);
    if (m_state == State::Annotating) {
        m_annotatedBase = m_desktop.crop(m_selection);
        m_editor.document().setBaseImage(m_annotatedBase);
    }
    if (m_showOverlays)
        for (auto* w : std::as_const(m_overlays))
            w->showOnScreen();
    updateOverlays();
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
    if (state == State::Selected) {
        autoDetectCodes(); // REC-06：选区确定后自动检测
        // ANN-15：默认工具 / 上次工具
        QString toolName = m_settings.stringValue(QStringLiteral("annotation.defaultCaptureTool"));
        if (m_settings.boolValue(QStringLiteral("annotation.rememberTool")) && !m_settings.stringValue(QStringLiteral("annotation.lastToolCapture")).isEmpty())
            toolName = m_settings.stringValue(QStringLiteral("annotation.lastToolCapture"));
        const AnnotationEditor::Tool tool = CaptureToolbar::toolFromName(toolName);
        if (tool != AnnotationEditor::Tool::None && !m_selection.isEmpty() && m_drag == Drag::None) {
            m_state = state;
            emit stateChanged(state);
            setTool(tool);
            return;
        }
    }
    else if (state != State::Annotating && state != State::History)
        m_codes.clear();
    emit stateChanged(state);
    m_toolbar->syncFromEditor();
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
    const State before = m_state;
    if (m_state == State::History)
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
    else if (m_state != State::Selecting)
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
    if (before == State::Selected && m_state == State::Selected) {
        m_codes.clear();
        autoDetectCodes(); // 选区变了：重新检测（REC-06）
    }
    emit selectionChanged(m_selection);
    layoutToolbar();
    updateOverlays();
}

void CaptureSession::setSelectionSize(int width, int height)
{
    if (!isActive() || m_selection.isEmpty() || width < 1 || height < 1)
        return;
    QRect r(m_selection.topLeft(), QSize(width, height));
    setSelection(r.intersected(m_desktop.bounds()));
}

void CaptureSession::setRatioLocked(bool locked)
{
    m_ratioLocked = locked && !m_selection.isEmpty();
    if (m_ratioLocked)
        m_lockedRatio = static_cast<double>(m_selection.width()) / m_selection.height();
    updateOverlays();
}

QRect CaptureSession::applyRatio(const QRect& r, SelectionHandle handle) const
{
    // Shift：1:1；锁定：保持开始时的宽高比。以宽为准，按锚点决定扩展方向
    const double ratio = m_shiftHeld ? 1.0 : m_ratioLocked ? m_lockedRatio : 0.0;
    if (ratio <= 0 || r.isEmpty())
        return r;
    const int h = std::max(1, static_cast<int>(std::lround(r.width() / ratio)));
    QRect out = r;
    const bool anchorBottom = handle == SelectionHandle::TopLeft || handle == SelectionHandle::Top || handle == SelectionHandle::TopRight;
    if (anchorBottom)
        out.setTop(out.bottom() - h + 1);
    else
        out.setHeight(h);
    return out;
}

void CaptureSession::setSpaceHeld(bool held)
{
    m_spaceHeld = held;
    if (held && (m_drag == Drag::Resize || m_drag == Drag::Create)) {
        // 调整大小中按住 Space：改为移动整个选区（CAP-16）
        m_drag = Drag::Move;
        m_dragOrigin = m_selection;
        m_dragStart = m_cursor;
    }
}

void CaptureSession::setIncludeCursor(bool on)
{
    m_includeCursor = on;
    updateOverlays();
}

void CaptureSession::cycleElement(int delta)
{
    if (m_state != State::Detecting || m_elementChain.size() < 2)
        return;
    m_elementLevel = std::clamp(m_elementLevel + delta, 0, static_cast<int>(m_elementChain.size()) - 1);
    m_highlight = m_elementChain[m_elementLevel];
    emit highlightChanged();
    updateOverlays();
}

void CaptureSession::setShadowEnabled(bool on)
{
    m_shadow = on;
    m_settings.setValue(QStringLiteral("output.shadow"), on);
    updateOverlays();
}

void CaptureSession::setBorderEnabled(bool on)
{
    m_border = on;
    m_settings.setValue(QStringLiteral("output.border"), on);
    updateOverlays();
}

QImage CaptureSession::finalImage() const
{
    if (m_output) {
        const auto w = sourceWindow();
        m_output->setSource(w ? w->title : QString(), w ? w->processName : QString());
    }
    QImage image = currentImage();
    if (image.isNull())
        return image;
    if (m_transparentBoard && m_capture) {
        // CAP-51：抓取实时桌面，把透明画布上的标注合成上去
        auto* self = const_cast<CaptureSession*>(this);
        self->hideOverlaysTemporarily(true);
        QCoreApplication::processEvents();
        const VirtualDesktop live(m_capture->captureAll());
        QImage base = live.crop(m_selection).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (!base.isNull()) {
            QPainter p(&base);
            p.drawImage(0, 0, image);
            p.end();
            image = base;
        }
        if (isActive())
            self->hideOverlaysTemporarily(false);
    }
    if (m_includeCursor && m_cursorImage && m_state != State::History)
        image = composeCursor(image, m_cursorImage->image, m_cursorImage->physicalTopLeft - m_selection.topLeft());
    DecorateOptions o;
    o.cornerRadius = m_settings.intValue(QStringLiteral("output.cornerRadius"));
    o.shadow = m_shadow;
    o.shadowColor = QColor::fromString(m_settings.stringValue(QStringLiteral("output.shadowColor")));
    o.border = m_border;
    o.borderColor = QColor::fromString(m_settings.stringValue(QStringLiteral("output.borderColor")));
    o.borderWidth = m_settings.intValue(QStringLiteral("output.borderWidth"));
    return decorateImage(image, o);
}

void CaptureSession::runActions(const QStringList& actionsIn)
{
    if (!isActive())
        return;
    const QImage image = finalImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    // 固定顺序：复制 → 贴图 → 保存 → 快速保存 → OCR 复制（CAP-04）
    static const QStringList order{QStringLiteral("copy"), QStringLiteral("pin"), QStringLiteral("save"), QStringLiteral("quickSave"), QStringLiteral("ocrCopy")};
    QStringList actions;
    for (const QString& a : order)
        if (actionsIn.contains(a, Qt::CaseInsensitive))
            actions << a;
    if (actions.isEmpty())
        actions << QStringLiteral("copy");
    bool wantPin = false;
    for (const QString& a : actions) {
        if (a == QLatin1String("copy")) {
            if (m_output)
                m_output->copy(image);
            else
                QApplication::clipboard()->setImage(image);
        } else if (a == QLatin1String("pin")) {
            wantPin = true;
        } else if (a == QLatin1String("save") && m_output) {
            hideOverlaysTemporarily(true);
            m_output->saveWithDialog(image, nullptr);
            if (isActive())
                hideOverlaysTemporarily(false);
        } else if (a == QLatin1String("quickSave") && m_output) {
            m_output->quickSave(image);
        } else if (a == QLatin1String("ocrCopy")) {
            emit ocrCopyRequested(image);
        }
    }
    recordHistory(image, region);
    emit captured(image, region);
    finish();
    if (wantPin)
        emit pinRequested(image, region);
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
    // CAP-14：按光标所在窗口的进程决定跳过 / 保守检测
    QString process;
    if (const auto w = m_windowFinder.windowAt(physical))
        process = w->processName.toLower();
    auto listed = [&](const QString& key) {
        for (const QString& item : m_settings.stringValue(key).split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (!item.trimmed().isEmpty() && process.contains(item.trimmed().toLower()))
                return true;
        return false;
    };
    const bool excluded = !process.isEmpty() && listed(QStringLiteral("capture.detectExcludeProcesses"));
    const bool conservative = !process.isEmpty() && listed(QStringLiteral("capture.detectConservativeProcesses"));
    m_elementWatcher.setFuture(QtConcurrent::run([detector, physical, excluded, conservative]() -> std::optional<QRect> {
        if (excluded)
            return std::nullopt;
        if (conservative)
            return detector->elementAt(physical, std::chrono::milliseconds(100)); // 短超时、不取层级
        const QList<QRect> chain = detector->elementChainAt(physical, std::chrono::milliseconds(300));
        if (chain.isEmpty())
            return std::nullopt;
        return chain.first();
    }));
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
                // 层级链（CAP-13）：最内层元素 … 窗口；滚轮切换
                m_elementChain = m_elements->elementChainAt(m_elementRequested, std::chrono::milliseconds(50));
                if (m_elementChain.isEmpty())
                    m_elementChain = {r};
                if (m_windowHighlight && !m_elementChain.contains(*m_windowHighlight))
                    m_elementChain.append(*m_windowHighlight);
                m_elementLevel = 0;
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
    if (m_state == State::Annotating) {
        const SelectionHandle h = m_editor.handleAt(toImage(physical));
        return h == SelectionHandle::Outside ? SelectionHandle::None : h;
    }
    if (m_state != State::Selected && m_state != State::History)
        return SelectionHandle::Outside;
    return hitTestSelection(m_selection, physical, kGrip);
}

void CaptureSession::pressAt(QPoint physical, Qt::MouseButton button)
{
    if (!isActive())
        return;
    m_cursor = physical;
    if (m_state == State::Annotating) {
        if (button == Qt::RightButton && !m_editor.hasDraft() && m_editor.selectedId() < 0 && !m_editor.textEditing()) {
            if (snapAnnotationToElement(physical)) // ANN-11：右键自动框出鼠标下的 UI 元素
                return;
            leaveAnnotating(); // 右键结束当前图形 → 已选定
            return;
        }
        m_editor.pressAt(toImage(physical), button, QGuiApplication::keyboardModifiers());
        m_drag = m_editor.dragMode() == AnnotationEditor::DragMode::None ? Drag::None : Drag::Create; // 让覆盖窗口把移动事件交给 dragTo
        return;
    }
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
    if (m_state == State::Annotating) {
        m_editor.moveTo(toImage(physical));
        return;
    }
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
        m_selection = applyRatio(m_selection, physical.y() < m_dragStart.y() ? SelectionHandle::TopRight : SelectionHandle::BottomRight);
        emit selectionChanged(m_selection);
        break;
    case Drag::Move:
        m_selection = moveSelection(m_dragOrigin, physical - m_dragStart, m_desktop.bounds());
        emit selectionChanged(m_selection);
        layoutToolbar();
        break;
    case Drag::Resize:
        m_selection = applyRatio(resizeSelection(m_dragOrigin, m_dragHandle, physical - m_dragStart), m_dragHandle).intersected(m_desktop.bounds());
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
    if (m_state == State::Annotating) {
        m_drag = Drag::None;
        m_editor.releaseAt(toImage(physical), button);
        return;
    }
    const Drag drag = m_drag;
    if (drag == Drag::Create || drag == Drag::Move || drag == Drag::Resize)
        dragTo(physical); // 松开点也计入（必须在清除拖拽状态之前）
    m_drag = Drag::None;
    switch (drag) {
    case Drag::ClickCandidate:
        // 单击：选中高亮的窗口 / 元素（CAP-11）
        if (m_highlight)
            setSelection(*m_highlight);
        break;
    case Drag::Create:
        setState(m_selection.isEmpty() ? State::Detecting : State::Selected);
        emit selectionChanged(m_selection);
        break;
    case Drag::Move:
    case Drag::Resize:
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
    if (m_state == State::Annotating) {
        m_editor.doubleClickAt(toImage(physical));
        return;
    }
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
    const bool alt = mods & Qt::AltModifier;
    if (key != Qt::Key_Shift)
        m_shiftAlone = false;
    if (key == Qt::Key_Shift) {
        m_editor.setShiftHeld(true);
        m_shiftHeld = true;
    }
    if (m_state == State::Annotating && handleAnnotationKey(e))
        return true;
    // ACT-09：可自定义的快捷键先按映射表分发；没有匹配再走固定键
    if (key != Qt::Key_Shift && key != Qt::Key_Alt && key != Qt::Key_Control && (m_state == State::Selected || m_state == State::History || m_state == State::Annotating)) {
        const ShortcutMap map(m_settings.stringValue(QStringLiteral("shortcuts.map")));
        const QString action = map.actionFor(QStringLiteral("capture"), key, mods);
        if (action == QLatin1String("copy")) { accept(); return true; }
        if (action == QLatin1String("save")) { save(); return true; }
        if (action == QLatin1String("quickSave")) { quickSave(); return true; }
        if (action == QLatin1String("pin")) { pin(); return true; }
        if (action == QLatin1String("print")) { print(); return true; }
        if (action == QLatin1String("ocrCopy")) { ocrCopy(); return true; }
        if (action == QLatin1String("scroll")) { startScroll(); return true; }
        if (action == QLatin1String("refresh")) { refresh(); return true; }
        if (action == QLatin1String("previewSave")) { previewAndSave(); return true; }
    }
    if (key == Qt::Key_F5) {
        refresh();
        return true;
    }
    if (key == Qt::Key_QuoteLeft) {
        setIncludeCursor(!m_includeCursor);
        return true;
    }
    if (key == Qt::Key_Space && m_state != State::Annotating) {
        setSpaceHeld(true);
        return true;
    }
    // 工具快捷键（ANN-13）：Ctrl+数字；从已选定状态直接进入标注
    if (ctrl && !shift && key >= Qt::Key_1 && key <= Qt::Key_8 && (m_state == State::Selected || m_state == State::History || m_state == State::Annotating)) {
        static const AnnotationEditor::Tool tools[] = {AnnotationEditor::Tool::Rect, AnnotationEditor::Tool::Ellipse, AnnotationEditor::Tool::Line,
                                                       AnnotationEditor::Tool::Arrow, AnnotationEditor::Tool::Highlighter, AnnotationEditor::Tool::Mosaic,
                                                       AnnotationEditor::Tool::Blur, AnnotationEditor::Tool::Eraser};
        setTool(tools[key - Qt::Key_1]);
        return true;
    }
    if (!ctrl && (key == Qt::Key_T || key == Qt::Key_B || key == Qt::Key_N || key == Qt::Key_M) && (m_state == State::Selected || m_state == State::History)) {
        setTool(key == Qt::Key_T ? AnnotationEditor::Tool::Text : key == Qt::Key_B ? AnnotationEditor::Tool::Pencil : key == Qt::Key_N ? AnnotationEditor::Tool::Step : AnnotationEditor::Tool::Magnifier);
        return true;
    }
    switch (key) {
    case Qt::Key_Escape:
        if (confirmCancel())
            cancel();
        return true;
    case Qt::Key_U:
        if (ctrl) {
            // CAP-19：切换尺寸单位
            const bool logical = m_settings.stringValue(QStringLiteral("capture.sizeUnit")) == QLatin1String("logical");
            m_settings.setValue(QStringLiteral("capture.sizeUnit"), logical ? QStringLiteral("physical") : QStringLiteral("logical"));
            updateOverlays();
            return true;
        }
        break;
    case Qt::Key_R:
        if (!ctrl) {
            // R：上次区域；Shift+R：更早的区域（CAP-41）
            loadRegionFromHistory(shift ? m_regionHistoryIndex + 1 : 0);
            return true;
        }
        break;
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
    // 复制 / 保存 / 贴图 / 打印 / OCR / 滚动 / 刷新由映射表（ACT-09）分发；这里只剩取色
    case Qt::Key_C:
        if (!ctrl && !shift) {
            copyColor();
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
        if (ctrl)
            cycleMode(); // CAP-07
        else
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
    if (e->key() == Qt::Key_Space) {
        setSpaceHeld(false);
        return true;
    }
    if (e->key() == Qt::Key_Shift) {
        m_editor.setShiftHeld(false);
        m_shiftHeld = false;
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
    if (!m_settings.boolValue(QStringLiteral("capture.magnifierAuto")) || m_state == State::Annotating)
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
    if (!m_historyCursor || !isActive() || m_drag != Drag::None || m_state == State::Annotating)
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
    // 标注过的画面：显示与导出共用文档的渲染（NFR-08）
    if (m_editor.active())
        return m_editor.renderForExport();
    if (m_state == State::History)
        return historyImage();
    if (m_selection.isEmpty())
        return {};
    return m_desktop.crop(m_selection);
}

void CaptureSession::recordHistory(const QImage& image, const QRect& region)
{
    if (m_history && m_state != State::History) {
        m_history->add({image, region, QDateTime::currentDateTime()});
        saveHistoryFile(image);
    }
    pushRegionHistory(region);
}

void CaptureSession::saveHistoryFile(const QImage& image)
{
    const QString dir = m_settings.stringValue(QStringLiteral("capture.historyDir"));
    if (dir.isEmpty() || image.isNull())
        return;
    QDir().mkpath(dir);
    const QString name = QStringLiteral("history_%1.png").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    image.save(QDir(dir).filePath(name), "PNG");
    // 超过上限的旧文件删掉
    QDir d(dir);
    QStringList files = d.entryList({QStringLiteral("history_*.png")}, QDir::Files, QDir::Name);
    const int limit = m_history ? m_history->limit() : 10;
    while (files.size() > limit)
        d.remove(files.takeFirst());
}

QList<QRect> CaptureSession::regionHistory() const
{
    QList<QRect> out;
    for (const QString& item : m_settings.stringValue(QStringLiteral("capture.regionHistory")).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const QStringList p = item.split(QLatin1Char(','));
        if (p.size() == 4)
            out.append(QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt()));
    }
    return out;
}

void CaptureSession::pushRegionHistory(const QRect& region)
{
    if (region.isEmpty())
        return;
    QList<QRect> list = regionHistory();
    list.removeAll(region);
    list.prepend(region);
    const int limit = std::max(1, m_settings.intValue(QStringLiteral("capture.regionHistoryLimit")));
    while (list.size() > limit)
        list.removeLast();
    QStringList items;
    for (const QRect& r : list)
        items.append(QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    m_settings.setValue(QStringLiteral("capture.regionHistory"), items.join(QLatin1Char(';')));
}

bool CaptureSession::loadRegionFromHistory(int index)
{
    const QList<QRect> list = regionHistory();
    if (!isActive() || list.isEmpty() || m_state == State::Annotating)
        return false;
    index = ((index % list.size()) + static_cast<int>(list.size())) % static_cast<int>(list.size());
    const QRect r = list[index].intersected(m_desktop.bounds());
    if (r.isEmpty())
        return false;
    m_regionHistoryIndex = index;
    setSelection(r);
    return true;
}

QImage CaptureSession::accept()
{
    if (!isActive())
        return {};
    if (m_mode == Mode::Scroll && !m_selection.isEmpty()) {
        startScroll();
        return {};
    }
    if (m_mode == Mode::Record && !m_selection.isEmpty()) {
        const QRect region = m_selection;
        finish();
        emit recordRequested(region);
        return {};
    }
    const QImage image = finalImage();
    if (image.isNull())
        return {};
    runActions(m_actionsOverride.isEmpty() ? m_settings.stringValue(QStringLiteral("capture.enterActions")).split(QLatin1Char(','), Qt::SkipEmptyParts) : m_actionsOverride);
    return image;
}

void CaptureSession::ocrCopy()
{
    if (!isActive())
        return;
    const QImage image = finalImage();
    if (image.isNull())
        return;
    recordHistory(image, m_selection);
    emit ocrCopyRequested(image);
    finish();
}

void CaptureSession::detectCodes()
{
    if (!isActive())
        return;
    m_codes = decodeCodes(currentImage());
    m_toolbar->syncFromEditor();
    if (!m_codes.isEmpty())
        emit codesRequested(m_codes);
}

void CaptureSession::autoDetectCodes()
{
    const QString mode = m_settings.stringValue(QStringLiteral("capture.qrDetect"));
    ++m_codesVersion;
    if (mode != QLatin1String("auto") && mode != QLatin1String("show"))
        return;
    const QImage image = currentImage();
    if (image.isNull())
        return;
    m_codesWatcher.setProperty("version", m_codesVersion);
    m_codesWatcher.setFuture(QtConcurrent::run([image] { return decodeCodes(image); }));
}

void CaptureSession::onCodesResult()
{
    // 结果属于发起时的版本：选区或会话已变化就丢弃
    if (m_codesWatcher.property("version").toInt() != m_codesVersion || m_state != State::Selected)
        return;
    m_codes = m_codesWatcher.result();
    m_toolbar->syncFromEditor();
    if (!m_codes.isEmpty() && m_settings.stringValue(QStringLiteral("capture.qrDetect")) == QLatin1String("show"))
        emit codesRequested(m_codes);
}

void CaptureSession::hideOverlaysTemporarily(bool hide)
{
    for (auto* w : std::as_const(m_overlays))
        w->setVisible(!hide && m_showOverlays);
    m_toolbar->setVisible(!hide && m_showOverlays && (m_state == State::Selected || m_state == State::History || m_state == State::Annotating));
}

void CaptureSession::save()
{
    if (!isActive() || !m_output)
        return;
    const QImage image = finalImage();
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
    const QImage image = finalImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    if (m_output->quickSave(image).isEmpty())
        return;
    recordHistory(image, region);
    emit captured(image, region);
    finish();
}

std::optional<WindowInfo> CaptureSession::sourceWindow() const
{
    if (m_selection.isEmpty())
        return m_windowFinder.windowAt(m_cursor);
    return m_windowFinder.windowAt(m_selection.center());
}

void CaptureSession::previewAndSave()
{
    if (!isActive() || !m_output)
        return;
    const QImage image = finalImage();
    if (image.isNull())
        return;
    hideOverlaysTemporarily(true);
    EncodePreviewDialog dialog(image, *m_output);
    const bool ok = dialog.exec() == QDialog::Accepted;
    if (isActive())
        hideOverlaysTemporarily(false);
    if (!ok)
        return;
    m_settings.setValue(QStringLiteral("output.saveFormat"), dialog.format());
    if (dialog.format() != QLatin1String("png")) {
        m_settings.setValue(QStringLiteral("output.qualityAuto"), false);
        m_settings.setValue(QStringLiteral("output.jpegQuality"), dialog.quality());
    }
    save();
}

void CaptureSession::print()
{
    if (!isActive() || !m_output)
        return;
    const QImage image = finalImage();
    if (image.isNull())
        return;
    hideOverlaysTemporarily(true);
    const bool ok = m_output->print(image, nullptr);
    if (!ok) {
        if (isActive())
            hideOverlaysTemporarily(false);
        return;
    }
    recordHistory(image, m_selection);
    emit captured(image, m_selection);
    finish();
}

void CaptureSession::pin()
{
    if (!isActive())
        return;
    const QImage image = finalImage();
    if (image.isNull())
        return;
    const QRect region = m_selection;
    recordHistory(image, region);
    emit captured(image, region);
    finish();
    emit pinRequested(image, region);
}

void CaptureSession::startScroll()
{
    if (!isActive() || m_selection.isEmpty())
        return;
    const QRect region = m_selection;
    finish();
    emit scrollRequested(region);
}

void CaptureSession::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    if (m_toolbar)
        m_toolbar->syncFromEditor();
    updateOverlays();
}

QString CaptureSession::modeName() const
{
    switch (m_mode) {
    case Mode::Scroll: return tr("Scroll");
    case Mode::Record: return tr("Record");
    case Mode::Static: break;
    }
    return tr("Capture");
}

void CaptureSession::cycleMode()
{
    if (!isActive() || m_state == State::Annotating)
        return;
    setMode(m_mode == Mode::Static ? Mode::Scroll : m_mode == Mode::Scroll ? Mode::Record : Mode::Static);
}

QString CaptureSession::sizeText(const QRect& physical) const
{
    if (m_settings.stringValue(QStringLiteral("capture.sizeUnit")) == QLatin1String("logical")) {
        const QRect screen = screenRectAt(physical.center());
        qreal scale = 1.0;
        for (const auto& snap : m_desktop.snapshots())
            if (snap.screen.physicalGeometry == screen)
                scale = snap.screen.scale;
        return QStringLiteral("%1 × %2").arg(qRound(physical.width() / scale)).arg(qRound(physical.height() / scale));
    }
    return QStringLiteral("%1 × %2").arg(physical.width()).arg(physical.height());
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
    if (!m_selection.isEmpty() && m_settings.boolValue(QStringLiteral("capture.historyIncludeCancelled")) && m_state != State::History) {
        const QImage image = currentImage();
        if (!image.isNull())
            recordHistory(image, m_selection); // CAP-42
    }
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
    m_mode = Mode::Static;
    if (m_transparentBoard) {
        m_transparentBoard = false;
        for (auto* w : std::as_const(m_overlays))
            w->setTransparentMode(false);
    }
    m_actionsOverride.clear();
    m_codes.clear();
    ++m_codesVersion;
    m_regionHistoryIndex = -1;
    m_drag = Drag::None;
    m_editor.end();
    m_annotatedBase = QImage();
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
    const bool show = isActive() && m_showOverlays && (m_state == State::Selected || m_state == State::History || m_state == State::Annotating)
        && !m_selection.isEmpty() && !dragging();
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
