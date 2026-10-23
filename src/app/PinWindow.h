#pragma once

#include "app/AnnotationEditor.h"
#include "app/ToolbarTarget.h"
#include "core/geometry/VirtualDesktop.h"
#include "core/pin/TextPin.h"
#include "core/ports/IOcrEngine.h"
#include "core/util/Mipmap.h"

#include <QImage>
#include <QJsonObject>
#include <QPixmap>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <utility>

class QMenu;
class QMimeData;

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
    enum class ContentKind { Image, Text, Color, File };

    PinWindow(const QImage& image, Settings& settings, OutputActions* output = nullptr);
    ~PinWindow() override;

    // ---- 内容（PIN-05 ~ PIN-07）----
    ContentKind contentKind() const { return m_kind; }
    void setTextContent(const QString& text, const TextPinOptions& options); // 纯文本或 HTML
    void setIgnoreFormatting(bool ignore);                                   // HTML 贴图：忽略 / 恢复格式
    bool ignoringFormatting() const { return m_kind == ContentKind::Text && !m_textOptions.html; }
    QString textPlain() const;
    QString textHtml() const;
    void copyText();                       // 全文或选中部分（纯文本 + HTML）
    void setTextSelectionMode(bool on);    // 文本选择模式：拖动只选部分文字；图像贴图按 OCR 行选（REC-04）
    void setOcrLines(const QList<OcrLine>& lines);
    QList<OcrLine> ocrLines() const { return m_ocrLines; }
    QList<int> selectedOcrLines() const { return m_ocrSelected; }
    void selectOcrLinesIn(const QRectF& imageRect);
    bool textSelectionMode() const { return m_textSelecting; }
    void selectTextRange(int from, int to);
    QString selectedText() const;
    QList<QRectF> textSelectionRectsImage() const;
    void setColorContent(const QColor& color, qreal scale);
    QColor color() const { return m_color; }
    void copyColorAs(const QString& format); // hex / rgb / hsv / hsl / float
    void editColorValue();
    // 文件卡片（PIN-08）
    void setFileContent(const QString& path, qreal scale);
    QString filePath() const { return m_filePath; }
    void openFile();
    void copyFile();
    void copyFilePath();
    void revealFile();

    // ---- 亮度（PIN-28）：7 / 8 ±10%，0 重置全部处理 ----
    int brightness() const { return m_brightness; }
    void setBrightness(int percent);
    void resetProcessing();
    // ---- 锁定（PIN-34）：L；锁定类型见设置 pin.lockType ----
    bool locked() const { return m_locked; }
    void setLocked(bool on);
    bool lockBlocks(const QString& what) const; // move / zoom / close
    // ---- 标题（PIN-35）：F2 ----
    QString title() const { return m_title; }
    void setTitle(const QString& title);
    void editTitle();
    // ---- 拖出到其他程序（PIN-40）：Ctrl+左键拖动 ----
    QMimeData* makeDragMimeData() const;
    // ---- 缩放细节（PIN-23）：按设置的中心 / 步长 ----
    QPointF zoomAnchorLogical(QPointF cursorLogical) const;
    void zoomStepFine(int steps, QPointF anchorLogical);
    // ---- 灰度 / 反色（PIN-27）----
    bool grayscale() const { return m_gray; }
    void setGrayscale(bool on);
    bool inverted() const { return m_invert; }
    void setInverted(bool on);

    // ---- 缩略图 / 显示范围 / 裁剪（PIN-29 ~ PIN-31）----
    // 临时隐藏标注（ANN-16）
    bool annotationsHidden() const { return m_annotationsHidden; }
    void setAnnotationsHidden(bool hidden);
    bool hasAnnotations() const { return !m_annotated.isNull(); }
    bool thumbnail() const { return m_thumbnail; }
    void toggleThumbnail();
    QRectF viewRect() const { return m_viewRect; }   // 完整显示图坐标；空 = 全部
    void setViewRect(const QRectF& displayRect);     // 以当前显示图坐标给出
    void clearViewRect();
    void panView(QPointF imageDelta);
    QRect cropRect() const { return m_cropRect; }    // 原图坐标；空 = 未裁剪
    void cropTo(const QRectF& displayRect);
    void restoreCrop();
    void confirmCrop();

    // ---- 置顶（PIN-32）、阴影（PIN-36）、选中（PIN-60）、分组 ----
    bool topmost() const { return m_topmost; }
    void setTopmost(bool on);
    QColor shadowColor() const;
    void setSelected(bool on);
    bool selected() const { return m_selected; }
    void setGroupIndex(int index);
    int groupIndex() const { return m_groupIndex; }
    void setGroupColor(const QColor& c) { m_groupColor = c; update(); }
    // 组列表（右键“移到组”菜单用，GRP-03）
    void setGroupNames(const QStringList& names) { m_groupNames = names; }

    // ---- 吸附（PIN-21）：Shift 拖动时吸附到这些矩形（逻辑桌面坐标）的边 ----
    void setSnapTargets(std::function<QList<QRectF>()> targets) { m_snapTargets = std::move(targets); }
    QPointF snappedTopLeft(QPointF proposed) const;

    // 鼠标动作（ACT-05）：close / destroy / copy / reset / thumbnail / menu / passthrough / topmost / none
    void runMouseAction(const QString& action, QPoint globalPos);

    // 关闭 / 销毁前的确认（PIN-53）；测试可替换
    static std::function<bool(QWidget*, const QString&)> confirmHandler;

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
    void closePin(bool userGesture = false);   // 进入历史；userGesture 时按设置二次确认（PIN-53）
    void destroyPin(bool userGesture = false); // 不进历史
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
    void replaceRequested(ss::PinWindow* pin, const QMimeData* mime); // PIN-11：Ctrl+V / 拖入
    void selectToggleRequested(ss::PinWindow* pin);                   // PIN-60：Ctrl+单击
    void selectAllRequested();                                        // PIN-60：Ctrl+A
    // 多选时的联动（由管理器转发给其他选中贴图）
    void groupMoved(ss::PinWindow* pin, QPointF logicalDelta);
    void groupAction(ss::PinWindow* pin, const QString& action, double arg); // close / destroy / reset / opacity
    void ocrRequested(const QImage& image);    // REC-01：贴图右键识别
    void codesRequested(const QImage& image);  // REC-06：贴图右键识别二维码
    void moveToGroupRequested(ss::PinWindow* pin, int groupIndex); // GRP-03
    void ocrLinesRequested(ss::PinWindow* pin, const QImage& image);  // REC-04：请求识别图像贴图上的文字行

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
    void dragEnterEvent(QDragEnterEvent* event) override;
    bool event(QEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    QSizeF contentSizeLogical() const;
    QRectF desktopBounds() const;
    void relayout();
    void applyNativeGeometry();
    void rebuildTransformed();
    void refreshDisplay();
    void drawShadow(QPainter& painter, const QRectF& content);
    void paintAlphaBackground(QPainter& painter, const QRectF& content);
    void updateClickThroughMask();
    void finishInteraction();
    void layoutToolbar();
    QPointF toImage(QPointF logicalInWindow) const;
    QPoint toPhysical(QPointF logicalInWindow) const;
    bool onContentEdge(QPointF logicalInWindow) const;
    void ensureMagnifierDesktop();
    void replaceSource(const QImage& image);
    void bakeAnnotations(const QImage& bakedDisplay);
    QImage croppedSource() const;
    QRect displayToSource(const QRectF& displayRect) const;
    void textSelectAt(QPointF imagePos, bool extend);
    void showAreaMenu(const QRectF& displayRect, QPoint globalPos);

    static constexpr int kShadowMargin = 24;

    Settings& m_settings;
    OutputActions* m_output;
    QImage m_source;          // 原图
    QImage m_annotated;       // 原图方向上烘焙了标注的图（空 = 没有标注）
    bool m_annotationsHidden = false;
    QImage m_transformedFull; // 裁剪 + 旋转 / 翻转 + 灰度 / 反色后的完整图
    QImage m_transformed;     // 再按显示范围裁出的部分
    QImage m_display;         // 加标注后（原始像素尺寸）
    // 内容
    ContentKind m_kind = ContentKind::Image;
    QString m_textSource;
    QString m_textHtmlSource;
    TextPinOptions m_textOptions;
    std::unique_ptr<TextPinDocument> m_textDoc;
    bool m_textSelecting = false;
    std::pair<int, int> m_textSelection{0, 0};
    QList<OcrLine> m_ocrLines;
    QList<int> m_ocrSelected;
    QRectF m_ocrMarquee;
    QColor m_color;
    QString m_filePath;
    bool m_gray = false;
    bool m_invert = false;
    int m_brightness = 0;
    bool m_locked = false;
    QString m_title;
    QPoint m_ctrlPressPos;
    bool m_ctrlPressed = false;
    bool m_thumbnail = false;
    qreal m_scaleBeforeThumbnail = 1.0;
    QRectF m_viewRect;
    QRect m_cropRect;
    bool m_topmost = true;
    bool m_selected = false;
    int m_groupIndex = 0;
    QColor m_groupColor;
    QStringList m_groupNames;
    std::function<QList<QRectF>()> m_snapTargets;
    // 右键拖选（PIN-30 / PIN-31）与 Shift 平移
    bool m_rubberActive = false;
    QPointF m_rubberStart;
    QRectF m_rubberRect;
    bool m_suppressMenu = false;
    bool m_panning = false;
    QPointF m_panLast;
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
