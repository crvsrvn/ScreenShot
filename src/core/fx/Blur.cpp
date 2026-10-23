#include "core/fx/Blur.h"

#include <QThreadPool>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <vector>

namespace ss::fx {

namespace {

// 每个线程处理的最小行数，避免小图上线程切换开销反而更大
constexpr int kMinRowsPerTask = 32;

// 把 [0, count) 切成若干段并行执行
template <typename Fn>
void parallelRanges(int count, int minPerTask, Fn fn)
{
    const int threads = std::max(1, QThreadPool::globalInstance()->maxThreadCount());
    const int perTask = std::max(minPerTask, (count + threads - 1) / threads);
    std::vector<std::pair<int, int>> ranges;
    for (int start = 0; start < count; start += perTask)
        ranges.emplace_back(start, std::min(count, start + perTask));
    if (ranges.size() <= 1) {
        if (!ranges.empty())
            fn(ranges[0].first, ranges[0].second);
        return;
    }
    QtConcurrent::blockingMap(ranges, [&](const std::pair<int, int>& r) { fn(r.first, r.second); });
}

// 水平盒式模糊：src → dst，逐行滑动窗口，边缘重复
void boxBlurH(const QImage& src, QImage& dst, int r)
{
    const int w = src.width();
    const int h = src.height();
    const int win = 2 * r + 1;
    parallelRanges(h, kMinRowsPerTask, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const auto* in = reinterpret_cast<const quint32*>(src.constScanLine(y));
            auto* out = reinterpret_cast<quint32*>(dst.scanLine(y));
            int sum[4] = {0, 0, 0, 0};
            auto add = [&](int x, int sign) {
                const quint32 p = in[std::clamp(x, 0, w - 1)];
                sum[0] += sign * static_cast<int>(p & 0xFF);
                sum[1] += sign * static_cast<int>((p >> 8) & 0xFF);
                sum[2] += sign * static_cast<int>((p >> 16) & 0xFF);
                sum[3] += sign * static_cast<int>(p >> 24);
            };
            for (int x = -r; x <= r; ++x)
                add(x, +1);
            for (int x = 0; x < w; ++x) {
                const quint32 c0 = static_cast<quint32>((sum[0] + win / 2) / win);
                const quint32 c1 = static_cast<quint32>((sum[1] + win / 2) / win);
                const quint32 c2 = static_cast<quint32>((sum[2] + win / 2) / win);
                const quint32 c3 = static_cast<quint32>((sum[3] + win / 2) / win);
                out[x] = c0 | (c1 << 8) | (c2 << 16) | (c3 << 24);
                add(x + r + 1, +1);
                add(x - r, -1);
            }
        }
    });
}

// 垂直盒式模糊：按列带并行，每个带内沿行方向滑动窗口，保持按行访问内存
void boxBlurV(const QImage& src, QImage& dst, int r)
{
    const int w = src.width();
    const int h = src.height();
    const int win = 2 * r + 1;
    constexpr int kMinColsPerTask = 64;
    parallelRanges(w, kMinColsPerTask, [&](int x0, int x1) {
        const int cols = x1 - x0;
        std::vector<int> sum(static_cast<size_t>(cols) * 4, 0);
        auto addRow = [&](int y, int sign) {
            const auto* in = reinterpret_cast<const quint32*>(src.constScanLine(std::clamp(y, 0, h - 1))) + x0;
            for (int i = 0; i < cols; ++i) {
                const quint32 p = in[i];
                int* s = &sum[static_cast<size_t>(i) * 4];
                s[0] += sign * static_cast<int>(p & 0xFF);
                s[1] += sign * static_cast<int>((p >> 8) & 0xFF);
                s[2] += sign * static_cast<int>((p >> 16) & 0xFF);
                s[3] += sign * static_cast<int>(p >> 24);
            }
        };
        for (int y = -r; y <= r; ++y)
            addRow(y, +1);
        for (int y = 0; y < h; ++y) {
            auto* out = reinterpret_cast<quint32*>(dst.scanLine(y)) + x0;
            for (int i = 0; i < cols; ++i) {
                const int* s = &sum[static_cast<size_t>(i) * 4];
                const quint32 c0 = static_cast<quint32>((s[0] + win / 2) / win);
                const quint32 c1 = static_cast<quint32>((s[1] + win / 2) / win);
                const quint32 c2 = static_cast<quint32>((s[2] + win / 2) / win);
                const quint32 c3 = static_cast<quint32>((s[3] + win / 2) / win);
                out[i] = c0 | (c1 << 8) | (c2 << 16) | (c3 << 24);
            }
            addRow(y + r + 1, +1);
            addRow(y - r, -1);
        }
    });
}

QImage toPremultiplied(const QImage& source)
{
    return source.format() == QImage::Format_ARGB32_Premultiplied
        ? source
        : source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

void boxSizesForGaussian(double sigma, int sizes[3])
{
    // 参考 Ivan Kutskir, "Fastest Gaussian Blur"
    const int n = 3;
    const double wIdeal = std::sqrt(12.0 * sigma * sigma / n + 1.0);
    int wl = static_cast<int>(std::floor(wIdeal));
    if (wl % 2 == 0)
        --wl;
    const int wu = wl + 2;
    const double mIdeal = (12.0 * sigma * sigma - n * wl * wl - 4.0 * n * wl - 3.0 * n) / (-4.0 * wl - 4.0);
    const int m = static_cast<int>(std::round(mIdeal));
    for (int i = 0; i < n; ++i)
        sizes[i] = i < m ? wl : wu;
}

QImage boxBlur(const QImage& source, int radius)
{
    QImage src = toPremultiplied(source);
    if (src.isNull() || radius <= 0)
        return src;
    QImage tmp(src.size(), src.format());
    QImage dst(src.size(), src.format());
    boxBlurH(src, tmp, radius);
    boxBlurV(tmp, dst, radius);
    return dst;
}

QImage gaussianBlur(const QImage& source, int radius, int downscaleThreshold)
{
    QImage src = toPremultiplied(source);
    if (src.isNull() || radius <= 0)
        return src;

    // 半径很大时先缩小：缩小 k 倍后半径也除以 k，结果放大回原尺寸
    int k = 1;
    while (radius / k > downscaleThreshold && src.width() / (k * 2) >= 8 && src.height() / (k * 2) >= 8)
        k *= 2;
    if (k > 1) {
        const QSize small = src.size() / k;
        QImage reduced = src.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QImage blurred = gaussianBlur(reduced, std::max(1, radius / k), downscaleThreshold);
        return blurred.scaled(src.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    int sizes[3];
    boxSizesForGaussian(static_cast<double>(radius) / 2.0, sizes);
    QImage a = src;
    QImage b(src.size(), src.format());
    QImage tmp(src.size(), src.format());
    for (int s : sizes) {
        const int r = std::max(0, (s - 1) / 2);
        if (r == 0)
            continue;
        boxBlurH(a, tmp, r);
        boxBlurV(tmp, b, r);
        std::swap(a, b);
    }
    return a;
}

QImage referenceGaussianBlur(const QImage& source, double sigma)
{
    QImage src = toPremultiplied(source);
    const int w = src.width();
    const int h = src.height();
    const int r = static_cast<int>(std::ceil(sigma * 3));
    std::vector<double> kernel(static_cast<size_t>(2 * r + 1));
    double total = 0;
    for (int i = -r; i <= r; ++i) {
        kernel[static_cast<size_t>(i + r)] = std::exp(-(i * i) / (2 * sigma * sigma));
        total += kernel[static_cast<size_t>(i + r)];
    }
    for (auto& k : kernel)
        k /= total;

    auto pass = [&](const QImage& in, bool horizontal) {
        QImage out(in.size(), in.format());
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double acc[4] = {0, 0, 0, 0};
                for (int i = -r; i <= r; ++i) {
                    const int sx = horizontal ? std::clamp(x + i, 0, w - 1) : x;
                    const int sy = horizontal ? y : std::clamp(y + i, 0, h - 1);
                    const quint32 p = reinterpret_cast<const quint32*>(in.constScanLine(sy))[sx];
                    const double k = kernel[static_cast<size_t>(i + r)];
                    acc[0] += k * (p & 0xFF);
                    acc[1] += k * ((p >> 8) & 0xFF);
                    acc[2] += k * ((p >> 16) & 0xFF);
                    acc[3] += k * (p >> 24);
                }
                quint32 c[4];
                for (int i = 0; i < 4; ++i)
                    c[i] = static_cast<quint32>(std::clamp(std::lround(acc[i]), 0L, 255L));
                reinterpret_cast<quint32*>(out.scanLine(y))[x] = c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
            }
        }
        return out;
    };
    return pass(pass(src, true), false);
}

} // namespace ss::fx
