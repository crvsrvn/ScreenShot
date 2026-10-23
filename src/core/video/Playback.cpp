#include "core/video/Playback.h"

#include <QPainter>
#include <algorithm>

namespace ss {

qint64 TrimRange::clamp(qint64 durationMs) const
{
    return endMs < 0 || endMs > durationMs ? durationMs : endMs;
}

bool TrimRange::contains(qint64 t, qint64 durationMs) const
{
    return t >= startMs && t < clamp(durationMs);
}

int frameIndexAt(const QVector<qint64>& timestamps, qint64 t)
{
    if (timestamps.isEmpty())
        return -1;
    auto it = std::upper_bound(timestamps.begin(), timestamps.end(), t);
    if (it == timestamps.begin())
        return 0;
    return static_cast<int>(std::distance(timestamps.begin(), it)) - 1;
}

QVector<ExportFrame> planExport(const QVector<qint64>& timestamps, qint64 durationMs, const TrimRange& trim, int targetFps)
{
    QVector<ExportFrame> out;
    if (timestamps.isEmpty() || targetFps <= 0)
        return out;
    const qint64 end = trim.clamp(durationMs);
    const qint64 step = 1000 / targetFps;
    for (qint64 t = trim.startMs; t < end; t += step) {
        const int index = frameIndexAt(timestamps, t);
        if (index < 0)
            continue;
        out.append({index, t - trim.startMs});
    }
    return out;
}

ProgressStyle progressStyleFromName(const QString& name)
{
    if (name == QLatin1String("bar"))
        return ProgressStyle::Bar;
    if (name == QLatin1String("text"))
        return ProgressStyle::Text;
    if (name == QLatin1String("countdown"))
        return ProgressStyle::Countdown;
    return ProgressStyle::None;
}

namespace {

QString clock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

void paintProgress(QPainter& p, const QSize& size, qint64 elapsedMs, qint64 totalMs, ProgressStyle style)
{
    if (style == ProgressStyle::None || totalMs <= 0)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const double fraction = std::clamp(static_cast<double>(elapsedMs) / totalMs, 0.0, 1.0);
    if (style == ProgressStyle::Bar) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 120));
        p.drawRect(QRect(0, size.height() - 6, size.width(), 6));
        p.setBrush(QColor(40, 120, 220, 230));
        p.drawRect(QRect(0, size.height() - 6, static_cast<int>(size.width() * fraction), 6));
    } else {
        QFont f = p.font();
        f.setPointSize(12);
        f.setBold(true);
        p.setFont(f);
        const QString text = style == ProgressStyle::Text ? clock(elapsedMs) + QStringLiteral(" / ") + clock(totalMs) : QStringLiteral("-") + clock(totalMs - elapsedMs);
        const QRectF box(size.width() - 110, size.height() - 34, 100, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 160));
        p.drawRoundedRect(box, 6, 6);
        p.setPen(Qt::white);
        p.drawText(box, Qt::AlignCenter, text);
    }
    p.restore();
}

QVector<qint16> mixAudio(const QVector<qint16>& a, int volumeA, const QVector<qint16>& b, int volumeB)
{
    const qsizetype n = std::max(a.size(), b.size());
    QVector<qint16> out(n);
    for (qsizetype i = 0; i < n; ++i) {
        const int va = i < a.size() ? a[i] * volumeA / 100 : 0;
        const int vb = i < b.size() ? b[i] * volumeB / 100 : 0;
        out[i] = static_cast<qint16>(std::clamp(va + vb, -32768, 32767));
    }
    return out;
}

void paintPictureInPicture(QPainter& p, const QSize& size, const QImage& camera, PipCorner corner, int widthPercent, int margin)
{
    if (camera.isNull())
        return;
    const int w = std::max(16, size.width() * std::clamp(widthPercent, 5, 60) / 100);
    const int h = std::max(16, w * camera.height() / std::max(1, camera.width()));
    QRect r(0, 0, w, h);
    switch (corner) {
    case PipCorner::TopLeft: r.moveTopLeft(QPoint(margin, margin)); break;
    case PipCorner::TopRight: r.moveTopRight(QPoint(size.width() - margin, margin)); break;
    case PipCorner::BottomLeft: r.moveBottomLeft(QPoint(margin, size.height() - margin)); break;
    case PipCorner::BottomRight: r.moveBottomRight(QPoint(size.width() - margin, size.height() - margin)); break;
    }
    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setPen(QPen(Qt::white, 2));
    p.setBrush(Qt::NoBrush);
    p.drawImage(r, camera);
    p.drawRect(r);
    p.restore();
}

} // namespace ss
