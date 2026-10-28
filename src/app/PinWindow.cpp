#include "app/PinWindow.h"

#include "app/WindowChrome.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "core/capture/Magnifier.h"
#include "core/color/ColorFormat.h"
#include "core/config/Settings.h"
#include "core/config/Shortcuts.h"
#include "core/fx/Blur.h"
#include "app/Theme.h"
#include "core/pin/ColorPin.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QBitmap>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QNativeGestureEvent>
#include <QMessageBox>
#include <QMimeData>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <QUrl>
#include <QWindow>
#include <QJsonArray>
#include <cmath>

#if defined(Q_OS_WIN)
#include "platform/windows/WinShell.h"
#include "platform/windows/WinWindowTools.h"
#endif

namespace ss {

namespace {

constexpr int kMargin = 24;
constexpr int kEdgeGrip = 8;

// 阴影贴片按颜色缓存（PIN-36：颜色表示状态）
const QPixmap& shadowTile(const QColor& color)
{
    static QHash<QRgb, QPixmap> tiles;
    auto it = tiles.find(color.rgba());
    if (it != tiles.end())
        return *it;
    const int core = 2 * kMargin;
    const int size = core + 2 * kMargin;
    QImage mask(size, size, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter p(&mask);
        p.fillRect(QRect(kMargin, kMargin + 4, core, core), color);
    }
    return *tiles.insert(color.rgba(), QPixmap::fromImage(fx::gaussianBlur(mask, 12)));
}

QScreen* screenForPhysical(QPoint physical)
{
    for (QScreen* s : QGuiApplication::screens()) {
        const QRect g = s->geometry();
        const QRect phys(g.topLeft(), QSize(qRound(g.width() * s->devicePixelRatio()), qRound(g.height() * s->devicePixelRatio())));
        if (phys.contains(physical))
            return s;
    }
    return QGuiApplication::primaryScreen();
}

} // namespace

std::function<bool(QWidget*, const QString&)> PinWindow::confirmHandler = [](QWidget* parent, const QString& text) {
    return theme::message(parent, QMessageBox::Question, QStringLiteral("ScreenShot"), text, QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
};

PinWindow::PinWindow(const QImage& image, Settings& settings, OutputActions* output)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_settings(settings)
    , m_output(output)
    , m_source(image.convertToFormat(QImage::Format_ARGB32_Premultiplied))
    , m_editor(std::make_unique<AnnotationEditor>(settings))
{
    m_source.setDevicePixelRatio(1.0);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true); // PIN-11：拖入本地内容替换
    m_settleTimer.setSingleShot(true);
    m_settleTimer.setInterval(150);
    connect(&m_settleTimer, &QTimer::timeout, this, &PinWindow::finishInteraction);
    connect(m_editor.get(), &AnnotationEditor::changed, this, [this] {
        if (!m_annotating)
            refreshDisplay();
        update();
    });
    m_opacity = m_settings.intValue(QStringLiteral("pin.defaultOpacity")) / 100.0;
    setWindowOpacity(m_opacity);
    if (QScreen* s = QGuiApplication::primaryScreen())
        m_dpr = s->devicePixelRatio();
    rebuildTransformed();
    relayout();
}

PinWindow::~PinWindow()
{
    delete m_toolbar;
}

// ---- 几何 ----

QSizeF PinWindow::contentSizeLogical() const
{
    return QSizeF(m_display.width() * m_scale / m_dpr, m_display.height() * m_scale / m_dpr);
}

QRectF PinWindow::contentRect() const
{
    return QRectF(m_contentTopLeft, contentSizeLogical());
}

QRectF PinWindow::desktopBounds() const
{
    QRectF bounds;
    for (QScreen* s : QGuiApplication::screens())
        bounds = bounds.united(QRectF(s->geometry()));
    return bounds;
}

QPoint PinWindow::physicalTopLeft() const
{
    QScreen* s = QGuiApplication::screenAt(m_contentTopLeft.toPoint());
    if (!s)
        s = QGuiApplication::primaryScreen();
    const QPointF rel = m_contentTopLeft - s->geometry().topLeft();
    return s->geometry().topLeft() + QPoint(qRound(rel.x() * m_dpr), qRound(rel.y() * m_dpr));
}

QRect PinWindow::physicalContentRect() const
{
    return QRect(physicalTopLeft(), QSize(qRound(m_display.width() * m_scale), qRound(m_display.height() * m_scale)));
}

void PinWindow::moveToPhysical(QPoint physical)
{
    QScreen* s = screenForPhysical(physical);
    if (s)
        m_dpr = s->devicePixelRatio();
    const QPoint origin = s ? s->geometry().topLeft() : QPoint();
    m_contentTopLeft = QPointF(origin) + QPointF(physical - origin) / m_dpr;
    relayout();
}

void PinWindow::moveContentTo(QPointF logicalTopLeft)
{
    m_contentTopLeft = logicalTopLeft;
    if (QScreen* s = QGuiApplication::screenAt(logicalTopLeft.toPoint()); s && !qFuzzyCompare(s->devicePixelRatio(), m_dpr)) {
        m_dpr = s->devicePixelRatio();
        m_smooth = QPixmap();
    }
    relayout();
}

void PinWindow::relayout()
{
    const QRectF full = contentRect().adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const QRectF desktop = desktopBounds();
    QRectF window = desktop.isEmpty() ? full : full.intersected(desktop);
    if (window.isEmpty())
        window = QRectF(full.topLeft(), QSizeF(1, 1));
    m_clipped = window != full;
    const QRect target = window.toAlignedRect();
    if (target.size() == size())
        move(target.topLeft());
    else
        setGeometry(target);
    applyNativeGeometry();
    if (m_clipped)
        update();
    layoutToolbar();
    updateClickThroughMask();
}

void PinWindow::updateClickThroughMask()
{
    // PIN-37 透明模式：透明像素处可点穿（用窗口遮罩实现）
    const bool transparentMode = m_settings.stringValue(QStringLiteral("pin.alphaBackground")) == QLatin1String("transparent");
    if (!transparentMode || !m_display.hasAlphaChannel() || m_annotating) {
        clearMask();
        return;
    }
    const QRectF content = contentRect().translated(-pos());
    QImage alpha = m_display.createAlphaMask().convertToFormat(QImage::Format_Mono);
    alpha = alpha.scaled(content.size().toSize().expandedTo(QSize(1, 1)));
    QRegion region(QBitmap::fromImage(alpha));
    region.translate(content.topLeft().toPoint());
    setMask(region);
}

void PinWindow::applyNativeGeometry()
{
#if defined(Q_OS_WIN)
    // 未裁剪时按物理像素精确定位，使 scale=1 的贴图与截图位置重合（PIN-02）
    if (!m_clipped && isVisible() && windowHandle()) {
        const QPoint phys = physicalTopLeft() - QPoint(qRound(kMargin * m_dpr), qRound(kMargin * m_dpr));
        const QSize size(qRound(contentSizeLogical().width() * m_dpr) + 2 * qRound(kMargin * m_dpr), qRound(contentSizeLogical().height() * m_dpr) + 2 * qRound(kMargin * m_dpr));
        win::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), QRect(phys, size));
    }
#endif
}

void PinWindow::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    applyNativeGeometry();
#if defined(Q_OS_WIN)
    if (m_settings.boolValue(QStringLiteral("pin.allDesktops")))
        win::setWindowOnAllDesktops(reinterpret_cast<void*>(winId()), true); // PIN-56
#endif
}

void PinWindow::moveEvent(QMoveEvent* e)
{
    QWidget::moveEvent(e);
    layoutToolbar();
}

void PinWindow::fitToScreen()
{
    QScreen* screen = QGuiApplication::screenAt(contentRect().center().toPoint());
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    const qreal percent = m_settings.intValue(QStringLiteral("pin.maxSizePercent")) / 100.0;
    const QSizeF avail = QSizeF(screen->availableGeometry().size()) * percent;
    const QSizeF cs = contentSizeLogical();
    if (cs.width() <= avail.width() && cs.height() <= avail.height())
        return;
    const qreal factor = std::min(avail.width() / cs.width(), avail.height() / cs.height());
    zoomAt(QPointF(kMargin, kMargin), factor, false);
}

// ---- 缩放 ----

void PinWindow::setScale(qreal scale, bool interactive)
{
    zoomAt(contentRect().topLeft() - pos(), scale / m_scale, interactive);
}

QPointF PinWindow::zoomAnchorLogical(QPointF cursorLogical) const
{
    // PIN-23：缩放中心
    const QString anchor = m_settings.stringValue(QStringLiteral("pin.zoomAnchor"));
    const QRectF c = contentRect().translated(-pos());
    if (anchor == QLatin1String("center"))
        return c.center();
    if (anchor == QLatin1String("topLeft"))
        return c.topLeft();
    if (anchor == QLatin1String("topRight"))
        return c.topRight();
    if (anchor == QLatin1String("bottomLeft"))
        return c.bottomLeft();
    if (anchor == QLatin1String("bottomRight"))
        return c.bottomRight();
    return cursorLogical;
}

void PinWindow::zoomStep(int steps, QPointF anchorLogical)
{
    if (lockBlocks(QStringLiteral("zoom")))
        return;
    const qreal step = m_settings.intValue(QStringLiteral("pin.zoomStep")) / 100.0;
    zoomAt(zoomAnchorLogical(anchorLogical), std::pow(1.0 + step, steps), true);
}

void PinWindow::zoomStepFine(int steps, QPointF anchorLogical)
{
    if (lockBlocks(QStringLiteral("zoom")))
        return;
    const qreal step = m_settings.intValue(QStringLiteral("pin.zoomFineStep")) / 100.0;
    zoomAt(zoomAnchorLogical(anchorLogical), std::pow(1.0 + step, steps), true);
}

void PinWindow::zoomAt(QPointF anchorLogical, qreal factor, bool interactive)
{
    const qreal next = std::clamp(m_scale * factor, 0.02, 32.0);
    if (qFuzzyCompare(next, m_scale))
        return;
    const QRectF before = contentRect();
    const QPointF anchorDesktop = anchorLogical + pos();
    const QPointF ratio((anchorDesktop.x() - before.left()) / before.width(), (anchorDesktop.y() - before.top()) / before.height());
    m_scale = next;
    m_resetActive = false;
    m_interactive = interactive;
    m_smooth = QPixmap();
    const QSizeF after = contentSizeLogical();
    m_contentTopLeft = anchorDesktop - QPointF(ratio.x() * after.width(), ratio.y() * after.height());
    relayout();
    if (interactive)
        m_settleTimer.start();
    update();
}

void PinWindow::toggleReset()
{
    if (m_resetActive) {
        setScale(m_scaleBeforeReset, false);
        setOpacity(m_opacityBeforeReset);
        m_resetActive = false;
        return;
    }
    m_scaleBeforeReset = m_scale;
    m_opacityBeforeReset = m_opacity;
    setScale(1.0, false);
    setOpacity(1.0);
    m_resetActive = true;
}

void PinWindow::finishInteraction()
{
    m_interactive = false;
    update();
}

// ---- 透明度 ----

void PinWindow::setOpacity(qreal opacity)
{
    m_opacity = std::clamp(opacity, 0.1, 1.0);
    setWindowOpacity(m_opacity);
}

void PinWindow::adjustOpacity(int percentDelta)
{
    setOpacity(m_opacity + percentDelta / 100.0);
    m_resetActive = false;
}

// ---- 变换 ----

void PinWindow::rebuildTransformed()
{
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_flipH ? -1 : 1, m_flipV ? -1 : 1);
    const QImage base = (m_annotated.isNull() || m_annotationsHidden) ? croppedSource() : (m_cropRect.isNull() ? m_annotated : m_annotated.copy(m_cropRect));
    m_transformedFull = t.isIdentity() ? base : base.transformed(t);
    if (m_gray) // PIN-27：灰度（保留透明度）
        m_transformedFull = m_transformedFull.convertToFormat(QImage::Format_Grayscale8).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (m_invert) {
        m_transformedFull = m_transformedFull.convertToFormat(QImage::Format_ARGB32);
        m_transformedFull.invertPixels(QImage::InvertRgb);
        m_transformedFull = m_transformedFull.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    if (m_brightness != 0) {
        // PIN-28：亮度按百分比线性缩放
        QImage img = m_transformedFull.convertToFormat(QImage::Format_ARGB32);
        const double k = 1.0 + m_brightness / 100.0;
        for (int y = 0; y < img.height(); ++y) {
            auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                const QRgb c = line[x];
                line[x] = qRgba(std::clamp(static_cast<int>(qRed(c) * k), 0, 255), std::clamp(static_cast<int>(qGreen(c) * k), 0, 255), std::clamp(static_cast<int>(qBlue(c) * k), 0, 255), qAlpha(c));
            }
        }
        m_transformedFull = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    m_transformedFull.setDevicePixelRatio(1.0);
    if (!m_viewRect.isEmpty())
        m_viewRect = m_viewRect.intersected(QRectF(QPointF(0, 0), QSizeF(m_transformedFull.size())));
    refreshDisplay();
    if (!m_annotating)
        m_editor->document().setBaseImage(m_transformed); // 会发 changed → refreshDisplay：放在最后，避免递归
}

void PinWindow::refreshDisplay()
{
    m_transformed = m_viewRect.isEmpty() ? m_transformedFull : m_transformedFull.copy(m_viewRect.toAlignedRect());
    m_transformed.setDevicePixelRatio(1.0);
    m_display = m_editor->document().count() > 0 && !m_annotating ? m_editor->document().cachedRender() : m_transformed;
    m_display.setDevicePixelRatio(1.0);
    m_mipmap = Mipmap(m_display);
    m_smooth = QPixmap();
    m_magnifierDesktop.reset();
    update();
}

void PinWindow::rotateQuarter(int turns)
{
    m_rotation = ((m_rotation + turns * 90) % 360 + 360) % 360;
    // 旋转会改变内容尺寸：围绕中心旋转
    const QPointF center = contentRect().center();
    rebuildTransformed();
    m_contentTopLeft = center - QPointF(contentSizeLogical().width() / 2, contentSizeLogical().height() / 2);
    relayout();
}

void PinWindow::flipHorizontal()
{
    m_flipH = !m_flipH;
    rebuildTransformed();
    relayout();
}

void PinWindow::flipVertical()
{
    m_flipV = !m_flipV;
    rebuildTransformed();
    relayout();
}

// ---- 穿透 ----

void PinWindow::setPassthrough(bool on)
{
    if (m_passthrough == on)
        return;
    m_passthrough = on;
    const bool wasVisible = isVisible();
    setWindowFlag(Qt::WindowTransparentForInput, on);
    if (wasVisible)
        show();
    const int percent = m_settings.intValue(QStringLiteral("pin.passthroughOpacity"));
    if (on) {
        m_opacityBeforePassthrough = m_opacity;
        if (percent < 100)
            setOpacity(std::min(m_opacity, percent / 100.0));
    } else {
        setOpacity(m_opacityBeforePassthrough);
    }
    relayout();
}

// ---- 图像输出 ----

QImage PinWindow::displayedImage() const
{
    return m_annotating ? m_editor->renderForExport() : m_display;
}

void PinWindow::copyDisplayed()
{
    if ((m_kind == ContentKind::Text || m_kind == ContentKind::Image) && m_textSelecting) {
        copyText(); // 文本选择模式下 Ctrl+C 复制选中文字（PIN-06、REC-04）
        return;
    }
    QApplication::clipboard()->setImage(displayedImage());
}

void PinWindow::copyOriginal()
{
    QApplication::clipboard()->setImage(m_source);
}

void PinWindow::saveDisplayed()
{
    if (m_output)
        m_output->saveWithDialog(displayedImage(), nullptr);
}

void PinWindow::pin()
{
    emit pinRequested(displayedImage(), physicalTopLeft() + QPoint(30, 30));
}

void PinWindow::cancel()
{
    if (m_annotating)
        toggleToolbar();
}

// ---- 标注 ----

void PinWindow::toggleToolbar()
{
    if (!m_annotating) {
        m_annotating = true;
        m_editor->begin(m_transformed, physicalContentRect());
        m_editor->document().fromJson({}); // begin 已清空；保持无标注起点（已有标注已烘焙在 m_display 里）
        m_editor->document().setBaseImage(m_display);
        m_editor->setTextHost(this, contentRect().topLeft() - pos(), m_dpr / m_scale);
        if (!m_toolbar)
            m_toolbar = new CaptureToolbar(this);
        if (m_settings.boolValue(QStringLiteral("annotation.rememberTool"))) {
            const AnnotationEditor::Tool last = CaptureToolbar::toolFromName(m_settings.stringValue(QStringLiteral("annotation.lastToolPin")));
            if (last != AnnotationEditor::Tool::None)
                m_editor->setTool(last); // ANN-15
        }
        m_toolbar->syncFromEditor();
        layoutToolbar();
        m_toolbar->show();
        update();
    } else {
        // 结束：把标注烘焙进原图方向的标注图（PIN-41 复制“当前显示的图像”含标注；旋转 / 裁剪后仍保留）
        m_editor->finishDraft();
        const QImage baked = m_editor->document().count() > 0 ? m_editor->renderForExport() : QImage();
        m_annotating = false;
        m_editor->end();
        if (!baked.isNull())
            bakeAnnotations(baked);
        rebuildTransformed();
        relayout();
        if (m_toolbar)
            m_toolbar->hide();
        update();
    }
}

bool PinWindow::toolbarVisible() const
{
    return m_toolbar && m_toolbar->isVisible();
}

void PinWindow::setTool(AnnotationEditor::Tool tool)
{
    if (!m_annotating)
        toggleToolbar();
    m_editor->setTool(tool);
    if (tool != AnnotationEditor::Tool::None && m_settings.boolValue(QStringLiteral("annotation.rememberTool")))
        m_settings.setValue(QStringLiteral("annotation.lastToolPin"), CaptureToolbar::toolName(tool));
    if (m_toolbar)
        m_toolbar->syncFromEditor();
    layoutToolbar();
}

void PinWindow::layoutToolbar()
{
    if (!m_toolbar || !m_annotating)
        return;
    const QSize size = m_toolbar->sizeHint();
    const QRectF content = contentRect();
    QPointF pos(content.right() - size.width() + 1, content.bottom() + 6);
    const QRectF desktop = desktopBounds();
    if (pos.y() + size.height() > desktop.bottom())
        pos.setY(content.top() - 6 - size.height());
    pos.setX(std::max(pos.x(), desktop.left()));
    m_toolbar->move(pos.toPoint());
    if (isVisible())
        m_toolbar->setVisible(true);
}

// ---- 生命周期 ----

QJsonObject PinWindow::stateJson() const
{
    const QPoint tl = physicalTopLeft();
    QJsonObject o{{QStringLiteral("x"), tl.x()}, {QStringLiteral("y"), tl.y()}, {QStringLiteral("scale"), m_scale}, {QStringLiteral("opacity"), m_opacity},
                  {QStringLiteral("rotation"), m_rotation}, {QStringLiteral("flipH"), m_flipH}, {QStringLiteral("flipV"), m_flipV}, {QStringLiteral("passthrough"), m_passthrough},
                  {QStringLiteral("gray"), m_gray}, {QStringLiteral("invert"), m_invert}, {QStringLiteral("topmost"), m_topmost},
                  {QStringLiteral("brightness"), m_brightness}, {QStringLiteral("locked"), m_locked}, {QStringLiteral("title"), m_title}};
    if (!m_cropRect.isNull())
        o[QStringLiteral("crop")] = QJsonArray{m_cropRect.x(), m_cropRect.y(), m_cropRect.width(), m_cropRect.height()};
    if (!m_viewRect.isEmpty())
        o[QStringLiteral("view")] = QJsonArray{m_viewRect.x(), m_viewRect.y(), m_viewRect.width(), m_viewRect.height()};
    if (m_kind == ContentKind::Text) {
        o[QStringLiteral("text")] = m_textSource;
        o[QStringLiteral("html")] = m_textOptions.html;
    } else if (m_kind == ContentKind::Color) {
        o[QStringLiteral("color")] = m_color.name(QColor::HexArgb);
    } else if (m_kind == ContentKind::File) {
        o[QStringLiteral("file")] = m_filePath;
    }
    return o;
}

void PinWindow::applyStateJson(const QJsonObject& o)
{
    m_rotation = o.value(QStringLiteral("rotation")).toInt(0);
    m_flipH = o.value(QStringLiteral("flipH")).toBool();
    m_flipV = o.value(QStringLiteral("flipV")).toBool();
    m_gray = o.value(QStringLiteral("gray")).toBool();
    m_invert = o.value(QStringLiteral("invert")).toBool();
    m_brightness = o.value(QStringLiteral("brightness")).toInt(0);
    m_locked = o.value(QStringLiteral("locked")).toBool();
    m_title = o.value(QStringLiteral("title")).toString();
    const QJsonArray crop = o.value(QStringLiteral("crop")).toArray();
    if (crop.size() == 4)
        m_cropRect = QRect(crop[0].toInt(), crop[1].toInt(), crop[2].toInt(), crop[3].toInt());
    const QJsonArray view = o.value(QStringLiteral("view")).toArray();
    if (view.size() == 4)
        m_viewRect = QRectF(view[0].toDouble(), view[1].toDouble(), view[2].toDouble(), view[3].toDouble());
    rebuildTransformed();
    if (!o.value(QStringLiteral("topmost")).toBool(true))
        setTopmost(false);
    m_scale = std::clamp(o.value(QStringLiteral("scale")).toDouble(1.0), 0.02, 32.0);
    setOpacity(o.value(QStringLiteral("opacity")).toDouble(1.0));
    moveToPhysical(QPoint(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt()));
    if (o.value(QStringLiteral("passthrough")).toBool())
        setPassthrough(true);
}

void PinWindow::closePin(bool userGesture)
{
    if (userGesture && lockBlocks(QStringLiteral("close")))
        return;
    if (m_selected) { // 多选：一起关闭（PIN-60），由管理器统一确认
        emit groupAction(this, QStringLiteral("close"), 0);
        return;
    }
    if (userGesture && m_settings.boolValue(QStringLiteral("pin.confirmClose")) && !confirmHandler(this, tr("Close this pin?")))
        return;
    if (m_annotating)
        toggleToolbar();
    hide();
    emit closed(this);
}

void PinWindow::destroyPin(bool userGesture)
{
    if (userGesture && lockBlocks(QStringLiteral("close")))
        return;
    if (m_selected) {
        emit groupAction(this, QStringLiteral("destroy"), 0);
        return;
    }
    if (userGesture && m_settings.boolValue(QStringLiteral("pin.confirmClose")) && !confirmHandler(this, tr("Destroy this pin? It cannot be restored.")))
        return;
    if (m_annotating)
        toggleToolbar();
    hide();
    emit destroyed(this);
}

// ---- 亮度（PIN-28）----

void PinWindow::setBrightness(int percent)
{
    percent = std::clamp(percent, -100, 100);
    if (m_brightness == percent)
        return;
    m_brightness = percent;
    rebuildTransformed();
    update();
}

void PinWindow::resetProcessing()
{
    m_gray = false;
    m_invert = false;
    m_brightness = 0;
    rebuildTransformed();
    update();
}

// ---- 锁定（PIN-34）----

void PinWindow::setLocked(bool on)
{
    if (m_locked == on)
        return;
    m_locked = on;
    update();
}

bool PinWindow::lockBlocks(const QString& what) const
{
    if (!m_locked)
        return false;
    const QString type = m_settings.stringValue(QStringLiteral("pin.lockType"));
    return type == QLatin1String("all") || type == what;
}

// ---- 标题（PIN-35）----

void PinWindow::setTitle(const QString& title)
{
    m_title = title.trimmed();
    update();
}

void PinWindow::editTitle()
{
    bool ok = false;
    const QString t = theme::getText(this, tr("Pin title"), tr("Title:"), m_title, &ok);
    if (ok)
        setTitle(t);
}

// ---- 拖出（PIN-40）----

QMimeData* PinWindow::makeDragMimeData() const
{
    auto* mime = new QMimeData;
    switch (m_kind) {
    case ContentKind::Text:
        mime->setText(textPlain());
        mime->setHtml(textHtml());
        break;
    case ContentKind::Color:
        mime->setText(colorFormatLines(m_color).first().text);
        mime->setImageData(displayedImage());
        break;
    case ContentKind::File:
        mime->setUrls({QUrl::fromLocalFile(m_filePath)});
        mime->setText(QDir::toNativeSeparators(m_filePath));
        break;
    case ContentKind::Image:
        if (m_textSelecting && !m_ocrSelected.isEmpty())
            mime->setText(selectedText()); // REC-04：拖出选中的文字
        else
            mime->setImageData(displayedImage());
        break;
    }
    return mime;
}

bool PinWindow::event(QEvent* e)
{
    // 触控板手势缩放（PIN-43）
    if (e->type() == QEvent::NativeGesture) {
        auto* g = static_cast<QNativeGestureEvent*>(e);
        if (g->gestureType() == Qt::ZoomNativeGesture && !lockBlocks(QStringLiteral("zoom"))) {
            zoomAt(zoomAnchorLogical(g->position()), 1.0 + g->value(), true);
            return true;
        }
    }
    return QWidget::event(e);
}

// ---- 标注层（ANN-16）----

void PinWindow::bakeAnnotations(const QImage& bakedDisplay)
{
    // 显示图（含当前旋转 / 翻转 / 裁剪 / 显示范围）→ 原图方向，写回对应位置
    QTransform t;
    t.rotate(m_rotation);
    t.scale(m_flipH ? -1 : 1, m_flipV ? -1 : 1);
    QImage layer = t.isIdentity() ? bakedDisplay : bakedDisplay.transformed(t.inverted());
    const QRect target = displayToSource(QRectF(QPointF(0, 0), QSizeF(bakedDisplay.size())));
    if (m_annotated.isNull())
        m_annotated = m_source.copy();
    QPainter p(&m_annotated);
    p.drawImage(target.topLeft(), layer.scaled(target.size()));
}

void PinWindow::setAnnotationsHidden(bool hidden)
{
    if (m_annotationsHidden == hidden)
        return;
    m_annotationsHidden = hidden;
    rebuildTransformed();
    update();
}

// ---- 状态外观 ----

QColor PinWindow::shadowColor() const
{
    if (!m_settings.boolValue(QStringLiteral("pin.shadow")))
        return Qt::transparent;
    if (m_passthrough)
        return QColor(230, 130, 30, 170);
    if (m_locked)
        return QColor(160, 60, 220, 170);
    if (m_selected)
        return QColor(60, 190, 90, 180);
    if (!isActiveWindow())
        return QColor(0, 0, 0, 150);
    QColor accent = theme::accent();
    accent.setAlpha(170);
    return accent;
}

void PinWindow::setSelected(bool on)
{
    if (m_selected == on)
        return;
    m_selected = on;
    update();
}

void PinWindow::changeEvent(QEvent* e)
{
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ActivationChange)
        update(); // 激活状态变化：阴影颜色（PIN-36）
}

// ---- 吸附（PIN-21）----

QPointF PinWindow::snappedTopLeft(QPointF proposed) const
{
    const qreal dist = m_settings.intValue(QStringLiteral("pin.snapDistance"));
    QList<QRectF> targets = m_snapTargets ? m_snapTargets() : QList<QRectF>();
    for (QScreen* s : QGuiApplication::screens())
        targets.append(QRectF(s->availableGeometry()));
    const QSizeF size = contentSizeLogical();
    QPointF best = proposed;
    qreal bestDx = dist + 1, bestDy = dist + 1;
    for (const QRectF& t : targets) {
        // 自己的左 / 右边对齐目标的左 / 右边
        for (const qreal edge : {t.left(), t.right()}) {
            for (const qreal mine : {proposed.x(), proposed.x() + size.width()}) {
                const qreal d = std::abs(edge - mine);
                if (d <= dist && d < bestDx) {
                    bestDx = d;
                    best.setX(proposed.x() + (edge - mine));
                }
            }
        }
        for (const qreal edge : {t.top(), t.bottom()}) {
            for (const qreal mine : {proposed.y(), proposed.y() + size.height()}) {
                const qreal d = std::abs(edge - mine);
                if (d <= dist && d < bestDy) {
                    bestDy = d;
                    best.setY(proposed.y() + (edge - mine));
                }
            }
        }
    }
    return best;
}

// ---- 替换（PIN-11）----

void PinWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasImage() || e->mimeData()->hasUrls() || e->mimeData()->hasText())
        e->acceptProposedAction();
}

void PinWindow::dropEvent(QDropEvent* e)
{
    emit replaceRequested(this, e->mimeData());
    e->acceptProposedAction();
}

// ---- 菜单 ----

QMenu* PinWindow::buildMenu()
{
    auto* menu = new QMenu(this);
    if (m_kind == ContentKind::Image && m_settings.boolValue(QStringLiteral("ocr.pinSelectText"))) {
        QAction* selectMode = menu->addAction(tr("Select text"), this, [this](bool on) { setTextSelectionMode(on); });
        selectMode->setCheckable(true);
        selectMode->setChecked(m_textSelecting);
        menu->addSeparator();
    }
    if (m_kind == ContentKind::Text) {
        menu->addAction(tr("Copy text"), this, &PinWindow::copyText);
        QAction* selectMode = menu->addAction(tr("Select text"), this, [this](bool on) { setTextSelectionMode(on); });
        selectMode->setCheckable(true);
        selectMode->setChecked(m_textSelecting);
        QAction* plain = menu->addAction(tr("Ignore formatting"), this, [this](bool on) { setIgnoreFormatting(on); });
        plain->setCheckable(true);
        plain->setChecked(ignoringFormatting());
        plain->setEnabled(m_textOptions.html || !m_textHtmlSource.isEmpty());
        menu->addSeparator();
    } else if (m_kind == ContentKind::File) {
        menu->addAction(tr("Open file"), this, &PinWindow::openFile);
        menu->addAction(tr("Copy file"), this, &PinWindow::copyFile);
        menu->addAction(tr("Copy path"), this, &PinWindow::copyFilePath);
        menu->addAction(tr("Show in folder"), this, &PinWindow::revealFile);
        menu->addSeparator();
    } else if (m_kind == ContentKind::Color) {
        QMenu* copyAs = menu->addMenu(tr("Copy color as"));
        for (const auto& line : colorFormatLines(m_color)) {
            const QString name = line.name;
            copyAs->addAction(line.text, this, [this, name] { copyColorAs(name); });
        }
        menu->addAction(tr("Edit color..."), this, &PinWindow::editColorValue);
        menu->addSeparator();
    }
    menu->addAction(tr("Recognize text (OCR)"), this, [this] { emit ocrRequested(displayedImage()); });
    menu->addAction(tr("Decode QR code / barcode"), this, [this] { emit codesRequested(displayedImage()); });
    menu->addSeparator();
    menu->addAction(tr("Copy image"), this, &PinWindow::copyDisplayed);
    menu->addAction(tr("Copy original image"), this, &PinWindow::copyOriginal);
    menu->addAction(tr("Save as..."), this, &PinWindow::saveDisplayed);
    menu->addSeparator();
    menu->addAction(m_annotating ? tr("Hide toolbar\tSpace") : tr("Annotate\tSpace"), this, &PinWindow::toggleToolbar);
    menu->addSeparator();
    menu->addAction(tr("Zoom in\t+"), this, [this] { zoomStep(1, QPointF(width() / 2.0, height() / 2.0)); });
    menu->addAction(tr("Zoom out\t-"), this, [this] { zoomStep(-1, QPointF(width() / 2.0, height() / 2.0)); });
    menu->addAction(tr("Reset size and opacity\tMiddle click"), this, &PinWindow::toggleReset);
    menu->addAction(tr("More opaque\tCtrl++"), this, [this] { adjustOpacity(m_settings.intValue(QStringLiteral("pin.opacityStep"))); });
    menu->addAction(tr("More transparent\tCtrl+-"), this, [this] { adjustOpacity(-m_settings.intValue(QStringLiteral("pin.opacityStep"))); });
    menu->addSeparator();
    menu->addAction(tr("Rotate clockwise\t1"), this, [this] { rotateQuarter(1); });
    menu->addAction(tr("Rotate counter-clockwise\t2"), this, [this] { rotateQuarter(-1); });
    menu->addAction(tr("Flip horizontally\t3"), this, &PinWindow::flipHorizontal);
    menu->addAction(tr("Flip vertically\t4"), this, &PinWindow::flipVertical);
    QAction* gray = menu->addAction(tr("Grayscale\t5"), this, [this](bool on) { setGrayscale(on); });
    gray->setCheckable(true);
    gray->setChecked(m_gray);
    QAction* invert = menu->addAction(tr("Invert colors\t6"), this, [this](bool on) { setInverted(on); });
    invert->setCheckable(true);
    invert->setChecked(m_invert);
    menu->addSeparator();
    if (!m_annotated.isNull()) {
        QAction* hideAnn = menu->addAction(tr("Hide annotations"), this, [this](bool on) { setAnnotationsHidden(on); });
        hideAnn->setCheckable(true);
        hideAnn->setChecked(m_annotationsHidden);
    }
    QAction* thumb = menu->addAction(tr("Thumbnail\tShift+double-click"), this, &PinWindow::toggleThumbnail);
    thumb->setCheckable(true);
    thumb->setChecked(m_thumbnail);
    if (!m_viewRect.isEmpty())
        menu->addAction(tr("Show whole image"), this, &PinWindow::clearViewRect);
    if (!m_cropRect.isNull()) {
        menu->addAction(tr("Confirm crop"), this, &PinWindow::confirmCrop);
        menu->addAction(tr("Restore crop"), this, &PinWindow::restoreCrop);
    }
    menu->addSeparator();
    QAction* through = menu->addAction(tr("Mouse click-through"), this, [this](bool on) { setPassthrough(on); });
    through->setCheckable(true);
    through->setChecked(m_passthrough);
    QAction* top = menu->addAction(tr("Always on top\tT"), this, [this](bool on) { setTopmost(on); });
    top->setCheckable(true);
    top->setChecked(m_topmost);
    QAction* lock = menu->addAction(tr("Lock\tL"), this, [this](bool on) { setLocked(on); });
    lock->setCheckable(true);
    lock->setChecked(m_locked);
    menu->addAction(tr("Title...\tF2"), this, &PinWindow::editTitle);
    menu->addAction(tr("Brighter\t8"), this, [this] { setBrightness(m_brightness + 10); });
    menu->addAction(tr("Darker\t7"), this, [this] { setBrightness(m_brightness - 10); });
    menu->addAction(tr("Reset image processing\t0"), this, &PinWindow::resetProcessing);
    menu->addSeparator();
    menu->addAction(tr("Close\tEsc"), this, [this] { closePin(true); });
    menu->addAction(tr("Destroy\tShift+Esc"), this, [this] { destroyPin(true); });
    return menu;
}

void PinWindow::showAreaMenu(const QRectF& displayRect, QPoint globalPos)
{
    // 右键拖选后：裁剪（PIN-31）或只显示该范围（PIN-30）
    QMenu menu(this);
    QAction* crop = menu.addAction(tr("Crop to this area"));
    QAction* view = menu.addAction(tr("Show only this area"));
    menu.addAction(tr("Cancel"));
    QAction* chosen = menu.exec(globalPos);
    if (chosen == crop)
        cropTo(displayRect);
    else if (chosen == view)
        setViewRect(displayRect);
}

void PinWindow::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_suppressMenu) {
        m_suppressMenu = false;
        return;
    }
    if (m_annotating && m_editor->active() && (m_editor->hasDraft() || m_editor->selectedId() >= 0))
        return; // 标注中的右键由编辑器处理（结束图形 / 取消选中）
    QMenu* menu = buildMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(e->globalPos());
}

// ---- 绘制 ----

void PinWindow::paintAlphaBackground(QPainter& p, const QRectF& content)
{
    // PIN-37：带透明通道的图像的背景
    if (!m_display.hasAlphaChannel())
        return;
    const QString mode = m_settings.stringValue(QStringLiteral("pin.alphaBackground"));
    if (mode == QLatin1String("transparent"))
        return;
    if (mode == QLatin1String("pseudo")) {
        p.fillRect(content, QColor(255, 255, 255, 40)); // 伪透明：淡淡的白
        return;
    }
    const QColor a = mode == QLatin1String("checkerDark") ? QColor(60, 60, 60) : QColor(255, 255, 255);
    const QColor b = mode == QLatin1String("checkerDark") ? QColor(90, 90, 90) : QColor(204, 204, 204);
    p.save();
    p.setClipRect(content);
    const int cell = 8;
    for (int y = 0; y * cell < content.height(); ++y)
        for (int x = 0; x * cell < content.width(); ++x)
            p.fillRect(QRectF(content.left() + x * cell, content.top() + y * cell, cell, cell), ((x + y) % 2) ? a : b);
    p.restore();
}

void PinWindow::drawShadow(QPainter& p, const QRectF& content)
{
    const QColor color = shadowColor();
    if (color.alpha() == 0)
        return;
    const QPixmap& tile = shadowTile(color);
    const int t = tile.width();
    const int c = kMargin + 8;
    const QRectF outer = content.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    const qreal L = outer.left(), T = outer.top(), R = outer.right(), B = outer.bottom();
    const qreal w = outer.width(), h = outer.height();
    if (w < 2 * c || h < 2 * c) {
        p.drawPixmap(outer, tile, tile.rect());
        return;
    }
    p.drawPixmap(QRectF(L, T, c, c), tile, QRectF(0, 0, c, c));
    p.drawPixmap(QRectF(R - c, T, c, c), tile, QRectF(t - c, 0, c, c));
    p.drawPixmap(QRectF(L, B - c, c, c), tile, QRectF(0, t - c, c, c));
    p.drawPixmap(QRectF(R - c, B - c, c, c), tile, QRectF(t - c, t - c, c, c));
    p.drawPixmap(QRectF(L + c, T, w - 2 * c, c), tile, QRectF(c, 0, t - 2 * c, c));
    p.drawPixmap(QRectF(L + c, B - c, w - 2 * c, c), tile, QRectF(c, t - c, t - 2 * c, c));
    p.drawPixmap(QRectF(L, T + c, c, h - 2 * c), tile, QRectF(0, c, c, t - 2 * c));
    p.drawPixmap(QRectF(R - c, T + c, c, h - 2 * c), tile, QRectF(t - c, c, c, t - 2 * c));
    p.fillRect(QRectF(L + c, T + c, w - 2 * c, h - 2 * c), color);
}

void PinWindow::paintEvent(QPaintEvent*)
{
    const qint64 t0 = monotonicNs();
    QPainter p(this);
    const QRectF content = contentRect().translated(-pos());
    drawShadow(p, content);

    if (m_annotating) {
        // 标注中：由编辑器画（缓存 + 草稿 + 锚点），图像像素 → 逻辑像素的比例为 m_scale / dpr
        p.save();
        p.setClipRect(content);
        m_editor->paint(p, content.topLeft(), m_dpr / m_scale);
        p.restore();
    } else {
        const QRectF desktop = desktopBounds();
        const bool huge = content.width() * content.height() > desktop.width() * desktop.height() * 1.5;
        const bool exact = qFuzzyCompare(m_scale, 1.0);
        const bool smoothZoom = m_settings.boolValue(QStringLiteral("pin.smoothZoom"));
        paintAlphaBackground(p, content);
        if (m_interactive || exact || huge || !smoothZoom) {
            const QImage& level = smoothZoom ? m_mipmap.levelForScale(m_scale / m_dpr) : m_display;
            p.setRenderHint(QPainter::SmoothPixmapTransform, !m_interactive && !exact && smoothZoom);
            if (exact) {
                // 1:1 物理像素：按设备像素画，避免任何重采样（NFR-08）
                p.save();
                p.scale(1.0 / m_dpr, 1.0 / m_dpr);
                p.drawImage(QPointF(content.topLeft() * m_dpr), m_display);
                p.restore();
            } else {
                p.drawImage(content, level);
            }
        } else {
            if (m_smooth.isNull()) {
                const QImage& level = m_mipmap.levelForScale(m_scale / m_dpr);
                const QSize target(qRound(content.width() * m_dpr), qRound(content.height() * m_dpr));
                QImage scaled = level.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                scaled.setDevicePixelRatio(m_dpr);
                m_smooth = QPixmap::fromImage(scaled);
            }
            p.drawPixmap(content.topLeft(), m_smooth);
        }
    }
    QPen pen(m_passthrough ? QColor(220, 120, 40, 200) : m_locked ? QColor(160, 60, 220, 220) : m_selected ? QColor(60, 190, 90, 230) : QColor(40, 120, 220, 200));
    pen.setCosmetic(true);
    if (m_selected || m_locked)
        pen.setWidth(2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(content.adjusted(0, 0, -1, -1));
    if (!m_title.isEmpty()) {
        // 标题（PIN-35）：画在内容上方或下方的阴影区
        const bool top = m_settings.stringValue(QStringLiteral("pin.titlePosition")) != QLatin1String("bottom");
        QFont f = p.font();
        f.setPointSize(9);
        p.setFont(f);
        const QRectF bar = top ? QRectF(content.left(), content.top() - 20, content.width(), 18) : QRectF(content.left(), content.bottom() + 2, content.width(), 18);
        p.fillRect(bar, QColor(0, 0, 0, 170));
        p.setPen(Qt::white);
        p.drawText(bar.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(m_title, Qt::ElideRight, static_cast<int>(bar.width() - 8)));
    }
    // 文本选择高亮（PIN-06）
    if (m_textSelecting && !m_annotating) {
        const qreal k = m_scale / m_dpr;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(40, 120, 220, 90));
        for (const QRectF& r : textSelectionRectsImage())
            p.drawRect(QRectF(content.topLeft() + r.topLeft() * k, r.size() * k));
        for (int i : m_ocrSelected)
            p.drawRect(QRectF(content.topLeft() + QPointF(m_ocrLines[i].bounds.topLeft()) * k, QSizeF(m_ocrLines[i].bounds.size()) * k));
    }
    // 右键拖选框（PIN-30 / PIN-31）
    if (m_rubberActive && !m_rubberRect.isEmpty()) {
        QPen dash(Qt::white, 1, Qt::DashLine);
        dash.setCosmetic(true);
        p.setPen(dash);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawRect(m_rubberRect);
    }

    if (m_altHeld) {
        const_cast<PinWindow*>(this)->ensureMagnifierDesktop();
        if (m_magnifierDesktop) {
            MagnifierOptions o;
            o.zoom = m_settings.intValue(QStringLiteral("capture.magnifierZoom"));
            o.colorFormat = colorFormatFromName(m_settings.stringValue(QStringLiteral("capture.colorFormat")));
            const QPointF cursorLogical = QPointF(m_cursorPhysical - physicalTopLeft()) / m_dpr + content.topLeft();
            const QPointF at = magnifierPlacement(cursorLogical, magnifierSize(o), QRectF(rect()));
            paintMagnifier(p, at, *m_magnifierDesktop, m_cursorPhysical, QRect(), o, colorTextAt(m_cursorPhysical));
        }
    }
    p.end();
    ++m_paintCount;
    m_paintDurationsMs.append(nsToMs(monotonicNs() - t0));
}

QList<double> PinWindow::takePaintDurationsMs()
{
    QList<double> out;
    out.swap(m_paintDurationsMs);
    return out;
}

void PinWindow::ensureMagnifierDesktop()
{
    if (m_magnifierDesktop)
        return;
    // 用当前显示的内容按物理尺寸构造一个单屏虚拟桌面（PIN-39）
    const QRect phys = physicalContentRect();
    QImage shown = m_display.scaled(phys.size(), Qt::IgnoreAspectRatio, qFuzzyCompare(m_scale, 1.0) ? Qt::FastTransformation : Qt::SmoothTransformation);
    ScreenInfo s{QStringLiteral("pin"), phys, m_dpr, false, false};
    shown.setDevicePixelRatio(m_dpr);
    m_magnifierDesktop = std::make_unique<VirtualDesktop>(QList<ScreenSnapshot>{ScreenSnapshot{s, shown, 0}});
}

QString PinWindow::colorTextAt(QPoint physical) const
{
    const_cast<PinWindow*>(this)->ensureMagnifierDesktop();
    if (!m_magnifierDesktop)
        return {};
    const auto px = m_magnifierDesktop->pixelAt(physical);
    if (!px)
        return {};
    return formatColor(QColor(*px), colorFormatFromName(m_settings.stringValue(QStringLiteral("capture.colorFormat"))), m_settings.boolValue(QStringLiteral("capture.hexUppercase")));
}

// ---- 坐标 ----

QPointF PinWindow::toImage(QPointF logical) const
{
    const QPointF rel = logical - (contentRect().topLeft() - pos());
    return rel * (m_dpr / m_scale);
}

QPoint PinWindow::toPhysical(QPointF logical) const
{
    const QPointF rel = logical - (contentRect().topLeft() - pos());
    return physicalTopLeft() + QPoint(qRound(rel.x() * m_dpr), qRound(rel.y() * m_dpr));
}

bool PinWindow::onContentEdge(QPointF logical) const
{
    const QRectF c = contentRect().translated(-pos());
    const QRectF outer = c.adjusted(-kEdgeGrip, -kEdgeGrip, kEdgeGrip, kEdgeGrip);
    const QRectF inner = c.adjusted(kEdgeGrip, kEdgeGrip, -kEdgeGrip, -kEdgeGrip);
    return outer.contains(logical) && !inner.contains(logical);
}

// ---- 输入 ----

void PinWindow::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    m_cursorPhysical = toPhysical(event->position());
    if (m_annotating && m_editor->tool() != AnnotationEditor::Tool::None && event->button() == Qt::LeftButton) {
        m_editor->pressAt(toImage(event->position()), event->button(), event->modifiers());
        return;
    }
    if (m_annotating && event->button() == Qt::LeftButton && m_editor->selectedId() >= 0) {
        m_editor->pressAt(toImage(event->position()), event->button(), event->modifiers());
        if (m_editor->dragMode() != AnnotationEditor::DragMode::None)
            return;
    }
    if (m_annotating && event->button() == Qt::RightButton && (m_editor->hasDraft() || m_editor->selectedId() >= 0)) {
        m_editor->pressAt(toImage(event->position()), event->button(), event->modifiers());
        return;
    }
    if (m_annotating && event->button() == Qt::RightButton && !m_ocrLines.isEmpty()) {
        // REC-04：右键自动框出光标下的文字行
        const QPointF at = toImage(event->position());
        for (const OcrLine& l : m_ocrLines) {
            if (QRectF(l.bounds).contains(at) && m_editor->createFromRect(QRectF(l.bounds).adjusted(-2, -2, 2, 2))) {
                m_suppressMenu = true;
                return;
            }
        }
    }
    if (event->button() == Qt::LeftButton) {
        if ((event->modifiers() & Qt::ControlModifier) && !m_annotating) {
            // Ctrl+单击 = 多选（PIN-60）；Ctrl+拖动 = 拖出内容（PIN-40），松开时再判断
            m_ctrlPressed = true;
            m_ctrlPressPos = event->globalPosition().toPoint();
            return;
        }
        if (lockBlocks(QStringLiteral("move")))
            return;
        if (m_textSelecting && !m_annotating && !(event->modifiers() & Qt::AltModifier)) {
            if (m_kind == ContentKind::Image) {
                m_ocrMarquee = QRectF(toImage(event->position()), QSizeF(0, 0));
                selectOcrLinesIn(QRectF(toImage(event->position()), QSizeF(1, 1)));
            } else {
                textSelectAt(toImage(event->position()), event->modifiers() & Qt::ShiftModifier);
            }
            m_dragging = false;
            m_panning = true; // 复用：拖动扩展选择；Alt+拖动仍移动贴图（REC-04）
            return;
        }
        if ((event->modifiers() & Qt::ShiftModifier) && !m_viewRect.isEmpty() && !m_annotating) {
            m_panning = true; // PIN-30：Shift+拖动平移显示范围
            m_panLast = event->globalPosition();
            return;
        }
        if (m_settings.boolValue(QStringLiteral("pin.dragEdgeZoom")) && onContentEdge(event->position()) && !m_annotating) {
            m_edgeDragging = true;
            m_edgeStart = event->globalPosition();
            m_edgeStartScale = m_scale;
            return;
        }
        m_dragging = true;
        m_dragOffset = event->globalPosition() - m_contentTopLeft;
    } else if (event->button() == Qt::MiddleButton) {
        runMouseAction(m_settings.stringValue(QStringLiteral("pin.mouseMiddle")), event->globalPosition().toPoint());
    } else if (event->button() == Qt::RightButton && !m_annotating) {
        if (event->modifiers() & Qt::ShiftModifier) {
            m_suppressMenu = true;
            runMouseAction(m_settings.stringValue(QStringLiteral("pin.mouseShiftRight")), event->globalPosition().toPoint());
            return;
        }
        m_rubberActive = true; // 可能是拖选（PIN-30 / PIN-31）
        m_rubberStart = event->position();
        m_rubberRect = QRectF();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent* event)
{
    m_cursorPhysical = toPhysical(event->position());
    if (m_annotating && m_editor->dragMode() != AnnotationEditor::DragMode::None) {
        m_editor->moveTo(toImage(event->position()));
        return;
    }
    if (m_edgeDragging) {
        // 拖边框缩放：按水平位移比例缩放（PIN-22）
        const qreal startWidth = m_display.width() * m_edgeStartScale / m_dpr;
        const qreal delta = event->globalPosition().x() - m_edgeStart.x();
        const qreal factor = std::max(0.05, (startWidth + delta) / startWidth);
        const qreal next = m_edgeStartScale * factor;
        zoomAt(contentRect().topLeft() - pos(), next / m_scale, true);
        return;
    }
    if (m_ctrlPressed) {
        if ((event->globalPosition().toPoint() - m_ctrlPressPos).manhattanLength() > 8) {
            m_ctrlPressed = false;
            auto* drag = new QDrag(this);
            drag->setMimeData(makeDragMimeData());
            drag->setPixmap(QPixmap::fromImage(displayedImage().scaled(96, 96, Qt::KeepAspectRatio)));
            drag->exec(Qt::CopyAction);
        }
        return;
    }
    if (m_panning) {
        if (m_textSelecting && m_kind == ContentKind::Image) {
            m_ocrMarquee.setBottomRight(toImage(event->position()));
            selectOcrLinesIn(m_ocrMarquee.normalized());
        } else if (m_textSelecting) {
            textSelectAt(toImage(event->position()), true);
        } else {
            const QPointF d = (event->globalPosition() - m_panLast) * (m_dpr / m_scale);
            m_panLast = event->globalPosition();
            panView(-d);
        }
        return;
    }
    if (m_rubberActive) {
        m_rubberRect = QRectF(m_rubberStart, event->position()).normalized();
        update();
        return;
    }
    if (m_dragging) {
        const QPointF before = m_contentTopLeft;
        QPointF next = event->globalPosition() - m_dragOffset;
        if (event->modifiers() & Qt::ShiftModifier)
            next = snappedTopLeft(next); // PIN-21
        moveContentTo(next);
        if (m_selected)
            emit groupMoved(this, m_contentTopLeft - before);
        return;
    }
    if (m_annotating)
        m_editor->moveTo(toImage(event->position()));
    if (m_altHeld)
        update();
    if (!m_annotating)
        setCursor(m_settings.boolValue(QStringLiteral("pin.dragEdgeZoom")) && onContentEdge(event->position()) ? Qt::SizeFDiagCursor : Qt::ArrowCursor);
}

void PinWindow::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_annotating && m_editor->dragMode() != AnnotationEditor::DragMode::None) {
        m_editor->releaseAt(toImage(event->position()), event->button());
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (m_ctrlPressed) {
            m_ctrlPressed = false;
            emit selectToggleRequested(this); // 没拖动：切换选中
        }
        m_dragging = false;
        m_edgeDragging = false;
        m_panning = false;
    } else if (event->button() == Qt::RightButton && m_rubberActive) {
        m_rubberActive = false;
        const QRectF band = m_rubberRect;
        m_rubberRect = QRectF();
        update();
        if (band.width() > 4 && band.height() > 4) {
            m_suppressMenu = true; // 本次右键是拖选，不弹普通菜单
            const QRectF content = contentRect().translated(-pos());
            const qreal k = m_dpr / m_scale;
            showAreaMenu(QRectF((band.topLeft() - content.topLeft()) * k, band.size() * k), event->globalPosition().toPoint());
        }
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_annotating) {
        m_editor->doubleClickAt(toImage(event->position()));
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    runMouseAction(m_settings.stringValue(shift ? QStringLiteral("pin.mouseShiftDoubleClick") : QStringLiteral("pin.mouseDoubleClick")), event->globalPosition().toPoint());
}

void PinWindow::runMouseAction(const QString& action, QPoint globalPos)
{
    if (action == QLatin1String("close"))
        closePin(true);
    else if (action == QLatin1String("destroy"))
        destroyPin(true);
    else if (action == QLatin1String("copy"))
        copyDisplayed();
    else if (action == QLatin1String("reset"))
        m_selected ? emit groupAction(this, QStringLiteral("reset"), 0) : toggleReset();
    else if (action == QLatin1String("thumbnail"))
        toggleThumbnail();
    else if (action == QLatin1String("passthrough"))
        setPassthrough(!m_passthrough);
    else if (action == QLatin1String("topmost"))
        setTopmost(!m_topmost);
    else if (action == QLatin1String("menu")) {
        QMenu* menu = buildMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(globalPos);
    }
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    if (event->modifiers() & Qt::ControlModifier) {
        if (m_selected)
            emit groupAction(this, QStringLiteral("opacity"), steps * m_settings.intValue(QStringLiteral("pin.opacityStep")));
        else
            adjustOpacity(steps * m_settings.intValue(QStringLiteral("pin.opacityStep")));
    } else if (event->modifiers() & Qt::ShiftModifier) {
        zoomStepFine(steps, event->position()); // PIN-23：精细步长
    } else {
        zoomStep(steps, event->position());
    }
}

void PinWindow::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const bool ctrl = event->modifiers() & Qt::ControlModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    if (key == Qt::Key_Alt) {
        m_altHeld = true;
        update();
        return;
    }
    if (key == Qt::Key_Shift)
        m_editor->setShiftHeld(true);
    if (m_annotating) {
        // 标注中的按键先交给编辑器语义
        switch (key) {
        case Qt::Key_Z: if (ctrl) { shift ? m_editor->redo() : m_editor->undo(); return; } break;
        case Qt::Key_Y: if (ctrl) { m_editor->redo(); return; } break;
        case Qt::Key_Delete: case Qt::Key_Backspace: m_editor->deleteSelected(); return;
        case Qt::Key_Tab: m_editor->toggleArrow(); return;
        case Qt::Key_BracketLeft: case Qt::Key_1: if (!ctrl) { m_editor->adjustWidth(-1); return; } break;
        case Qt::Key_BracketRight: case Qt::Key_2: if (!ctrl) { m_editor->adjustWidth(1); return; } break;
        case Qt::Key_T: if (!ctrl) { setTool(AnnotationEditor::Tool::Text); return; } break;
        case Qt::Key_B: if (!ctrl) { setTool(AnnotationEditor::Tool::Pencil); return; } break;
        case Qt::Key_Escape:
            if (m_editor->hasDraft()) { m_editor->cancelDraft(); return; }
            if (m_editor->selectedId() >= 0) { m_editor->select(-1); return; }
            toggleToolbar();
            return;
        default: break;
        }
        if (ctrl && !shift && key >= Qt::Key_1 && key <= Qt::Key_8) {
            static const AnnotationEditor::Tool tools[] = {AnnotationEditor::Tool::Rect, AnnotationEditor::Tool::Ellipse, AnnotationEditor::Tool::Line, AnnotationEditor::Tool::Arrow,
                                                           AnnotationEditor::Tool::Highlighter, AnnotationEditor::Tool::Mosaic, AnnotationEditor::Tool::Blur, AnnotationEditor::Tool::Eraser};
            setTool(tools[key - Qt::Key_1]);
            return;
        }
    }
    // ACT-09：可自定义的快捷键先按映射表分发（所有贴图 / 图像贴图 / 文本贴图）
    if (!m_annotating && key != Qt::Key_Shift && key != Qt::Key_Alt && key != Qt::Key_Control) {
        const ShortcutMap map(m_settings.stringValue(QStringLiteral("shortcuts.map")));
        QString action = map.actionFor(QStringLiteral("pin"), key, event->modifiers());
        if (action.isEmpty() && m_kind == ContentKind::Image)
            action = map.actionFor(QStringLiteral("pinImage"), key, event->modifiers());
        if (action.isEmpty() && m_kind == ContentKind::Text)
            action = map.actionFor(QStringLiteral("pinText"), key, event->modifiers());
        if (action == QLatin1String("close")) { closePin(true); return; }
        if (action == QLatin1String("destroy")) { destroyPin(true); return; }
        if (action == QLatin1String("copy")) { copyDisplayed(); return; }
        if (action == QLatin1String("copyOriginal")) { copyOriginal(); return; }
        if (action == QLatin1String("save")) { saveDisplayed(); return; }
        if (action == QLatin1String("toolbar")) { toggleToolbar(); return; }
        if (action == QLatin1String("lock")) { setLocked(!m_locked); return; }
        if (action == QLatin1String("topmost")) { setTopmost(!m_topmost); return; }
        if (action == QLatin1String("title")) { editTitle(); return; }
        if (action == QLatin1String("grayscale")) { setGrayscale(!m_gray); return; }
        if (action == QLatin1String("invert")) { setInverted(!m_invert); return; }
        if (action == QLatin1String("darker")) { setBrightness(m_brightness - 10); return; }
        if (action == QLatin1String("brighter")) { setBrightness(m_brightness + 10); return; }
        if (action == QLatin1String("resetProcessing")) { resetProcessing(); return; }
        if (action == QLatin1String("copyText")) { copyText(); return; }
        if (action == QLatin1String("selectText")) { setTextSelectionMode(!m_textSelecting); return; }
    }
    const int opacityStep = m_settings.intValue(QStringLiteral("pin.opacityStep"));
    switch (key) {
    case Qt::Key_Escape:
        if (m_textSelecting) { setTextSelectionMode(false); return; }
        if (lockBlocks(QStringLiteral("close"))) return;
        shift ? destroyPin(true) : closePin(true);
        return;
    // 关闭 / 销毁 / 复制 / 保存 / 标注 / 锁定 / 置顶 / 标题 / 图像处理等键由上面的映射表（ACT-09）分发
    case Qt::Key_V: if (ctrl) { emit replaceRequested(this, QApplication::clipboard()->mimeData()); return; } break;
    case Qt::Key_A: if (ctrl && !m_annotating) { emit selectAllRequested(); return; } break;
    case Qt::Key_Space: if (m_annotating) { toggleToolbar(); return; } break;
    case Qt::Key_C:
        if (m_annotating && ctrl && shift) { copyOriginal(); return; }
        if (m_annotating && ctrl) { copyDisplayed(); return; }
        if (m_altHeld || !m_annotating) {
            const QString text = colorTextAt(m_cursorPhysical);
            if (!text.isEmpty())
                QApplication::clipboard()->setText(text);
            return;
        }
        break;
    case Qt::Key_S: if (m_annotating && ctrl) { saveDisplayed(); return; } break;
    case Qt::Key_Plus: case Qt::Key_Equal:
        if (ctrl && m_selected) emit groupAction(this, QStringLiteral("opacity"), opacityStep);
        else ctrl ? adjustOpacity(opacityStep) : zoomStep(1, QPointF(width() / 2.0, height() / 2.0));
        return;
    case Qt::Key_Minus: case Qt::Key_Underscore:
        if (ctrl && m_selected) emit groupAction(this, QStringLiteral("opacity"), -opacityStep);
        else ctrl ? adjustOpacity(-opacityStep) : zoomStep(-1, QPointF(width() / 2.0, height() / 2.0));
        return;
    case Qt::Key_1: if (!m_annotating) { rotateQuarter(1); return; } break;
    case Qt::Key_2: if (!m_annotating) { rotateQuarter(-1); return; } break;
    case Qt::Key_3: if (!m_annotating) { flipHorizontal(); return; } break;
    case Qt::Key_4: if (!m_annotating) { flipVertical(); return; } break;
    case Qt::Key_Left: case Qt::Key_Right: case Qt::Key_Up: case Qt::Key_Down: {
        const QPoint d = key == Qt::Key_Left ? QPoint(-1, 0) : key == Qt::Key_Right ? QPoint(1, 0) : key == Qt::Key_Up ? QPoint(0, -1) : QPoint(0, 1);
        moveToPhysical(physicalTopLeft() + d); // 方向键移动 1 物理像素（PIN-20）
        return;
    }
    case Qt::Key_Menu: {
        QMenu* menu = buildMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(mapToGlobal(contentRect().translated(-pos()).center().toPoint()));
        return;
    }
    default: break;
    }
    QWidget::keyPressEvent(event);
}

void PinWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Alt) {
        m_altHeld = false;
        update();
        return;
    }
    if (event->key() == Qt::Key_Shift)
        m_editor->setShiftHeld(false);
    QWidget::keyReleaseEvent(event);
}

} // namespace ss
