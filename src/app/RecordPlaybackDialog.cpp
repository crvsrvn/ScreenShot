#include "app/RecordPlaybackDialog.h"

#include "app/WindowChrome.h"
#include "app/OutputActions.h"
#include "core/config/Settings.h"
#include "core/output/FileNaming.h"
#include "core/video/GifEncoder.h"
#include "core/video/InputVisualizer.h"
#include "core/video/WebpAnimEncoder.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

namespace ss {

namespace {

QString clockText(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2.%3").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')).arg((ms % 1000) / 100);
}

} // namespace

RecordPlaybackDialog::RecordPlaybackDialog(std::shared_ptr<FrameStore> store, Settings& settings, OutputActions& output, VideoEncoderFactory encoder, QWidget* parent)
    : QDialog(parent)
    , m_store(std::move(store))
    , m_settings(settings)
    , m_output(output)
    , m_encoder(std::move(encoder))
{
    setWindowTitle(tr("Recording"));
    for (int i = 0; i < m_store->frameCount(); ++i)
        m_timestamps.append(m_store->timestampMs(i));
    m_duration = m_store->durationMs();
    m_progress = progressStyleFromName(m_settings.stringValue(QStringLiteral("record.progress")));
    m_showInputs = m_settings.boolValue(QStringLiteral("record.showClicks")) || m_settings.boolValue(QStringLiteral("record.showKeys"));
    m_inputOptions.showClicks = m_inputOptions.showWheel = m_settings.boolValue(QStringLiteral("record.showClicks"));
    m_inputOptions.showKeys = m_settings.boolValue(QStringLiteral("record.showKeys"));
    m_inputOptions.clickDurationMs = m_settings.intValue(QStringLiteral("record.clickAnimMs"));
    connect(&m_timer, &QTimer::timeout, this, &RecordPlaybackDialog::advance);
    buildUi();
    refreshPreview();
    theme::decorateWindow(this);
}

RecordPlaybackDialog::~RecordPlaybackDialog() = default;

void RecordPlaybackDialog::buildUi()
{
    auto* layout = new QVBoxLayout(this);
    m_preview = new QLabel(this);
    m_preview->setMinimumSize(320, 180);
    m_preview->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_preview, 1);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, static_cast<int>(m_duration));
    connect(m_slider, &QSlider::sliderMoved, this, [this](int v) { seek(v); });
    layout->addWidget(m_slider);

    auto* controls = new QHBoxLayout;
    layout->addLayout(controls);
    m_playButton = new QPushButton(tr("Play"), this);
    connect(m_playButton, &QPushButton::clicked, this, [this] { playing() ? pause() : play(); });
    controls->addWidget(m_playButton);
    auto* speed = new QComboBox(this);
    for (double v : {0.5, 1.0, 1.5, 2.0, 4.0})
        speed->addItem(QStringLiteral("%1x").arg(v), v);
    speed->setCurrentIndex(1);
    connect(speed, &QComboBox::currentIndexChanged, this, [this, speed](int i) { setSpeed(speed->itemData(i).toDouble()); });
    controls->addWidget(speed);
    m_timeLabel = new QLabel(this);
    controls->addWidget(m_timeLabel);
    controls->addStretch();
    auto* trimStart = new QPushButton(tr("Trim start here"), this);
    connect(trimStart, &QPushButton::clicked, this, &RecordPlaybackDialog::trimStartHere);
    controls->addWidget(trimStart);
    auto* trimEnd = new QPushButton(tr("Trim end here"), this);
    connect(trimEnd, &QPushButton::clicked, this, &RecordPlaybackDialog::trimEndHere);
    controls->addWidget(trimEnd);
    auto* reset = new QPushButton(tr("Reset trim"), this);
    connect(reset, &QPushButton::clicked, this, &RecordPlaybackDialog::resetTrim);
    controls->addWidget(reset);
    m_trimLabel = new QLabel(this);
    controls->addWidget(m_trimLabel);

    auto* options = new QHBoxLayout;
    layout->addLayout(options);
    auto* progress = new QComboBox(this);
    progress->addItem(tr("No progress overlay"), QStringLiteral("none"));
    progress->addItem(tr("Progress bar"), QStringLiteral("bar"));
    progress->addItem(tr("Elapsed / total"), QStringLiteral("text"));
    progress->addItem(tr("Countdown"), QStringLiteral("countdown"));
    progress->setCurrentIndex(progress->findData(m_settings.stringValue(QStringLiteral("record.progress"))));
    connect(progress, &QComboBox::currentIndexChanged, this, [this, progress](int i) {
        setProgressStyle(progressStyleFromName(progress->itemData(i).toString()));
        refreshPreview();
    });
    options->addWidget(progress);
    auto* inputs = new QCheckBox(tr("Show clicks and keys"), this);
    inputs->setChecked(m_showInputs);
    connect(inputs, &QCheckBox::toggled, this, [this](bool on) {
        setShowInputs(on);
        refreshPreview();
    });
    options->addWidget(inputs);
    auto* anim = new QSpinBox(this);
    anim->setRange(100, 2000);
    anim->setSingleStep(100);
    anim->setSuffix(tr(" ms click animation"));
    anim->setValue(m_inputOptions.clickDurationMs);
    connect(anim, &QSpinBox::valueChanged, this, [this](int v) {
        setClickAnimMs(v);
        refreshPreview();
    });
    options->addWidget(anim);
    options->addStretch();
    m_formatBox = new QComboBox(this);
    m_formatBox->addItem(QStringLiteral("MP4"), QStringLiteral("mp4"));
    m_formatBox->addItem(QStringLiteral("GIF"), QStringLiteral("gif"));
    m_formatBox->addItem(QStringLiteral("WebP"), QStringLiteral("webp"));
    m_formatBox->setCurrentIndex(std::max(0, m_formatBox->findData(m_settings.stringValue(QStringLiteral("record.format")))));
    options->addWidget(m_formatBox);
    auto* save = new QPushButton(tr("Save..."), this);
    connect(save, &QPushButton::clicked, this, &RecordPlaybackDialog::save);
    options->addWidget(save);
    auto* quick = new QPushButton(tr("Quick save"), this);
    connect(quick, &QPushButton::clicked, this, &RecordPlaybackDialog::quickSave);
    options->addWidget(quick);
    auto* copy = new QPushButton(tr("Copy"), this);
    connect(copy, &QPushButton::clicked, this, &RecordPlaybackDialog::copyToClipboard);
    options->addWidget(copy);
    auto* discard = new QPushButton(tr("Discard"), this);
    connect(discard, &QPushButton::clicked, this, &RecordPlaybackDialog::discard);
    options->addWidget(discard);
}

// ---- 播放 ----

void RecordPlaybackDialog::play()
{
    if (m_position >= m_trim.clamp(m_duration))
        m_position = m_trim.startMs;
    m_wall.start();
    m_timer.start(33);
    m_playButton->setText(tr("Pause"));
}

void RecordPlaybackDialog::pause()
{
    m_timer.stop();
    m_playButton->setText(tr("Play"));
}

void RecordPlaybackDialog::advance()
{
    const qint64 delta = static_cast<qint64>(m_wall.restart() * m_speed);
    m_position += delta;
    if (m_position >= m_trim.clamp(m_duration)) {
        m_position = m_trim.clamp(m_duration);
        pause();
    }
    refreshPreview();
}

void RecordPlaybackDialog::seek(qint64 ms)
{
    m_position = std::clamp<qint64>(ms, 0, m_duration);
    refreshPreview();
}

void RecordPlaybackDialog::trimStartHere()
{
    setTrim(m_position, m_trim.endMs);
}

void RecordPlaybackDialog::trimEndHere()
{
    setTrim(m_trim.startMs, m_position);
}

void RecordPlaybackDialog::resetTrim()
{
    setTrim(0, -1);
}

void RecordPlaybackDialog::setTrim(qint64 startMs, qint64 endMs)
{
    m_trim.startMs = std::clamp<qint64>(startMs, 0, m_duration);
    m_trim.endMs = endMs < 0 ? -1 : std::clamp<qint64>(endMs, m_trim.startMs + 1, m_duration);
    refreshPreview();
}

QImage RecordPlaybackDialog::renderAt(qint64 absoluteMs) const
{
    const int index = frameIndexAt(m_timestamps, absoluteMs);
    if (index < 0)
        return {};
    QImage img = m_store->frame(index).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter p(&img);
    if (m_showInputs)
        paintInputEvents(p, m_store->events(), absoluteMs, img.size(), m_inputOptions);
    paintProgress(p, img.size(), absoluteMs - m_trim.startMs, m_trim.clamp(m_duration) - m_trim.startMs, m_progress);
    return img;
}

void RecordPlaybackDialog::refreshPreview()
{
    const QImage img = renderAt(m_position);
    if (!img.isNull())
        m_preview->setPixmap(QPixmap::fromImage(img.scaled(m_preview->size().expandedTo(QSize(320, 180)), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_slider->setValue(static_cast<int>(m_position));
    m_timeLabel->setText(clockText(m_position) + QStringLiteral(" / ") + clockText(m_duration));
    m_trimLabel->setText(tr("Keep %1 - %2").arg(clockText(m_trim.startMs), clockText(m_trim.clamp(m_duration))));
}

// ---- 导出 ----

QString RecordPlaybackDialog::format() const
{
    return m_formatBox->currentData().toString();
}

void RecordPlaybackDialog::setFormat(const QString& format)
{
    m_formatBox->setCurrentIndex(std::max(0, m_formatBox->findData(format)));
}

bool RecordPlaybackDialog::exportTo(const QString& path, const QString& format)
{
    m_error.clear();
    const QVector<ExportFrame> plan = planExport(m_timestamps, m_duration, m_trim, m_store->fps());
    if (plan.isEmpty()) {
        m_error = tr("Nothing to export.");
        return false;
    }
    const qint64 totalMs = m_trim.clamp(m_duration) - m_trim.startMs;
    if (format == QLatin1String("mp4"))
        return exportMp4(path, plan, totalMs);
    const QSize size = m_store->frameSize();
    GifEncoder gif;
    WebpAnimEncoder webp;
    const bool isGif = format == QLatin1String("gif");
    if (isGif ? !gif.open(path, size) : !webp.open(path, size, m_settings.intValue(QStringLiteral("record.webpQuality")))) {
        m_error = tr("Could not open %1").arg(path);
        return false;
    }
    const int step = 1000 / std::max(1, m_store->fps());
    for (int i = 0; i < plan.size(); ++i) {
        const QImage frame = renderAt(m_trim.startMs + plan[i].timeMs);
        const int delay = i + 1 < plan.size() ? static_cast<int>(plan[i + 1].timeMs - plan[i].timeMs) : step;
        if (isGif)
            gif.addFrame(frame, delay);
        else
            webp.addFrame(frame, static_cast<int>(plan[i].timeMs));
        if (i % 10 == 0)
            QCoreApplication::processEvents();
    }
    if (!(isGif ? gif.finish() : webp.finish())) {
        m_error = tr("Could not write %1").arg(path);
        return false;
    }
    emit exported(path);
    return true;
}

bool RecordPlaybackDialog::exportMp4(const QString& path, const QVector<ExportFrame>& plan, qint64 totalMs)
{
    if (!m_encoder) {
        m_error = tr("MP4 encoding is not available on this system.");
        return false;
    }
    std::unique_ptr<IVideoEncoder> enc = m_encoder();
    const int rate = m_store->audioSampleRate();
    if (!enc->open(path, m_store->frameSize(), m_store->fps(), rate, rate > 0 ? 1 : 0)) {
        m_error = enc->lastError();
        return false;
    }
    for (int i = 0; i < plan.size(); ++i) {
        enc->addFrame(renderAt(m_trim.startMs + plan[i].timeMs), plan[i].timeMs);
        if (i % 10 == 0)
            QCoreApplication::processEvents();
    }
    if (rate > 0) {
        const QVector<qint16> all = m_store->audio();
        const qsizetype from = std::min<qsizetype>(all.size(), m_trim.startMs * rate / 1000);
        const qsizetype to = std::min<qsizetype>(all.size(), (m_trim.startMs + totalMs) * rate / 1000);
        for (qsizetype pos = from; pos < to; pos += rate) {
            const qsizetype n = std::min<qsizetype>(rate, to - pos);
            enc->addAudio(all.mid(pos, n), (pos - from) * 1000 / rate);
        }
    }
    if (!enc->finish()) {
        m_error = enc->lastError();
        return false;
    }
    emit exported(path);
    return true;
}

QString RecordPlaybackDialog::exportToDir(const QString& dir)
{
    QDir().mkpath(dir);
    const QString path = uniqueFilePath(dir, m_output.makeFileName(m_store->frame(0)), format());
    return exportTo(path, format()) ? path : QString();
}

QString RecordPlaybackDialog::save()
{
    QString path = m_dialogOverride;
    if (path.isEmpty()) {
        const QString suggested = QDir(m_output.defaultSaveDir()).filePath(m_output.makeFileName(m_store->frame(0)) + QLatin1Char('.') + format());
        path = QFileDialog::getSaveFileName(this, tr("Save recording"), suggested, QStringLiteral("%1 (*.%2)").arg(format().toUpper(), format()));
    }
    if (path.isEmpty())
        return {};
    return exportTo(path, format()) ? path : QString();
}

QString RecordPlaybackDialog::quickSave()
{
    return exportToDir(m_output.quickSaveDir());
}

QString RecordPlaybackDialog::copyToClipboard()
{
    const QString path = exportToDir(m_output.clipboardFileDir());
    if (path.isEmpty())
        return {};
    auto* mime = new QMimeData;
    mime->setUrls({QUrl::fromLocalFile(path)});
    QGuiApplication::clipboard()->setMimeData(mime);
    return path;
}

void RecordPlaybackDialog::discard()
{
    pause();
    m_store->remove();
    reject();
}

} // namespace ss
