#pragma once

#include "core/capture/CaptureHistory.h"
#include "core/capture/SelectionGeometry.h"
#include "core/capture/WindowFinder.h"
#include "core/color/ColorFormat.h"
#include "core/geometry/VirtualDesktop.h"
#include "core/ports/IElementDetector.h"
#include "core/ports/IScreenCapture.h"
#include "core/ports/IWindowDetector.h"

#include <QFutureWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QTimer>
#include <optional>

class QKeyEvent;

namespace ss {

class OverlayWindow;
class CaptureToolbar;
class Settings;
class OutputActions;

// 截图会话：冻结画面 → 检测 / 框选 → 调整 → 输出。
// 状态与选区全部用物理像素；覆盖窗口只负责把 Qt 的输入和绘制换算成物理坐标后转交到这里。
// 数据单向流动：屏幕快照（只读）→ VirtualDesktop → 选区裁剪 → 结果位图 → 输出动作 → 历史；结束时整份快照释放。
class CaptureSession : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Detecting, Selecting, Selected, History };
    Q_ENUM(State)
    enum class DetectMode { Element, Window, Off };
    Q_ENUM(DetectMode)
    enum class Drag { None, Create, Move, Resize, ClickCandidate };

    struct Timings {
        qint64 triggerNs = 0;
        qint64 captureDoneNs = 0;
        qint64 shownNs = 0;
        qint64 firstPaintNs = 0;
        bool complete() const { return firstPaintNs != 0; }
    };

    CaptureSession(IScreenCapture* capture, IWindowDetector* windows, IElementDetector* elements, Settings& settings, QObject* parent = nullptr);
    ~CaptureSession() override;

    void setScreenCapture(IScreenCapture* capture) { m_capture = capture; }
    IScreenCapture* screenCapture() const { return m_capture; }
    QString screenCaptureBackend() const { return m_capture ? m_capture->backendName() : QString(); }
    void setHistory(CaptureHistory* history) { m_history = history; }
    void setOutput(OutputActions* output) { m_output = output; }

    // ---- 生命周期 ----
    void prepareOverlays();
    bool start(qint64 triggerNs, bool showOverlays = true);
    bool isActive() const { return m_state != State::Idle; }
    State state() const { return m_state; }
    void cancel();

    // ---- 选区（物理像素）----
    QRect selection() const { return m_selection; }
    void setSelection(const QRect& physical);
    void selectFullScreen(); // Ctrl+A：当前屏幕 / 全部屏幕轮换（CAP-15）

    // ---- 检测（CAP-11、CAP-12）----
    DetectMode detectMode() const { return m_detectMode; }
    void setDetectMode(DetectMode mode);
    void toggleDetectMode(); // Tab
    std::optional<QRect> highlight() const { return m_highlight; }
    const WindowFinder& windowFinder() const { return m_windowFinder; }

    // ---- 鼠标（物理坐标），由覆盖窗口转交 ----
    void hoverAt(QPoint physical);
    void pressAt(QPoint physical, Qt::MouseButton button);
    void dragTo(QPoint physical);
    void releaseAt(QPoint physical, Qt::MouseButton button);
    void doubleClickAt(QPoint physical);
    SelectionHandle handleAt(QPoint physical) const;
    Drag dragKind() const { return m_drag; }
    bool dragging() const { return m_drag == Drag::Create || m_drag == Drag::Move || m_drag == Drag::Resize; }
    // 兼容 M0 接口
    void beginDrag(QPoint physical) { pressAt(physical, Qt::LeftButton); }
    void endDrag(QPoint physical) { releaseAt(physical, Qt::LeftButton); }

    // ---- 键盘 ----
    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyRelease(QKeyEvent* event);
    void setAltHeld(bool held);
    bool magnifierVisible() const;
    QPoint cursor() const { return m_cursor; }

    // ---- 取色（CAP-23）----
    ColorFormat colorFormat() const { return m_colorFormat; }
    void cycleColorFormat();
    QString colorTextAtCursor() const;
    void copyColor();

    // ---- 历史（CAP-40）----
    void historyOlder();
    void historyNewer();
    int historyIndex() const { return m_historyCursor ? m_historyCursor->index() : -1; }
    QImage historyImage() const; // 正在查看的历史图（State::History 时）

    // ---- 输出 ----
    QImage currentImage() const; // 选区裁剪或历史图
    QImage accept();             // 复制并结束（OUT-01）
    void copy() { accept(); }
    void save();                 // OUT-03
    void quickSave();            // OUT-08
    void pin();                  // PIN-02

    const VirtualDesktop& desktop() const { return m_desktop; }
    Timings timings() const { return m_timings; }
    QList<OverlayWindow*> overlays() const { return m_overlays.values(); }
    OverlayWindow* overlayAt(QPoint physical) const;
    CaptureToolbar* toolbar() const { return m_toolbar; }
    Settings& settings() { return m_settings; }

    // 焦点丢失（CAP-03）：覆盖窗口在失去激活时调用
    void onFocusLost();

signals:
    void captured(const QImage& image, const QRect& physicalRegion);
    void cancelled();
    void selectionChanged(const QRect& physical);
    void stateChanged(ss::CaptureSession::State state);
    void highlightChanged();
    void cursorChanged(QPoint physical);
    void started();
    void pinRequested(const QImage& image, const QRect& physicalRegion);
    void colorCopied(const QString& text);
    void cursorMoveRequested(QPoint physical);

private:
    void setState(State state);
    void finish();
    void recordHistory(const QImage& image, const QRect& region);
    void updateHighlight();
    void requestElementAt(QPoint physical);
    void onElementResult();
    void nudge(Qt::Key key, NudgeKind kind);
    void moveCursorBy(QPoint delta);
    void layoutToolbar();
    void updateOverlays();
    void onOverlayFirstPaint(qint64 atNs);
    void hideOverlaysTemporarily(bool hide);
    bool confirmCancel();
    QRect screenRectAt(QPoint physical) const;

    IScreenCapture* m_capture;
    IWindowDetector* m_windows;
    IElementDetector* m_elements;
    Settings& m_settings;
    CaptureHistory* m_history = nullptr;
    OutputActions* m_output = nullptr;
    VirtualDesktop m_desktop;
    WindowFinder m_windowFinder;
    QHash<QString, OverlayWindow*> m_overlays;
    CaptureToolbar* m_toolbar = nullptr;
    State m_state = State::Idle;
    DetectMode m_detectMode = DetectMode::Element;
    QRect m_selection;
    std::optional<QRect> m_highlight;
    std::optional<QRect> m_windowHighlight;
    QPoint m_cursor;
    // 拖拽
    Drag m_drag = Drag::None;
    QPoint m_dragStart;
    QPoint m_dragCurrent;
    QRect m_dragOrigin;
    SelectionHandle m_dragHandle = SelectionHandle::None;
    // 元素检测
    QFutureWatcher<std::optional<QRect>> m_elementWatcher;
    QPoint m_elementRequested;
    std::optional<QPoint> m_elementQueued;
    bool m_elementPending = false;
    // 键盘与放大镜
    bool m_altHeld = false;
    bool m_shiftAlone = false;
    bool m_fullScreenAll = false;
    ColorFormat m_colorFormat = ColorFormat::Hex;
    // 历史
    std::unique_ptr<HistoryCursor> m_historyCursor;
    Timings m_timings;
    bool m_showOverlays = true;
};

} // namespace ss
