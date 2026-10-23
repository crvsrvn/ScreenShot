#pragma once

#include "app/AnnotationEditor.h"
#include "app/ToolbarTarget.h"
#include "core/capture/CaptureHistory.h"
#include "core/capture/SelectionGeometry.h"
#include "core/capture/WindowFinder.h"
#include "core/code/CodeDecoder.h"
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
class CaptureSession : public QObject, public ToolbarTarget {
    Q_OBJECT
public:
    enum class State { Idle, Detecting, Selecting, Selected, History, Annotating };
    Q_ENUM(State)
    enum class DetectMode { Element, Window, Off };
    Q_ENUM(DetectMode)
    enum class Drag { None, Create, Move, Resize, ClickCandidate };
    // 截图模式（CAP-07）：同一界面里轮换静态截图 / 滚动截图 / 屏幕录制
    enum class Mode { Static, Scroll, Record };
    Q_ENUM(Mode)

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
    // 白板（CAP-50）：纯色全屏画布，直接进入标注
    bool startWhiteboard(const QColor& color, bool currentScreenOnly, bool showOverlays = true);
    // 刷新截图（CAP-31）：保留选区重新抓取
    bool refresh();
    // 模式轮换（CAP-07）：Ctrl+Tab；确认时按模式进入滚动截图 / 录制
    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    // 透明白板（CAP-51）：在实时桌面上标注，确认时抓取桌面并合成
    bool startTransparentBoard(bool showOverlays = true);
    bool transparentBoard() const { return m_transparentBoard; }
    // 尺寸文字（CAP-19）：按设置用物理或逻辑像素
    QString sizeText(const QRect& physical) const;
    bool altHeld() const { return m_altHeld; }
    // 区域历史（CAP-41）：R 载入上次区域，Shift+R 依次载入更早的
    QList<QRect> regionHistory() const;
    bool loadRegionFromHistory(int index);
    bool isActive() const { return m_state != State::Idle; }
    State state() const { return m_state; }

    // ---- 选区（物理像素）----
    QRect selection() const { return m_selection; }
    void setSelection(const QRect& physical);
    void selectFullScreen(); // Ctrl+A：当前屏幕 / 全部屏幕轮换（CAP-15）
    // 直接输入宽高（CAP-17）：保持左上角
    void setSelectionSize(int width, int height);
    // 固定比例（CAP-18）：锁定当前宽高比；Shift 拖拽为 1:1
    void setRatioLocked(bool locked) override;
    bool ratioLocked() const override { return m_ratioLocked; }
    bool supportsCaptureOptions() const override { return true; }
    // 拖拽调整大小时按住 Space 改为移动（CAP-16）
    void setSpaceHeld(bool held);
    // 截取鼠标指针（CAP-30）：` 切换
    bool includeCursor() const { return m_includeCursor; }
    void setIncludeCursor(bool on);
    // 层级元素（CAP-13）：滚轮切换父 / 子元素
    void cycleElement(int delta);
    int elementLevel() const { return m_elementLevel; }
    // 输出装饰（CAP-32、CAP-33）：圆角 / 阴影 / 边框开关，作用于复制、保存、贴图
    bool shadowEnabled() const override { return m_shadow; }
    bool borderEnabled() const override { return m_border; }
    void setShadowEnabled(bool on) override;
    void setBorderEnabled(bool on) override;
    std::optional<CursorImage> cursorImage() const { return m_cursorImage; }
    // 应用装饰与指针后的最终输出图
    QImage finalImage() const;
    // 截图后动作（CAP-04）：按固定顺序执行列表里的动作，然后结束
    void runActions(const QStringList& actions);
    // 命令行指定的本次输出组合（ACT-06），结束后清除
    void setEnterActionsOverride(const QStringList& actions) { m_actionsOverride = actions; }
    // OCR（REC-01）：Shift+C 识别选区并复制，然后结束
    void ocrCopy();
    // 二维码 / 条码（REC-06）：当前选区里识别到的码；detectCodes 立即识别并发 codesRequested
    QList<CodeResult> detectedCodes() const { return m_codes; }
    void detectCodes() override;
    QString modeName() const override;
    void cycleMode() override;
    bool hasCodes() const override { return !m_codes.isEmpty(); }

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

    // ---- 标注（M2）；ToolbarTarget ----
    AnnotationEditor& editor() override { return m_editor; }
    Settings& settings() override { return m_settings; }
    bool annotating() const override { return m_state == State::Annotating; }
    // 选择标注工具：从已选定 / 历史状态进入标注；Tool::None 不退出标注
    void setTool(AnnotationEditor::Tool tool) override;
    // 结束标注回到已选定（右键）；标注保留在导出结果里
    void leaveAnnotating();

    // ---- 历史（CAP-40）----
    void historyOlder();
    void historyNewer();
    int historyIndex() const { return m_historyCursor ? m_historyCursor->index() : -1; }
    QImage historyImage() const; // 正在查看的历史图（State::History 时）

    // ---- 输出 ----
    QImage currentImage() const; // 选区裁剪或历史图
    QImage accept();             // 复制并结束（OUT-01）
    void copy() override { accept(); }
    void save() override;        // OUT-03
    void quickSave() override;   // OUT-08
    void pin() override;         // PIN-02
    void print() override;       // OUT-11
    // 截图来源窗口（选区中心所在的窗口），用于文件名变量（OUT-10）
    std::optional<WindowInfo> sourceWindow() const;
    void cancel() override;
    void startScroll() override; // CAP-60：结束本会话并以当前选区开始滚动截图
    bool supportsScroll() const override { return true; }

    const VirtualDesktop& desktop() const { return m_desktop; }
    Timings timings() const { return m_timings; }
    QList<OverlayWindow*> overlays() const { return m_overlays.values(); }
    OverlayWindow* overlayAt(QPoint physical) const;
    CaptureToolbar* toolbar() const { return m_toolbar; }

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
    void scrollRequested(const QRect& physicalRegion); // CAP-60：选区后点“滚动截图”
    void recordRequested(const QRect& physicalRegion); // CAP-07 / VID-01：录制模式下确认
    void ocrCopyRequested(const QImage& image);         // CAP-04 的 OCR 复制动作 / Shift+C
    void codesRequested(const QList<ss::CodeResult>& codes); // REC-06：显示识别到的码
    void sizeInputRequested();                          // CAP-17：点击尺寸文字
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

    bool handleAnnotationKey(QKeyEvent* e);
    bool snapAnnotationToElement(QPoint physical); // ANN-11
    QPointF toImage(QPoint physical) const { return QPointF(physical - m_selection.topLeft()); }
    bool startWithSnapshots(qint64 triggerNs, QList<ScreenSnapshot> snapshots, bool showOverlays);
    QRect applyRatio(const QRect& r, SelectionHandle handle) const;

    IScreenCapture* m_capture;
    IWindowDetector* m_windows;
    IElementDetector* m_elements;
    Settings& m_settings;
    AnnotationEditor m_editor;
    QImage m_annotatedBase; // 进入标注时的底图（历史图或裁剪）
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
    bool m_shiftHeld = false;
    bool m_spaceHeld = false;
    bool m_fullScreenAll = false;
    bool m_ratioLocked = false;
    double m_lockedRatio = 1.0;
    bool m_includeCursor = false;
    std::optional<CursorImage> m_cursorImage;
    QList<QRect> m_elementChain;
    int m_elementLevel = 0;
    bool m_shadow = false;
    bool m_border = false;
    bool m_whiteboard = false;
    bool m_transparentBoard = false;
    Mode m_mode = Mode::Static;
    QString m_cursorProcess; // 光标所在窗口的进程名（检测排除列表用）
    void saveHistoryFile(const QImage& image);
    QStringList m_actionsOverride;
    int m_regionHistoryIndex = -1;
    void pushRegionHistory(const QRect& region);
    QList<CodeResult> m_codes;
    QFutureWatcher<QList<CodeResult>> m_codesWatcher; // 自动检测在工作线程做，不卡界面
    int m_codesVersion = 0;
    void autoDetectCodes();
    void onCodesResult();
    ColorFormat m_colorFormat = ColorFormat::Hex;
    // 历史
    std::unique_ptr<HistoryCursor> m_historyCursor;
    Timings m_timings;
    bool m_showOverlays = true;
};

} // namespace ss
