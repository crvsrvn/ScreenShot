#pragma once

#include "app/AnnotationEditor.h"
#include "app/ToolbarTarget.h"
#include "core/geometry/VirtualDesktop.h"
#include "core/util/Mipmap.h"

#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <memory>

class QMenu;

namespace ss {

class Settings;
class OutputActions;
class CaptureToolbar;

// 贴图窗口（PIN-*）：无边框、置顶、透明背景、带缓存阴影。
// - 内容按物理像素定位：scale=1 时一个图像像素正好盖住一个屏幕像素（PIN-02 位置重合）
// - 窗口只覆盖内容在桌面上可见的部分；拖动不重绘；缩放交互中用多级缩小图，停下后平滑
// - 旋转 / 翻转作用于底图；标注（Space 工具栏）经 AnnotationEditor；显示与复制共用同一渲染
class PinWindow : public QWidget, public ToolbarTarget {
    Q_OBJECT
public:
    PinWindow(const QImage& image, Settings& settings, OutputActions* output = nullptr);
    ~PinWindow() override;

    // ---- 几何（物理像素）----
    void moveToPhysical(QPoint physicalTopLeft);
    QPoint physicalTopLeft() const;
    QRect physicalContentRect() const;
    QPointF contentTopLeft() const { return m_contentTopLeft; } // 逻辑桌面坐标
    void moveContentTo(QPointF logicalTopLeft);
    QRectF contentRect() const;                                 // 逻辑桌面坐标
    qreal devicePixelRatioForContent() const { return m_dpr; }
    bool isClipped() const { return m_clipped; }

    // ---- 缩放（PIN-22、PIN-25）----
    qreal scale() const { return m_scale; }
    void zoomAt(QPointF anchorLogical, qreal factor, bool interactive);
    void setScale(qreal scale, bool interactive);
    void zoomStep(int steps, QPointF anchorLogical);
    void fitToScreen();
    void toggleReset(); // 中键：100% 与不透明 ↔ 之前的状态

    // ---- 透明度（PIN-24）----
    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);
    void adjustOpacity(int percentDelta);

    // ---- 变换（PIN-26）----
    void rotateQuarter(int turns); // +1 顺时针 90°
    void flipHorizontal();
    void flipVertical();
    int rotation() const { return m_rotation; }
    bool flippedH() const { return m_flipH; }
    bool flippedV() const { return m_flipV; }

    // ---- 穿透（PIN-33）----
    bool passthrough() const { return m_passthrough; }
    void setPassthrough(bool on);

    // ---- 图像 ----
    const QImage& originalImage() const { return m_source; }
    QImage displayedImage() const; // 变换 + 标注（原始像素尺寸）
    void copyDisplayed();
    void copyOriginal();
    void saveDisplayed();

    // ---- 标注（PIN-38、ANN-01）；ToolbarTarget ----
    void toggleToolbar();
    bool toolbarVisible() const;
    CaptureToolbar* toolbar() const { return m_toolbar; }
    AnnotationEditor& editor() override { return *m_editor; }
    Settings& settings() override { return m_settings; }
    bool annotating() const override { return m_annotating; }
    void setTool(AnnotationEditor::Tool tool) override;
    void copy() override { copyDisplayed(); }
    void save() override { saveDisplayed(); }
    void quickSave() override;
    void pin() override;  // 复制为新贴图
    void cancel() override; // 关闭工具栏
    bool supportsPin() const override { return true; }

    // ---- 生命周期（PIN-50、PIN-51）----
    void closePin();   // 进入历史
    void destroyPin(); // 不进历史
    QJsonObject stateJson() const; // 恢复 / 重启用
    void applyStateJson(const QJsonObject& o);

    // ---- 右键菜单（PIN-44）----
    QMenu* buildMenu();

    // ---- 测试 / 统计 ----
    int paintCount() const { return m_paintCount; }
    QList<double> takePaintDurationsMs();
    bool magnifierVisible() const { return m_altHeld; }
    QString colorTextAt(QPoint physical) const;

signals:
    void closed(ss::PinWindow* pin);
    void destroyed(ss::PinWindow* pin);
    void pinRequested(const QImage& image, QPoint physicalTopLeft);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QSizeF contentSizeLogical() const;
    QRectF desktopBounds() const;
    void relayout();
    void applyNativeGeometry();
    void rebuildTransformed();
    void refreshDisplay();
    void drawShadow(QPainter& painter, const QRectF& content);
    void finishInteraction();
    void layoutToolbar();
    QPointF toImage(QPointF logicalInWindow) const;
    QPoint toPhysical(QPointF logicalInWindow) const;
    bool onContentEdge(QPointF logicalInWindow) const;
    void ensureMagnifierDesktop();

    static constexpr int kShadowMargin = 24;

    Settings& m_settings;
    OutputActions* m_output;
    QImage m_source;       // 原图
    QImage m_transformed;  // 旋转 / 翻转后
    QImage m_display;      // 加标注后（原始像素尺寸）
    Mipmap m_mipmap;
    std::unique_ptr<AnnotationEditor> m_editor;
    CaptureToolbar* m_toolbar = nullptr;
    bool m_annotating = false;
    QPointF m_contentTopLeft; // 逻辑桌面坐标
    qreal m_dpr = 1.0;
    qreal m_scale = 1.0;
    qreal m_opacity = 1.0;
    int m_rotation = 0;
    bool m_flipH = false;
    bool m_flipV = false;
    bool m_passthrough = false;
    qreal m_opacityBeforePassthrough = 1.0;
    // 重置记忆
    bool m_resetActive = false;
    qreal m_scaleBeforeReset = 1.0;
    qreal m_opacityBeforeReset = 1.0;
    // 交互
    bool m_interactive = false;
    bool m_clipped = false;
    QPixmap m_smooth;
    QTimer m_settleTimer;
    QPointF m_dragOffset;
    bool m_dragging = false;
    bool m_edgeDragging = false;
    QPointF m_edgeStart;
    qreal m_edgeStartScale = 1.0;
    bool m_altHeld = false;
    QPoint m_cursorPhysical;
    std::unique_ptr<VirtualDesktop> m_magnifierDesktop;
    int m_paintCount = 0;
    QList<double> m_paintDurationsMs;
};

} // namespace ss
