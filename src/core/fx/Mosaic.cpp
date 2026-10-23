#include "core/fx/Mosaic.h"

#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <vector>

namespace ss::fx {

QImage mosaic(const QImage& source, int blockSize)
{
    QImage src = source.format() == QImage::Format_ARGB32_Premultiplied
        ? source
        : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (src.isNull() || blockSize <= 1)
        return src;

    QImage dst(src.size(), src.format());
    const int w = src.width();
    const int h = src.height();
    const int blockRows = (h + blockSize - 1) / blockSize;

    // 每个任务处理若干整行的块，块内先求和再回填
    std::vector<int> rows(static_cast<size_t>(blockRows));
    for (int i = 0; i < blockRows; ++i)
        rows[static_cast<size_t>(i)] = i;

    auto processBlockRow = [&](int by) {
        const int y0 = by * blockSize;
        const int y1 = std::min(h, y0 + blockSize);
        for (int x0 = 0; x0 < w; x0 += blockSize) {
            const int x1 = std::min(w, x0 + blockSize);
            quint64 sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; ++y) {
                const auto* line = reinterpret_cast<const quint32*>(src.constScanLine(y));
                for (int x = x0; x < x1; ++x) {
                    const quint32 p = line[x];
                    sum[0] += p & 0xFF;
                    sum[1] += (p >> 8) & 0xFF;
                    sum[2] += (p >> 16) & 0xFF;
                    sum[3] += p >> 24;
                }
            }
            const quint64 n = static_cast<quint64>(x1 - x0) * static_cast<quint64>(y1 - y0);
            const quint32 avg = static_cast<quint32>((sum[0] + n / 2) / n)
                | (static_cast<quint32>((sum[1] + n / 2) / n) << 8)
                | (static_cast<quint32>((sum[2] + n / 2) / n) << 16)
                | (static_cast<quint32>((sum[3] + n / 2) / n) << 24);
            for (int y = y0; y < y1; ++y) {
                auto* line = reinterpret_cast<quint32*>(dst.scanLine(y));
                std::fill(line + x0, line + x1, avg);
            }
        }
    };

    if (blockRows < 4)
        std::for_each(rows.begin(), rows.end(), processBlockRow);
    else
        QtConcurrent::blockingMap(rows, processBlockRow);
    return dst;
}

} // namespace ss::fx
