#include "core/video/Recording.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace ss {

QJsonObject InputEvent::toJson() const
{
    return {{QStringLiteral("kind"), static_cast<int>(kind)}, {QStringLiteral("t"), static_cast<double>(timeMs)}, {QStringLiteral("x"), pos.x()}, {QStringLiteral("y"), pos.y()},
            {QStringLiteral("button"), button}, {QStringLiteral("wheel"), wheel}, {QStringLiteral("key"), keyText}};
}

InputEvent InputEvent::fromJson(const QJsonObject& o)
{
    InputEvent e;
    e.kind = static_cast<Kind>(o.value(QStringLiteral("kind")).toInt());
    e.timeMs = static_cast<qint64>(o.value(QStringLiteral("t")).toDouble());
    e.pos = QPoint(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt());
    e.button = o.value(QStringLiteral("button")).toInt(1);
    e.wheel = o.value(QStringLiteral("wheel")).toInt();
    e.keyText = o.value(QStringLiteral("key")).toString();
    return e;
}

QString FrameStore::qualityName(Quality q)
{
    switch (q) {
    case Quality::Light: return QStringLiteral("light");
    case Quality::Raw: return QStringLiteral("raw");
    case Quality::Standard: break;
    }
    return QStringLiteral("standard");
}

FrameStore::Quality FrameStore::qualityFromName(const QString& name)
{
    if (name == QLatin1String("light"))
        return Quality::Light;
    if (name == QLatin1String("raw"))
        return Quality::Raw;
    return Quality::Standard;
}

QString FrameStore::framePath(int index) const
{
    return QDir(m_dir).filePath(QStringLiteral("f%1.%2").arg(index, 6, 10, QLatin1Char('0')).arg(m_frameExt));
}

bool FrameStore::create(const QString& dir, const QRect& region, int fps, Quality quality)
{
    QDir d(dir);
    if (d.exists())
        d.removeRecursively();
    if (!QDir().mkpath(dir))
        return false;
    m_dir = dir;
    m_region = region;
    m_fps = fps;
    m_quality = quality;
    m_frameExt = quality == Quality::Raw ? QStringLiteral("png") : QStringLiteral("jpg");
    m_frameSize = QSize();
    m_timestamps.clear();
    m_events.clear();
    m_audioRate = 0;
    m_finished = false;
    QFile::remove(d.filePath(QStringLiteral("audio.pcm")));
    return writeIndex(false);
}

bool FrameStore::open(const QString& dir)
{
    QFile f(QDir(dir).filePath(QStringLiteral("recording.json")));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_dir = dir;
    const QJsonArray r = o.value(QStringLiteral("region")).toArray();
    if (r.size() == 4)
        m_region = QRect(r[0].toInt(), r[1].toInt(), r[2].toInt(), r[3].toInt());
    m_fps = o.value(QStringLiteral("fps")).toInt(30);
    m_quality = qualityFromName(o.value(QStringLiteral("quality")).toString());
    m_frameExt = o.value(QStringLiteral("ext")).toString(QStringLiteral("jpg"));
    const QJsonArray size = o.value(QStringLiteral("size")).toArray();
    if (size.size() == 2)
        m_frameSize = QSize(size[0].toInt(), size[1].toInt());
    m_timestamps.clear();
    for (const QJsonValue& v : o.value(QStringLiteral("timestamps")).toArray())
        m_timestamps.append(static_cast<qint64>(v.toDouble()));
    m_events.clear();
    for (const QJsonValue& v : o.value(QStringLiteral("events")).toArray())
        m_events.append(InputEvent::fromJson(v.toObject()));
    m_audioRate = o.value(QStringLiteral("audioRate")).toInt(0);
    m_finished = o.value(QStringLiteral("finished")).toBool(false);
    // 索引可能落后于磁盘上的帧（崩溃前没来得及写）：补上多出来的帧，时间按帧率推算
    for (int i = frameCount(); QFile::exists(framePath(i)); ++i)
        m_timestamps.append(m_timestamps.isEmpty() ? 0 : m_timestamps.last() + 1000 / std::max(1, m_fps));
    return true;
}

bool FrameStore::appendFrame(const QImage& frame, qint64 timestampMs)
{
    if (m_dir.isEmpty() || frame.isNull())
        return false;
    QImage out = frame;
    if (m_quality == Quality::Light)
        out = frame.scaled(frame.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (m_frameSize.isEmpty())
        m_frameSize = out.size();
    const int index = frameCount();
    const bool ok = m_quality == Quality::Raw ? out.save(framePath(index), "PNG") : out.convertToFormat(QImage::Format_RGB32).save(framePath(index), "JPG", m_quality == Quality::Light ? 75 : 88);
    if (!ok)
        return false;
    m_timestamps.append(timestampMs);
    return true;
}

QImage FrameStore::frame(int index) const
{
    if (index < 0 || index >= frameCount())
        return {};
    return QImage(framePath(index));
}

qint64 FrameStore::durationMs() const
{
    if (m_timestamps.isEmpty())
        return 0;
    return m_timestamps.last() + 1000 / std::max(1, m_fps);
}

bool FrameStore::appendAudio(const QVector<qint16>& samples, int sampleRate)
{
    if (m_dir.isEmpty() || samples.isEmpty())
        return false;
    m_audioRate = sampleRate;
    QFile f(QDir(m_dir).filePath(QStringLiteral("audio.pcm")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
        return false;
    f.write(reinterpret_cast<const char*>(samples.constData()), samples.size() * static_cast<qint64>(sizeof(qint16)));
    return true;
}

QVector<qint16> FrameStore::audio() const
{
    QVector<qint16> out;
    QFile f(QDir(m_dir).filePath(QStringLiteral("audio.pcm")));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray bytes = f.readAll();
    out.resize(static_cast<qsizetype>(bytes.size() / sizeof(qint16)));
    memcpy(out.data(), bytes.constData(), out.size() * sizeof(qint16));
    return out;
}

bool FrameStore::writeIndex(bool finished)
{
    if (m_dir.isEmpty())
        return false;
    m_finished = finished;
    QJsonArray ts;
    for (qint64 t : m_timestamps)
        ts.append(static_cast<double>(t));
    QJsonArray events;
    for (const InputEvent& e : m_events)
        events.append(e.toJson());
    const QJsonObject o{{QStringLiteral("region"), QJsonArray{m_region.x(), m_region.y(), m_region.width(), m_region.height()}}, {QStringLiteral("fps"), m_fps},
                        {QStringLiteral("quality"), qualityName(m_quality)}, {QStringLiteral("ext"), m_frameExt}, {QStringLiteral("size"), QJsonArray{m_frameSize.width(), m_frameSize.height()}},
                        {QStringLiteral("timestamps"), ts}, {QStringLiteral("events"), events}, {QStringLiteral("audioRate"), m_audioRate}, {QStringLiteral("finished"), finished}};
    QFile f(QDir(m_dir).filePath(QStringLiteral("recording.json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return true;
}

void FrameStore::remove()
{
    if (!m_dir.isEmpty())
        QDir(m_dir).removeRecursively();
    m_dir.clear();
    m_timestamps.clear();
}

bool FrameStore::hasUnfinished(const QString& dir)
{
    FrameStore s;
    return s.open(dir) && !s.finished() && s.frameCount() > 0;
}

} // namespace ss
