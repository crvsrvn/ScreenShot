#include "app/PinWindow.h"

#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "core/capture/Magnifier.h"
#include "core/color/ColorFormat.h"
#include "core/config/Settings.h"
#include "core/fx/Blur.h"
#include "core/util/Clock.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QWheelEvent>
#include <QWindow>
#include <cmath>

#if defined(Q_OS_WIN)
#include "platform/windows/WinWindowTools.h"
#endif

namespace ss {

namespace {

constexpr int kMargin = 24;
constexpr int kEdgeGrip = 8;

const QPixmap& shadowTile()
{
    static const QPixmap tile = [] {
        const int core = 2 * kMargin;
        const int size = core + 2 * kMargin;
        QImage mask(size, size, QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        {
            QPainter p(&mask);
            p.fillRect(QRect(kMargin, kMargin + 4, core, core), QColor(0, 0, 0, 150));
        }
        return QPixmap::fromImage(fx::gaussianBlur(mask, 12));
    }();
    return tile;
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

void PinWindow::zoomStep(int steps, QPointF anchorLogical)
{
    const qreal step = m_settings.intValue(QStringLiteral("pin.zoomStep")) / 100.0;
    zoomAt(anchorLogical, std::pow(1.0 + step, steps), true);
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
    m_transformed = t.isIdentity() ? m_source : m_source.transformed(t);
    m_transformed.setDevicePixelRatio(1.0);
    m_editor->document().setBaseImage(m_transformed);
    refreshDisplay();
}

void PinWindow::refreshDisplay()
{
    m_display = m_editor->document().count() > 0 ? m_editor->document().cachedRender() : m_transformed;
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

void PinWindow::quickSave()
{
    if (m_output)
        m_output->quickSave(displayedImage());
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
        m_toolbar->syncFromEditor();
        layoutToolbar();
        m_toolbar->show();
        update();
    } else {
        // 结束：把标注烘焙进显示图（PIN-41 复制“当前显示的图像”含标注）
        m_editor->finishDraft();
        const QImage baked = m_editor->renderForExport();
        m_annotating = false;
        m_editor->end();
        if (!baked.isNull()) {
            m_display = baked;
            m_display.setDevicePixelRatio(1.0);
            m_mipmap = Mipmap(m_display);
            m_smooth = QPixmap();
            m_magnifierDesktop.reset();
        }
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
    return {{QStringLiteral("x"), tl.x()}, {QStringLiteral("y"), tl.y()}, {QStringLiteral("scale"), m_scale}, {QStringLiteral("opacity"), m_opacity},
            {QStringLiteral("rotation"), m_rotation}, {QStringLiteral("flipH"), m_flipH}, {QStringLiteral("flipV"), m_flipV}, {QStringLiteral("passthrough"), m_passthrough}};
}

void PinWindow::applyStateJson(const QJsonObject& o)
{
    m_rotation = o.value(QStringLiteral("rotation")).toInt(0);
    m_flipH = o.value(QStringLiteral("flipH")).toBool();
    m_flipV = o.value(QStringLiteral("flipV")).toBool();
    rebuildTransformed();
    m_scale = std::clamp(o.value(QStringLiteral("scale")).toDouble(1.0), 0.02, 32.0);
    setOpacity(o.value(QStringLiteral("opacity")).toDouble(1.0));
    moveToPhysical(QPoint(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt()));
    if (o.value(QStringLiteral("passthrough")).toBool())
        setPassthrough(true);
}

void PinWindow::closePin()
{
    if (m_annotating)
        toggleToolbar();
    hide();
    emit closed(this);
}

void PinWindow::destroyPin()
{
    if (m_annotating)
        toggleToolbar();
    hide();
    emit destroyed(this);
}

// ---- 菜单 ----

QMenu* PinWindow::buildMenu()
{
    auto* menu = new QMenu(this);
    menu->addAction(tr("Copy image"), this, &PinWindow::copyDisplayed);
    menu->addAction(tr("Copy original image"), this, &PinWindow::copyOriginal);
    menu->addAction(tr("Save as..."), this, &PinWindow::saveDisplayed);
    menu->addAction(tr("Quick save"), this, &PinWindow::quickSave);
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
    menu->addSeparator();
    QAction* through = menu->addAction(tr("Mouse click-through"), this, [this](bool on) { setPassthrough(on); });
    through->setCheckable(true);
    through->setChecked(m_passthrough);
    menu->addSeparator();
    menu->addAction(tr("Close\tEsc"), this, &PinWindow::closePin);
    menu->addAction(tr("Destroy\tShift+Esc"), this, &PinWindow::destroyPin);
    return menu;
}

void PinWindow::contextMenuEvent(QContextMenuEvent* e)
{
    if (m_annotating && m_editor->active() && (m_editor->hasDraft() || m_editor->selectedId() >= 0))
        return; // 标注中的右键由编辑器处理（结束图形 / 取消选中）
    QMenu* menu = buildMenu();
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(e->globalPos());
}

// ---- 绘制 ----

void PinWindow::drawShadow(QPainter& p, const QRectF& content)
{
    const QPixmap& tile = shadowTile();
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
    p.fillRect(QRectF(L + c, T + c, w - 2 * c, h - 2 * c), QColor(0, 0, 0, 150));
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
        if (m_interactive || exact || huge) {
            const QImage& level = m_mipmap.levelForScale(m_scale / m_dpr);
            p.setRenderHint(QPainter::SmoothPixmapTransform, !m_interactive && !exact);
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
    QPen pen(m_passthrough ? QColor(220, 120, 40, 200) : QColor(40, 120, 220, 200));
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(content.adjusted(0, 0, -1, -1));

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
    if (event->button() == Qt::LeftButton) {
        if (m_settings.boolValue(QStringLiteral("pin.dragEdgeZoom")) && onContentEdge(event->position()) && !m_annotating) {
            m_edgeDragging = true;
            m_edgeStart = event->globalPosition();
            m_edgeStartScale = m_scale;
            return;
        }
        m_dragging = true;
        m_dragOffset = event->globalPosition() - m_contentTopLeft;
    } else if (event->button() == Qt::MiddleButton) {
        toggleReset();
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
    if (m_dragging) {
        moveContentTo(event->globalPosition() - m_dragOffset);
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
        m_dragging = false;
        m_edgeDragging = false;
    }
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (m_annotating) {
        m_editor->doubleClickAt(toImage(event->position()));
        return;
    }
    if (event->button() == Qt::LeftButton)
        closePin();
}

void PinWindow::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    if (event->modifiers() & Qt::ControlModifier)
        adjustOpacity(steps * m_settings.intValue(QStringLiteral("pin.opacityStep")));
    else
        zoomStep(steps, event->position());
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
    const int opacityStep = m_settings.intValue(QStringLiteral("pin.opacityStep"));
    switch (key) {
    case Qt::Key_Escape:
        shift ? destroyPin() : closePin();
        return;
    case Qt::Key_W: if (ctrl) { closePin(); return; } break;
    case Qt::Key_D: if (ctrl) { destroyPin(); return; } break;
    case Qt::Key_Space: toggleToolbar(); return;
    case Qt::Key_C:
        if (ctrl && shift) { copyOriginal(); return; }
        if (ctrl) { copyDisplayed(); return; }
        if (m_altHeld || !m_annotating) {
            const QString text = colorTextAt(m_cursorPhysical);
            if (!text.isEmpty())
                QApplication::clipboard()->setText(text);
            return;
        }
        break;
    case Qt::Key_S: if (ctrl && shift) { quickSave(); return; } if (ctrl) { saveDisplayed(); return; } break;
    case Qt::Key_Plus: case Qt::Key_Equal:
        ctrl ? adjustOpacity(opacityStep) : zoomStep(1, QPointF(width() / 2.0, height() / 2.0));
        return;
    case Qt::Key_Minus: case Qt::Key_Underscore:
        ctrl ? adjustOpacity(-opacityStep) : zoomStep(-1, QPointF(width() / 2.0, height() / 2.0));
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
