#include "core/fx/Inpaint.h"

#include <QPainter>
#include <QVector>
#include <queue>

namespace ss::fx {

QImage inpaint(const QImage& source, const QPainterPath& mask, int smoothPasses)
{
    QImage img = source.convertToFormat(QImage::Format_ARGB32);
    const QRect region = mask.boundingRect().toAlignedRect().intersected(img.rect());
    if (region.isEmpty())
        return img;
    const int w = img.width(), h = img.height();
    // 未知像素标记
    QImage maskImg(w, h, QImage::Format_Grayscale8);
    maskImg.fill(0);
    {
        QPainter p(&maskImg);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillPath(mask, Qt::white);
    }
    QVector<quint8> unknown(static_cast<size_t>(w) * h, 0);
    std::queue<QPoint> frontier;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (maskImg.constScanLine(y)[x] > 127)
                unknown[static_cast<size_t>(y) * w + x] = 1;
    // 边界起步：与已知像素相邻的未知像素
    auto known = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && unknown[static_cast<size_t>(y) * w + x] == 0; };
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (unknown[static_cast<size_t>(y) * w + x] && (known(x - 1, y) || known(x + 1, y) || known(x, y - 1) || known(x, y + 1)))
                frontier.push(QPoint(x, y));
    // 由外向内逐层填充：每个像素取已知邻居（8 邻域）的平均
    while (!frontier.empty()) {
        const QPoint p = frontier.front();
        frontier.pop();
        const size_t idx = static_cast<size_t>(p.y()) * w + p.x();
        if (!unknown[idx])
            continue;
        int r = 0, g = 0, b = 0, n = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                if (!dx && !dy)
                    continue;
                const int x = p.x() + dx, y = p.y() + dy;
                if (!known(x, y))
                    continue;
                const QRgb c = img.pixel(x, y);
                r += qRed(c);
                g += qGreen(c);
                b += qBlue(c);
                ++n;
            }
        if (n == 0)
            continue;
        img.setPixel(p.x(), p.y(), qRgb(r / n, g / n, b / n));
        unknown[idx] = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = p.x() + dx, y = p.y() + dy;
                if (x >= 0 && y >= 0 && x < w && y < h && unknown[static_cast<size_t>(y) * w + x])
                    frontier.push(QPoint(x, y));
            }
    }
    // 平滑几遍，消除扩散方向留下的纹路（只改区域内像素）
    for (int pass = 0; pass < smoothPasses; ++pass) {
        QImage copy = img;
        for (int y = region.top(); y <= region.bottom(); ++y) {
            for (int x = region.left(); x <= region.right(); ++x) {
                if (maskImg.constScanLine(y)[x] <= 127)
                    continue;
                int r = 0, g = 0, b = 0, n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = x + dx, sy = y + dy;
                        if (sx < 0 || sy < 0 || sx >= w || sy >= h)
                            continue;
                        const QRgb c = copy.pixel(sx, sy);
                        r += qRed(c);
                        g += qGreen(c);
                        b += qBlue(c);
                        ++n;
                    }
                img.setPixel(x, y, qRgb(r / n, g / n, b / n));
            }
        }
    }
    return img;
}

} // namespace ss::fx
