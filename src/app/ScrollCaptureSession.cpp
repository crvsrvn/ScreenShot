#include "app/ScrollCaptureSession.h"

#include "app/Theme.h"

#include "core/config/Settings.h"
#include "core/geometry/VirtualDesktop.h"

#include <QCheckBox>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#if defined(Q_OS_WIN)
#include "platform/windows/WinWindowTools.h"
#endif

namespace ss {

// ---- 区域边框窗口：不接收输入、不参与捕获 ----
class ScrollFrameWindow : public QWidget {
public:
    explicit ScrollFrameWindow(const QRect& physical, qreal scale, QPoint logicalOrigin, QPoint physicalOrigin)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
        , m_scale(scale)
        , m_logicalOrigin(logicalOrigin)
        , m_physicalOrigin(physicalOrigin)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setRegion(physical);
    }
    // 区域移动后跟随（CAP-71）
    void setRegion(const QRect& physical)
    {
        const int border = 3;
        m_physical = physical.adjusted(-border, -border, border, border);
        const QPointF logical = QPointF(m_logicalOrigin) + QPointF(m_physical.topLeft() - m_physicalOrigin) / m_scale;
        setGeometry(QRect(logical.toPoint(), QSize(static_cast<int>(std::ceil(m_physical.width() / m_scale)), static_cast<int>(std::ceil(m_physical.height() / m_scale)))));
#if defined(Q_OS_WIN)
        if (isVisible())
            win::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_physical);
#endif
    }
    void setOk(bool ok)
    {
        m_ok = ok;
        update();
    }
    void showExcluded()
    {
        show();
#if defined(Q_OS_WIN)
        win::excludeFromCapture(reinterpret_cast<void*>(winId()), true); // CAP-72
        win::setWindowPhysicalRect(reinterpret_cast<void*>(winId()), m_physical);
#endif
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        QPen pen(m_ok ? QColor(60, 200, 90) : QColor(230, 60, 60), 3);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(1.5, 1.5, width() - 3, height() - 3));
    }

private:
    QRect m_physical;
    bool m_ok = true;
    qreal m_scale;
    QPoint m_logicalOrigin;
    QPoint m_physicalOrigin;
};

// ---- 预览面板（CAP-64）----
class ScrollPreviewPanel : public QWidget {
public:
    explicit ScrollPreviewPanel(ScrollCaptureSession* session)
        : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
        , m_session(session)
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
        theme::makeSurface(this);
        auto* layout = new QVBoxLayout(this);
        m_thumb = new QLabel(this);
        m_thumb->setFixedSize(180, 420);
        m_thumb->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        layout->addWidget(m_thumb);
        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        m_status->setFixedWidth(180);
        layout->addWidget(m_status);
        m_auto = new QCheckBox(QObject::tr("Auto scroll"), this);
        m_auto->setChecked(session->autoScroll());
        QObject::connect(m_auto, &QCheckBox::toggled, session, &ScrollCaptureSession::setAutoScroll);
        layout->addWidget(m_auto);
        auto* finish = new QPushButton(QObject::tr("Finish (Enter)"), this);
        QObject::connect(finish, &QPushButton::clicked, session, [session] { session->finish(); });
        layout->addWidget(finish);
        auto* cancel = new QPushButton(QObject::tr("Cancel (Esc)"), this);
        QObject::connect(cancel, &QPushButton::clicked, session, &ScrollCaptureSession::cancel);
        layout->addWidget(cancel);
        m_outputs = new QWidget(this);
        auto* out = new QVBoxLayout(m_outputs);
        out->setContentsMargins(0, 0, 0, 0);
        for (const auto& [kind, text] : QList<QPair<QString, QString>>{{QStringLiteral("copy"), QObject::tr("Copy")}, {QStringLiteral("save"), QObject::tr("Save...")},
                                                                       {QStringLiteral("quickSave"), QObject::tr("Quick save")}, {QStringLiteral("pin"), QObject::tr("Pin (annotate on the pin)")}}) {
            auto* b = new QPushButton(text, m_outputs);
            b->setObjectName(kind);
            const QString k = kind;
            QObject::connect(b, &QPushButton::clicked, session, [session, k] { emit session->outputRequested(k); });
            out->addWidget(b);
        }
        m_outputs->hide();
        layout->addWidget(m_outputs);
        QObject::connect(session, &ScrollCaptureSession::statusChanged, this, [this] { refresh(); });
        setFocusPolicy(Qt::StrongFocus);
    }
    void refresh()
    {
        const QImage t = m_session->thumbnail(180, 420);
        if (!t.isNull())
            m_thumb->setPixmap(QPixmap::fromImage(t));
        QString text = QObject::tr("Height: %1 px, frames: %2").arg(m_session->stitchedHeight()).arg(m_session->frameCount());
        if (m_session->state() == ScrollCaptureSession::State::Finished)
            text += QStringLiteral("\n") + QObject::tr("Done. Choose an output.");
        else if (m_session->reachedEnd())
            text += QStringLiteral("\n") + QObject::tr("Reached the end.");
        else if (!m_session->lastMatchOk())
            text += QStringLiteral("\n") + QObject::tr("Not appended: %1").arg(m_session->lastReason());
        else
            text += QStringLiteral("\n") + QObject::tr("Scroll the content to continue.");
        m_status->setText(text);
        m_status->setStyleSheet(QStringLiteral("color: %1;").arg((m_session->lastMatchOk() ? theme::colors().muted : theme::colors().danger).name()));
        m_outputs->setVisible(m_session->state() == ScrollCaptureSession::State::Finished);
        adjustSize();
    }
    void showExcluded()
    {
        show();
        raise();
        activateWindow();
        setFocus();
#if defined(Q_OS_WIN)
        win::excludeFromCapture(reinterpret_cast<void*>(winId()), true);
#endif
    }

protected:
    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape)
            m_session->cancel();
        else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
            m_session->finish();
        else
            QWidget::keyPressEvent(e);
    }

private:
    ScrollCaptureSession* m_session;
    QLabel* m_thumb;
    QLabel* m_status;
    QCheckBox* m_auto;
    QWidget* m_outputs;
};

// ---- 会话 ----

ScrollCaptureSession::ScrollCaptureSession(IScreenCapture* capture, IInputSimulator* input, Settings& settings, QObject* parent)
    : QObject(parent)
    , m_capture(capture)
    , m_input(input)
    , m_settings(settings)
{
    connect(&m_timer, &QTimer::timeout, this, &ScrollCaptureSession::tick);
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    teardownUi();
}

bool ScrollCaptureSession::begin(const QRect& physicalRegion, bool showUi)
{
    if (m_state != State::Idle || !m_capture || physicalRegion.isEmpty())
        return false;
    // CAP-75：区域横跨缩放比例不同的显示器时拒绝
    const auto screens = m_capture->screens();
    std::optional<qreal> scale;
    ScreenInfo host;
    for (const auto& s : screens) {
        if (!s.physicalGeometry.intersects(physicalRegion))
            continue;
        if (scale && !qFuzzyCompare(*scale, s.scale)) {
            m_error = tr("The area spans monitors with different scaling. Choose an area on one monitor.");
            return false;
        }
        scale = s.scale;
        if (s.physicalGeometry.contains(physicalRegion.topLeft()))
            host = s;
    }
    if (!scale) {
        m_error = tr("The area is not on any monitor.");
        return false;
    }
    m_region = physicalRegion;
    m_direction = m_settings.stringValue(QStringLiteral("scroll.direction")) == QLatin1String("horizontal") ? Direction::Horizontal : Direction::Vertical;
    m_autoScroll = m_settings.boolValue(QStringLiteral("scroll.autoScroll"));
    m_stitcher = stitch::Stitcher();
    m_stitcher.setEndThreshold(m_settings.intValue(QStringLiteral("scroll.endFrames")));
    m_lastFrame = QImage();
    m_stableCount = 0;
    m_lastOk = true;
    m_lastReason.clear();
    m_reachedEnd = false;
    m_rejected = 0;
    m_ticksSinceScroll = 0;
    m_state = State::Running;
    if (showUi) {
        QScreen* qs = QGuiApplication::primaryScreen();
        for (QScreen* s : QGuiApplication::screens())
            if (s->name() == host.id)
                qs = s;
        const QPoint logicalOrigin = qs ? qs->geometry().topLeft() : QPoint();
        m_frame = new ScrollFrameWindow(m_region, *scale, logicalOrigin, host.physicalGeometry.topLeft());
        m_frame->showExcluded();
        m_panel = new ScrollPreviewPanel(this);
        // 面板放在区域右侧，不够放则左侧
        const QPointF regionLogicalRight = QPointF(logicalOrigin) + QPointF(m_region.topRight() + QPoint(12, 0) - host.physicalGeometry.topLeft()) / *scale;
        QPoint pos = regionLogicalRight.toPoint();
        if (qs && pos.x() + 220 > qs->geometry().right())
            pos.setX(static_cast<int>((logicalOrigin.x() + (m_region.left() - host.physicalGeometry.left()) / *scale) - 230));
        m_panel->move(pos);
        m_panel->refresh();
        m_panel->showExcluded();
    }
    m_timer.start(m_settings.intValue(QStringLiteral("scroll.tickMs")));
    emit statusChanged();
    return true;
}

bool ScrollCaptureSession::adjustRegion(const QRect& r)
{
    if (r.isEmpty())
        return false;
    if (m_state == State::Running) {
        const bool sameSize = r.size() == m_region.size();
        const bool alongDirection = m_direction == Direction::Vertical ? r.x() == m_region.x() : r.y() == m_region.y();
        if (!sameSize || !alongDirection)
            return false;
        m_region = r;
        m_reachedEnd = false; // 拖动区域后可以继续截取
        m_stableCount = 0;
        if (m_frame)
            m_frame->setRegion(r);
        emit statusChanged();
        return true;
    }
    m_region = r;
    emit statusChanged();
    return true;
}

void ScrollCaptureSession::setAutoScroll(bool on)
{
    m_autoScroll = on;
    m_ticksSinceScroll = 0;
}

QImage ScrollCaptureSession::grabRegion()
{
    for (const auto& s : m_capture->screens()) {
        if (!s.physicalGeometry.contains(m_region.topLeft()))
            continue;
        auto snap = m_capture->capture(s);
        if (!snap)
            return {};
        QImage f = snap->image.copy(m_region.translated(-s.physicalGeometry.topLeft()));
        f.setDevicePixelRatio(1.0);
        return m_direction == Direction::Horizontal ? stitch::transposed(f) : f.convertToFormat(QImage::Format_RGB32);
    }
    return {};
}

void ScrollCaptureSession::scrollOnce()
{
    if (!m_input)
        return;
    // 鼠标放在区域边缘，减少悬停高亮（CAP-69）
    const QPoint at = m_direction == Direction::Vertical ? QPoint(m_region.right() - 4, m_region.center().y()) : QPoint(m_region.center().x(), m_region.bottom() - 4);
    m_input->mouseWheelAt(at, -m_settings.intValue(QStringLiteral("scroll.speed")), m_direction == Direction::Horizontal);
}

void ScrollCaptureSession::tick()
{
    if (m_state != State::Running)
        return;
    const QImage frame = grabRegion();
    if (frame.isNull())
        return;
    const QVector<quint64> hashes = stitch::rowHashes(frame);
    const bool stable = hashes == m_lastHashes;
    m_lastFrame = frame;
    m_lastHashes = hashes;
    if (!m_stitcher.started()) {
        // 首帧：等画面稳定两次再作为长图开头
        if (stable && ++m_stableCount >= m_settings.intValue(QStringLiteral("scroll.stableFrames")) - 1) {
            m_stitcher.begin(frame);
            m_stitchedHashes = hashes;
            emit statusChanged();
            if (m_autoScroll)
                scrollOnce();
        }
        return;
    }
    if (!stable) {
        m_stableCount = 0;
        return; // 画面还在动
    }
    if (hashes == m_stitchedHashes) {
        // 与上次拼接的帧相同：没有新内容
        ++m_ticksSinceScroll;
        const auto r = m_stitcher.addFrame(frame);
        if (r.reachedEnd && m_autoScroll) {
            m_reachedEnd = true;
            emit statusChanged();
            return;
        }
        if (m_autoScroll && m_ticksSinceScroll >= 2) {
            m_ticksSinceScroll = 0;
            scrollOnce();
        }
        return;
    }
    const auto r = m_stitcher.addFrame(frame);
    m_stitchedHashes = hashes;
    m_lastOk = r.shift.ok;
    m_lastReason = r.shift.reason;
    m_lastShift = r.shift.shift;
    if (!r.shift.ok)
        ++m_rejected;
    m_reachedEnd = r.reachedEnd;
    if (m_frame)
        m_frame->setOk(m_lastOk);
    emit statusChanged();
    if (m_autoScroll && !m_reachedEnd) {
        m_ticksSinceScroll = 0;
        scrollOnce();
    }
}

QImage ScrollCaptureSession::currentImage() const
{
    if (!m_stitcher.started())
        return {};
    const QImage img = m_stitcher.finish();
    return m_direction == Direction::Horizontal ? stitch::transposed(img) : img;
}

QImage ScrollCaptureSession::thumbnail(int maxWidth, int maxHeight) const
{
    if (!m_stitcher.started())
        return {};
    QImage t = m_stitcher.image().thumbnail(m_direction == Direction::Horizontal ? maxHeight : maxWidth, m_direction == Direction::Horizontal ? maxWidth : maxHeight);
    if (m_direction == Direction::Horizontal)
        t = stitch::transposed(t);
    // 当前帧位置：末尾一段用高亮边框表示
    QPainter p(&t);
    QPen pen(m_lastOk ? QColor(60, 200, 90) : QColor(230, 60, 60), 2);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int frameRows = m_lastFrame.isNull() ? 0 : m_lastFrame.height();
    const double factor = m_stitcher.image().height() > 0 ? static_cast<double>(t.height()) / m_stitcher.image().height() : 1.0;
    const int h = std::max(4, static_cast<int>(frameRows * factor));
    p.drawRect(QRect(1, std::max(0, t.height() - h), t.width() - 2, h - 1));
    return t;
}

QImage ScrollCaptureSession::finish()
{
    if (m_state != State::Running)
        return {};
    m_timer.stop();
    m_state = State::Finished;
    const QImage image = currentImage();
    emit statusChanged();
    if (m_panel)
        m_panel->refresh();
    emit finished(image, m_region);
    return image;
}

void ScrollCaptureSession::cancel()
{
    if (m_state == State::Idle)
        return;
    m_timer.stop();
    m_state = State::Idle;
    teardownUi();
    emit cancelled();
}

void ScrollCaptureSession::teardownUi()
{
    if (m_frame) {
        m_frame->hide();
        m_frame->deleteLater();
        m_frame = nullptr;
    }
    if (m_panel) {
        m_panel->hide();
        m_panel->deleteLater();
        m_panel = nullptr;
    }
}

} // namespace ss
