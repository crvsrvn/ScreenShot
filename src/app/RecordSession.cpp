#include "app/RecordSession.h"

#include "app/RecordWidgets.h"
#include "core/config/Settings.h"
#include "core/ports/Platform.h"
#include "core/util/Clock.h"
#include "core/video/InputVisualizer.h"
#include "core/video/Playback.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

namespace ss {

namespace {

PipCorner cornerFromName(const QString& name)
{
    if (name == QLatin1String("topLeft"))
        return PipCorner::TopLeft;
    if (name == QLatin1String("topRight"))
        return PipCorner::TopRight;
    if (name == QLatin1String("bottomLeft"))
        return PipCorner::BottomLeft;
    return PipCorner::BottomRight;
}

} // namespace

RecordSession::RecordSession(IScreenCapture* capture, Platform& platform, Settings& settings, const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_capture(capture)
    , m_platform(platform)
    , m_settings(settings)
    , m_dataDir(dataDir)
{
    m_fps = m_settings.intValue(QStringLiteral("record.fps"));
    m_quality = FrameStore::qualityFromName(m_settings.stringValue(QStringLiteral("record.quality")));
    m_mode = m_settings.stringValue(QStringLiteral("record.mode")) == QLatin1String("quick") ? Mode::Quick : Mode::Normal;
    m_delaySec = m_settings.intValue(QStringLiteral("record.delaySec"));
    m_micEnabled = m_settings.boolValue(QStringLiteral("record.mic"));
    m_systemEnabled = m_settings.boolValue(QStringLiteral("record.system"));
    m_cameraEnabled = m_settings.boolValue(QStringLiteral("record.camera"));
    connect(&m_timer, &QTimer::timeout, this, &RecordSession::tick);
}

RecordSession::~RecordSession()
{
    // 录制中被销毁（程序退出）：帧留在磁盘且索引不标记完成，下次启动可恢复（VID-09）
    if (m_state == State::Recording || m_state == State::Paused) {
        m_timer.stop();
        if (m_store)
            m_store->writeIndex(false);
        if (m_encoder)
            m_encoder->finish();
        m_store.reset();
        m_encoder.reset();
        closeOutput(true);
    } else if (m_state != State::Stopped) {
        closeOutput(false);
    }
    teardownUi();
}

qint64 RecordSession::defaultNowMs()
{
    return monotonicNs() / 1000000;
}

void RecordSession::setMode(Mode mode)
{
    if (m_state == State::Idle)
        m_mode = mode;
}

void RecordSession::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    if (m_frame)
        m_frame->setRecording(s == State::Recording);
    emit stateChanged();
}

bool RecordSession::begin(const QRect& physicalRegion, bool showUi)
{
    if (physicalRegion.width() < 2 || physicalRegion.height() < 2) {
        m_error = tr("The area is too small to record.");
        return false;
    }
    m_region = physicalRegion;
    QImage transparent(m_region.size(), QImage::Format_ARGB32_Premultiplied);
    transparent.fill(Qt::transparent);
    m_annotations.setBaseImage(transparent);
    if (showUi) {
        RecordScreenMap map;
        const QList<ScreenInfo> screens = m_capture->screens();
        for (const ScreenInfo& s : screens) {
            if (s.physicalGeometry.contains(m_region.topLeft())) {
                map.scale = s.scale;
                map.physicalOrigin = s.physicalGeometry.topLeft();
                QScreen* host = QGuiApplication::primaryScreen();
                for (QScreen* qs : QGuiApplication::screens()) {
                    if (qs->name() == s.id)
                        host = qs;
                }
                if (host)
                    map.logicalOrigin = host->geometry().topLeft();
            }
        }
        m_frame = new RecordFrameWindow(m_region, map);
        m_frame->showExcluded();
        m_annotateWindow = new RecordAnnotateWindow(m_region, map, &m_annotations);
        m_annotateWindow->showExcluded();
        m_toolbar = new RecordToolbar(this);
        const QRect logical = map.toLogical(m_region);
        m_toolbar->adjustSize();
        QPoint pos(logical.left(), logical.bottom() + 8);
        if (QScreen* qs = QGuiApplication::screenAt(logical.center()); qs && pos.y() + m_toolbar->height() > qs->availableGeometry().bottom())
            pos.setY(logical.top() - m_toolbar->height() - 8);
        m_toolbar->move(pos);
        m_toolbar->showExcluded();
    }
    setState(State::Idle);
    return true;
}

void RecordSession::start()
{
    if (m_state != State::Idle)
        return;
    if (!openOutput())
        return;
    m_timer.start(std::max(10, 1000 / m_fps));
    if (m_delaySec > 0) {
        m_countdownEnd = nowMs() + m_delaySec * 1000;
        setState(State::Countdown);
        return;
    }
    beginRecording();
}

void RecordSession::beginRecording()
{
    m_startAt = nowMs();
    m_pausedTotal = 0;
    m_lastIndexWrite = m_startAt;
    m_frameCount = 0;
    m_audioSamples = 0;
    m_events.clear();
    if (m_micEnabled && m_platform.audioCapture) {
        m_mic = m_platform.audioCapture();
        if (!m_mic->start(IAudioCapture::Source::Microphone))
            m_mic.reset();
    }
    if (m_systemEnabled && m_platform.audioCapture) {
        m_system = m_platform.audioCapture();
        if (!m_system->start(IAudioCapture::Source::System))
            m_system.reset();
    }
    if (m_cameraEnabled && m_platform.camera)
        m_platform.camera->start();
    setState(State::Recording);
    captureFrame();
}

bool RecordSession::openOutput()
{
    const QString base = QDir(m_dataDir).filePath(QStringLiteral("recordings/") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    if (m_mode == Mode::Quick) {
        if (!m_platform.videoEncoder) {
            m_error = tr("MP4 encoding is not available on this system.");
            return false;
        }
        QDir().mkpath(QDir(m_dataDir).filePath(QStringLiteral("recordings")));
        m_quickPath = base + QStringLiteral(".mp4");
        m_encoder = m_platform.videoEncoder();
        const bool audio = (m_micEnabled || m_systemEnabled) && m_platform.audioCapture;
        if (!m_encoder->open(m_quickPath, m_region.size(), m_fps, audio ? 48000 : 0, audio ? 1 : 0)) {
            m_error = m_encoder->lastError();
            m_encoder.reset();
            return false;
        }
        return true;
    }
    m_store = std::make_shared<FrameStore>();
    if (!m_store->create(base, m_region, m_fps, m_quality)) {
        m_error = tr("Could not create the recording folder %1").arg(base);
        m_store.reset();
        return false;
    }
    return true;
}

void RecordSession::closeOutput(bool keep)
{
    if (m_mic)
        m_mic->stop();
    if (m_system)
        m_system->stop();
    m_mic.reset();
    m_system.reset();
    if (m_platform.camera && m_cameraEnabled)
        m_platform.camera->stop();
    if (m_encoder) {
        m_encoder->finish();
        m_encoder.reset();
        if (!keep)
            QFile::remove(m_quickPath);
    }
    if (m_store) {
        if (keep)
            m_store->writeIndex(true);
        else
            m_store->remove();
    }
    if (!keep) {
        m_store.reset();
        m_quickPath.clear();
    }
}

qint64 RecordSession::elapsedMs() const
{
    if (m_state == State::Recording)
        return nowMs() - m_startAt - m_pausedTotal;
    if (m_state == State::Paused || m_state == State::Stopped)
        return m_pauseAt - m_startAt - m_pausedTotal;
    return 0;
}

qint64 RecordSession::countdownRemainingMs() const
{
    return m_state == State::Countdown ? std::max<qint64>(0, m_countdownEnd - nowMs()) : 0;
}

void RecordSession::pause()
{
    if (m_state != State::Recording)
        return;
    m_pauseAt = nowMs();
    setState(State::Paused);
}

void RecordSession::resume()
{
    if (m_state != State::Paused)
        return;
    m_pausedTotal += nowMs() - m_pauseAt;
    // 暂停期间的声音丢弃
    if (m_mic)
        m_mic->take();
    if (m_system)
        m_system->take();
    setState(State::Recording);
}

void RecordSession::stop()
{
    if (m_state != State::Recording && m_state != State::Paused)
        return;
    if (m_state == State::Recording)
        m_pauseAt = nowMs();
    m_timer.stop();
    captureAudio();
    closeOutput(true);
    setState(State::Stopped);
    teardownUi();
    emit finished();
}

void RecordSession::restart()
{
    if (m_state == State::Idle || m_state == State::Stopped)
        return;
    m_timer.stop();
    closeOutput(false);
    m_annotations.clearAll();
    setState(State::Idle);
}

void RecordSession::cancel()
{
    m_timer.stop();
    closeOutput(false);
    setState(State::Stopped);
    teardownUi();
    emit cancelled();
}

void RecordSession::tick()
{
    if (m_state == State::Countdown) {
        if (nowMs() >= m_countdownEnd)
            beginRecording();
        emit ticked();
        return;
    }
    if (m_state != State::Recording)
        return;
    captureFrame();
    captureAudio();
    // 索引定期落盘，异常退出后可恢复（VID-09）
    if (m_store && nowMs() - m_lastIndexWrite >= 2000) {
        m_store->writeIndex(false);
        m_lastIndexWrite = nowMs();
    }
    emit ticked();
}

void RecordSession::captureFrame()
{
    QImage frame;
    for (const ScreenInfo& s : m_capture->screens()) {
        if (!s.physicalGeometry.contains(m_region.topLeft()))
            continue;
        if (auto snap = m_capture->capture(s)) {
            frame = snap->image.copy(m_region.translated(-s.physicalGeometry.topLeft()));
            frame.setDevicePixelRatio(1.0);
        }
        break;
    }
    if (frame.isNull())
        return;
    const qint64 t = elapsedMs();
    frame = composite(std::move(frame), t);
    if (m_encoder)
        m_encoder->addFrame(frame, t);
    else if (m_store)
        m_store->appendFrame(frame, t);
    ++m_frameCount;
}

QImage RecordSession::composite(QImage frame, qint64 timeMs) const
{
    const bool hasAnnotations = m_annotations.count() > 0;
    const bool hasCamera = m_cameraEnabled && m_platform.camera;
    const bool quickInputs = m_encoder && !m_events.isEmpty();
    if (!hasAnnotations && !hasCamera && !quickInputs)
        return frame;
    QImage out = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    if (hasAnnotations)
        p.drawImage(0, 0, m_annotations.render());
    if (hasCamera)
        paintPictureInPicture(p, out.size(), m_platform.camera->latestFrame(), cornerFromName(m_settings.stringValue(QStringLiteral("record.cameraCorner"))),
                              m_settings.intValue(QStringLiteral("record.cameraSize")));
    if (quickInputs) {
        InputVisualOptions o;
        o.showClicks = o.showWheel = m_settings.boolValue(QStringLiteral("record.showClicks"));
        o.showKeys = m_settings.boolValue(QStringLiteral("record.showKeys"));
        o.clickDurationMs = m_settings.intValue(QStringLiteral("record.clickAnimMs"));
        paintInputEvents(p, m_events, timeMs, out.size(), o);
    }
    return out;
}

void RecordSession::captureAudio()
{
    if (!m_mic && !m_system)
        return;
    QVector<qint16> mic = m_mic ? m_mic->take() : QVector<qint16>();
    QVector<qint16> system = m_system ? m_system->take() : QVector<qint16>();
    if (mic.isEmpty() && system.isEmpty())
        return;
    // 让两路长度对齐到已过去的时间：没声音的一路（例如系统环回无播放）补零
    const qint64 expected = elapsedMs() * 48 - m_audioSamples;
    const qsizetype n = std::max<qsizetype>({mic.size(), system.size(), static_cast<qsizetype>(std::max<qint64>(0, expected))});
    if (m_mic && mic.size() < n)
        mic.insert(0, n - mic.size(), 0);
    if (m_system && system.size() < n)
        system.insert(0, n - system.size(), 0);
    const QVector<qint16> mixed = mixAudio(mic, m_mic ? m_settings.intValue(QStringLiteral("record.micVolume")) : 0, system, m_system ? m_settings.intValue(QStringLiteral("record.systemVolume")) : 0);
    const qint64 t = m_audioSamples * 1000 / 48000;
    if (m_encoder)
        m_encoder->addAudio(mixed, t);
    else if (m_store)
        m_store->appendAudio(mixed, 48000);
    m_audioSamples += static_cast<int>(mixed.size());
}

// ---- 输入事件（VID-06）----

void RecordSession::noteMouseButton(int button, bool down, QPoint physical)
{
    if (m_state != State::Recording || !down || !m_region.contains(physical))
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::Click;
    e.timeMs = elapsedMs();
    e.pos = physical - m_region.topLeft();
    e.button = button;
    m_events.append(e);
    if (m_store)
        m_store->addEvent(e);
}

void RecordSession::noteWheel(int delta, QPoint physical)
{
    if (m_state != State::Recording || !m_region.contains(physical))
        return;
    InputEvent e;
    e.kind = InputEvent::Kind::Wheel;
    e.timeMs = elapsedMs();
    e.pos = physical - m_region.topLeft();
    e.wheel = delta;
    m_events.append(e);
    if (m_store)
        m_store->addEvent(e);
}

QString RecordSession::keyName(int vk)
{
    static const QHash<int, QString> named{{0x08, QStringLiteral("Backspace")}, {0x09, QStringLiteral("Tab")}, {0x0D, QStringLiteral("Enter")}, {0x1B, QStringLiteral("Esc")},
                                           {0x20, QStringLiteral("Space")}, {0x21, QStringLiteral("PgUp")}, {0x22, QStringLiteral("PgDn")}, {0x23, QStringLiteral("End")},
                                           {0x24, QStringLiteral("Home")}, {0x25, QStringLiteral("Left")}, {0x26, QStringLiteral("Up")}, {0x27, QStringLiteral("Right")},
                                           {0x28, QStringLiteral("Down")}, {0x2D, QStringLiteral("Ins")}, {0x2E, QStringLiteral("Del")}, {0x5B, QStringLiteral("Win")},
                                           {0x5C, QStringLiteral("Win")}, {0xBA, QStringLiteral(";")}, {0xBB, QStringLiteral("=")}, {0xBC, QStringLiteral(",")},
                                           {0xBD, QStringLiteral("-")}, {0xBE, QStringLiteral(".")}, {0xBF, QStringLiteral("/")}, {0xC0, QStringLiteral("`")},
                                           {0xDB, QStringLiteral("[")}, {0xDC, QStringLiteral("\\")}, {0xDD, QStringLiteral("]")}, {0xDE, QStringLiteral("'")}};
    if (vk == 0x10 || vk == 0xA0 || vk == 0xA1)
        return QStringLiteral("Shift");
    if (vk == 0x11 || vk == 0xA2 || vk == 0xA3)
        return QStringLiteral("Ctrl");
    if (vk == 0x12 || vk == 0xA4 || vk == 0xA5)
        return QStringLiteral("Alt");
    if ((vk >= 0x30 && vk <= 0x39) || (vk >= 0x41 && vk <= 0x5A))
        return QString(QChar(vk));
    if (vk >= 0x60 && vk <= 0x69)
        return QStringLiteral("Num%1").arg(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87)
        return QStringLiteral("F%1").arg(vk - 0x70 + 1);
    return named.value(vk, QStringLiteral("Key%1").arg(vk));
}

void RecordSession::noteKey(int vk, bool down)
{
    const QString name = keyName(vk);
    const bool modifier = name == QLatin1String("Shift") || name == QLatin1String("Ctrl") || name == QLatin1String("Alt") || name == QLatin1String("Win");
    if (modifier) {
        if (down)
            m_modifiersDown.insert(vk);
        else
            m_modifiersDown.remove(vk);
    }
    if (m_state != State::Recording || !down || modifier)
        return;
    QStringList parts;
    QSet<QString> seen;
    for (int m : m_modifiersDown) {
        const QString n = keyName(m);
        if (!seen.contains(n)) {
            seen.insert(n);
            parts.append(n);
        }
    }
    parts.sort();
    parts.append(name);
    InputEvent e;
    e.kind = InputEvent::Kind::Key;
    e.timeMs = elapsedMs();
    e.keyText = parts.join(QLatin1Char('+'));
    m_events.append(e);
    if (m_store)
        m_store->addEvent(e);
}

// ---- 标注 / 摄像头 ----

void RecordSession::setAnnotating(bool on)
{
    m_annotating = on;
    if (m_annotateWindow)
        m_annotateWindow->setActive(on);
}

void RecordSession::setCameraEnabled(bool on)
{
    if (m_cameraEnabled == on)
        return;
    m_cameraEnabled = on;
    if (m_platform.camera && (m_state == State::Recording || m_state == State::Paused)) {
        if (on)
            m_platform.camera->start();
        else
            m_platform.camera->stop();
    }
}

void RecordSession::teardownUi()
{
    for (QWidget* w : {static_cast<QWidget*>(m_frame), static_cast<QWidget*>(m_toolbar), static_cast<QWidget*>(m_annotateWindow)}) {
        if (w) {
            w->hide();
            w->deleteLater();
        }
    }
    m_frame = nullptr;
    m_toolbar = nullptr;
    m_annotateWindow = nullptr;
}

} // namespace ss
