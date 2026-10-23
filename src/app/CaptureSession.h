#pragma once

#include "core/geometry/VirtualDesktop.h"
#include "core/ports/IScreenCapture.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>

namespace ss {

class OverlayWindow;

// 截图会话：冻结画面 → 覆盖窗口框选 → 裁剪输出。数据单向流动：
// 屏幕快照（只读）→ VirtualDesktop → 选区裁剪 → 结果位图 → captured 信号；结束时整份快照释放。
class CaptureSession : public QObject {
Q_OBJECT
public:
    struct Timings {
        qint64 triggerNs = 0;      // 热键 / 命令到达
        qint64 captureDoneNs = 0;  // 全部显示器抓取完成
        qint64 shownNs = 0;        // 覆盖窗口 show() 返回
        qint64 firstPaintNs = 0;   // 第一个覆盖窗口完成首次绘制
        bool complete() const { return firstPaintNs != 0; }
    };

    explicit CaptureSession(IScreenCapture* capture, QObject* parent = nullptr);
    ~CaptureSession() override;

    void setScreenCapture(IScreenCapture* capture) { m_capture = capture; }
    IScreenCapture* screenCapture() const { return m_capture; }
    QString screenCaptureBackend() const { return m_capture ? m_capture->backendName() : QString(); }

    // 按当前显示器布局创建（或复用）覆盖窗口，保持隐藏
    void prepareOverlays();

    // 开始截图。showOverlays=false 时只冻结画面不显示窗口（测试合成布局用）。
    bool start(qint64 triggerNs, bool showOverlays = true);
    bool isActive() const { return m_active; }

    // 选区（物理像素）。拖拽过程由覆盖窗口调用。
    QRect selection() const { return m_selection; }
    void setSelection(const QRect& physical);
    void beginDrag(QPoint physical);
    void dragTo(QPoint physical);
    void endDrag(QPoint physical);
    bool dragging() const { return m_dragging; }

    // 裁出选区并结束会话；没有选区返回空图像且会话继续
    QImage accept();
    void cancel();

    const VirtualDesktop& desktop() const { return m_desktop; }
    Timings timings() const { return m_timings; }
    QList<OverlayWindow*> overlays() const { return m_overlays.values(); }
    OverlayWindow* overlayAt(QPoint physical) const;

signals:
    void captured(const QImage& image, const QRect& physicalRegion);
    void cancelled();
    void selectionChanged(const QRect& physical);
    void started();

private:
    void finish();
    void onOverlayFirstPaint(qint64 atNs);

    IScreenCapture* m_capture;
    VirtualDesktop m_desktop;
    QHash<QString, OverlayWindow*> m_overlays; // 按显示器 id
    QRect m_selection;
    QPoint m_dragStart;
    bool m_dragging = false;
    bool m_active = false;
    Timings m_timings;
};

} // namespace ss
