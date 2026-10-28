#include "app/RecordWidgets.h"

#include "app/Theme.h"

#include "app/RecordSession.h"
#include "core/annotation/AnnotationDocument.h"
#include "core/annotation/Shapes.h"
#include "core/video/Recording.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>

#if defined(Q_OS_WIN)
#include "platform/windows/WinWindowTools.h"
#endif

namespace ss {

QRect RecordScreenMap::toLogical(const QRect& physical) const
{
    const QPointF logical = QPointF(logicalOrigin) + QPointF(physical.topLeft() - physicalOrigin) / scale;
    return QRect(logical.toPoint(), QSize(static_cast<int>(std::ceil(physical.width() / scale)), static_cast<int>(std::ceil(physical.height() / scale))));
}

QPoint RecordScreenMap::toPhysical(QPoint logical) const
{
    return physicalOrigin + ((QPointF(logical) - QPointF(logicalOrigin)) * scale).toPoint();
}

namespace {

void excludeWindow(QWidget* w, const QRect& physical)
{
    w->show();
#if defined(Q_OS_WIN)
    win::excludeFromCapture(reinterpret_cast<void*>(w->winId()), true);
    if (!physical.isNull())
        win::setWindowPhysicalRect(reinterpret_cast<void*>(w->winId()), physical);
#else
    Q_UNUSED(physical);
#endif
}

} // namespace

// ---- 边框 ----

RecordFrameWindow::RecordFrameWindow(const QRect& physical, const RecordScreenMap& map)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_physical = physical.adjusted(-3, -3, 3, 3);
    setGeometry(map.toLogical(m_physical));
}

void RecordFrameWindow::setRecording(bool on)
{
    m_recording = on;
    update();
}

void RecordFrameWindow::showExcluded()
{
    excludeWindow(this, m_physical);
}

void RecordFrameWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setPen(QPen(m_recording ? QColor(230, 50, 50) : QColor(60, 140, 255), 3));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(1, 1, -2, -2));
}

// ---- 工具栏 ----

RecordToolbar::RecordToolbar(RecordSession* session)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
    , m_session(session)
{
    setAttribute(Qt::WA_ShowWithoutActivating);
    theme::makeSurface(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(6);
    auto* row = new QHBoxLayout;
    outer->addLayout(row);
    auto add = [&](const QString& name, const QString& text, auto slot) {
        auto* b = new QToolButton(this);
        b->setText(text);
        b->setObjectName(name);
        connect(b, &QToolButton::clicked, m_session, slot);
        row->addWidget(b);
        m_buttons.insert(name, b);
        return b;
    };
    add(QStringLiteral("start"), tr("Start"), &RecordSession::start);
    add(QStringLiteral("pause"), tr("Pause"), &RecordSession::pause);
    add(QStringLiteral("resume"), tr("Resume"), &RecordSession::resume);
    add(QStringLiteral("stop"), tr("Stop"), &RecordSession::stop);
    add(QStringLiteral("restart"), tr("Re-record"), &RecordSession::restart);
    m_time = new QLabel(QStringLiteral("00:00"), this);
    m_time->setMinimumWidth(48);
    row->addWidget(m_time);
    auto* annotate = new QToolButton(this);
    annotate->setText(tr("Annotate"));
    annotate->setCheckable(true);
    connect(annotate, &QToolButton::toggled, m_session, &RecordSession::setAnnotating);
    row->addWidget(annotate);
    m_buttons.insert(QStringLiteral("annotate"), annotate);
    add(QStringLiteral("cancel"), tr("Cancel"), &RecordSession::cancel);

    // 第二行：开始前的选项
    m_options = new QWidget(this);
    auto* opts = new QHBoxLayout(m_options);
    opts->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_options);
    auto* mode = new QComboBox(m_options);
    mode->addItem(tr("Normal (edit, then export)"), QStringLiteral("normal"));
    mode->addItem(tr("Quick (MP4 while recording)"), QStringLiteral("quick"));
    mode->setCurrentIndex(m_session->mode() == RecordSession::Mode::Quick ? 1 : 0);
    connect(mode, &QComboBox::currentIndexChanged, m_session, [this](int i) { m_session->setMode(i == 1 ? RecordSession::Mode::Quick : RecordSession::Mode::Normal); });
    opts->addWidget(mode);
    auto* delay = new QSpinBox(m_options);
    delay->setRange(0, 10);
    delay->setSuffix(tr(" s delay"));
    delay->setObjectName(QStringLiteral("delay"));
    delay->setValue(m_session->delaySec());
    connect(delay, &QSpinBox::valueChanged, m_session, &RecordSession::setDelaySec);
    opts->addWidget(delay);
    auto* fps = new QComboBox(m_options);
    for (int f : {5, 16, 24, 30, 60})
        fps->addItem(tr("%1 fps").arg(f), f);
    fps->setCurrentIndex(fps->findData(m_session->fps()));
    connect(fps, &QComboBox::currentIndexChanged, m_session, [this, fps](int i) { m_session->setFps(fps->itemData(i).toInt()); });
    opts->addWidget(fps);
    auto* quality = new QComboBox(m_options);
    quality->addItem(tr("Light"), QStringLiteral("light"));
    quality->addItem(tr("Standard"), QStringLiteral("standard"));
    quality->addItem(tr("Raw"), QStringLiteral("raw"));
    quality->setCurrentIndex(quality->findData(FrameStore::qualityName(m_session->quality())));
    connect(quality, &QComboBox::currentIndexChanged, m_session, [this, quality](int i) { m_session->setQuality(FrameStore::qualityFromName(quality->itemData(i).toString())); });
    opts->addWidget(quality);
    auto* mic = new QCheckBox(tr("Microphone"), m_options);
    mic->setChecked(m_session->micEnabled());
    connect(mic, &QCheckBox::toggled, m_session, &RecordSession::setMicEnabled);
    opts->addWidget(mic);
    auto* system = new QCheckBox(tr("System sound"), m_options);
    system->setChecked(m_session->systemAudioEnabled());
    connect(system, &QCheckBox::toggled, m_session, &RecordSession::setSystemAudioEnabled);
    opts->addWidget(system);
    auto* camera = new QCheckBox(tr("Camera"), m_options);
    camera->setChecked(m_session->cameraEnabled());
    connect(camera, &QCheckBox::toggled, m_session, &RecordSession::setCameraEnabled);
    opts->addWidget(camera);
    connect(m_session, &RecordSession::stateChanged, this, &RecordToolbar::refresh);
    connect(m_session, &RecordSession::ticked, this, &RecordToolbar::refresh);
    refresh();
}

void RecordToolbar::refresh()
{
    const auto st = m_session->state();
    m_buttons[QStringLiteral("start")]->setVisible(st == RecordSession::State::Idle);
    m_buttons[QStringLiteral("pause")]->setVisible(st == RecordSession::State::Recording);
    m_buttons[QStringLiteral("resume")]->setVisible(st == RecordSession::State::Paused);
    m_buttons[QStringLiteral("stop")]->setVisible(st == RecordSession::State::Recording || st == RecordSession::State::Paused);
    m_buttons[QStringLiteral("restart")]->setVisible(st == RecordSession::State::Recording || st == RecordSession::State::Paused);
    m_buttons[QStringLiteral("annotate")]->setVisible(st == RecordSession::State::Recording || st == RecordSession::State::Paused);
    m_options->setVisible(st == RecordSession::State::Idle);
    if (st == RecordSession::State::Countdown) {
        m_time->setText(tr("Starting in %1").arg((m_session->countdownRemainingMs() + 999) / 1000));
    } else {
        const qint64 s = m_session->elapsedMs() / 1000;
        m_time->setText(QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    }
    adjustSize();
}

void RecordToolbar::showExcluded()
{
    excludeWindow(this, QRect());
}

// ---- 标注层 ----

RecordAnnotateWindow::RecordAnnotateWindow(const QRect& physical, const RecordScreenMap& map, AnnotationDocument* document)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus)
    , m_physical(physical)
    , m_map(map)
    , m_document(document)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setGeometry(map.toLogical(physical));
    setActive(false);
}

RecordAnnotateWindow::~RecordAnnotateWindow() = default;

void RecordAnnotateWindow::setActive(bool on)
{
    m_active = on;
    setAttribute(Qt::WA_TransparentForMouseEvents, !on);
    setWindowFlag(Qt::WindowTransparentForInput, !on);
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    if (isVisible())
        showExcluded();
}

void RecordAnnotateWindow::showExcluded()
{
    excludeWindow(this, m_physical);
}

QPointF RecordAnnotateWindow::toRegion(QPointF logical) const
{
    return logical * m_map.scale;
}

void RecordAnnotateWindow::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(1.0 / m_map.scale, 1.0 / m_map.scale);
    QImage rendered = m_document->render();
    if (!rendered.isNull())
        p.drawImage(0, 0, rendered);
    if (m_current) {
        RenderContext ctx;
        ctx.below = &rendered;
        m_current->paint(p, ctx);
    }
    if (m_active) {
        p.setPen(QPen(QColor(255, 200, 0, 160), 2 * m_map.scale));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(0, 0, m_physical.width(), m_physical.height()).adjusted(1, 1, -1, -1));
    }
}

void RecordAnnotateWindow::mousePressEvent(QMouseEvent* e)
{
    if (!m_active)
        return;
    if (e->button() == Qt::RightButton) {
        m_document->undo(); // 右键撤销上一笔
        update();
        return;
    }
    m_current = std::make_unique<PencilAnnotation>();
    m_current->setStyle(style);
    m_current->points().append(toRegion(e->position()));
    update();
}

void RecordAnnotateWindow::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_current)
        return;
    m_current->points().append(toRegion(e->position()));
    update();
}

void RecordAnnotateWindow::mouseReleaseEvent(QMouseEvent*)
{
    if (!m_current)
        return;
    if (m_current->points().size() > 1)
        m_document->add(std::move(m_current));
    m_current.reset();
    update();
}

} // namespace ss
