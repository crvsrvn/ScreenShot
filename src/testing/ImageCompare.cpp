#include "testing/ImageCompare.h"

#include <QDir>
#include <algorithm>
#include <cstdlib>

namespace ss::testing {

QString CompareResult::summary() const
{
    if (sizeMismatch)
        return QStringLiteral("尺寸不同");
    return QStringLiteral("差异像素 %1，最大通道差 %2，首个差异 (%3, %4)，%5")
        .arg(differingPixels)
        .arg(maxChannelDiff)
        .arg(firstDiff.x())
        .arg(firstDiff.y())
        .arg(passed ? QStringLiteral("通过") : QStringLiteral("失败"));
}

CompareResult compareImages(const QImage& actualIn, const QImage& expectedIn, const CompareOptions& options)
{
    CompareResult r;
    if (actualIn.size() != expectedIn.size()) {
        r.sizeMismatch = true;
        return r;
    }
    const QImage actual = actualIn.convertToFormat(QImage::Format_ARGB32);
    const QImage expected = expectedIn.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < actual.height(); ++y) {
        const auto* a = reinterpret_cast<const QRgb*>(actual.constScanLine(y));
        const auto* e = reinterpret_cast<const QRgb*>(expected.constScanLine(y));
        for (int x = 0; x < actual.width(); ++x) {
            if (a[x] == e[x])
                continue;
            const int d = std::max({std::abs(qRed(a[x]) - qRed(e[x])), std::abs(qGreen(a[x]) - qGreen(e[x])),
                                    std::abs(qBlue(a[x]) - qBlue(e[x])), std::abs(qAlpha(a[x]) - qAlpha(e[x]))});
            r.maxChannelDiff = std::max(r.maxChannelDiff, d);
            if (d > options.channelTolerance) {
                if (r.differingPixels == 0)
                    r.firstDiff = QPoint(x, y);
                ++r.differingPixels;
            }
        }
    }
    r.passed = r.differingPixels <= options.maxDifferingPixels;
    return r;
}

QImage diffImage(const QImage& actualIn, const QImage& expectedIn)
{
    const QImage actual = actualIn.convertToFormat(QImage::Format_ARGB32);
    const QImage expected = expectedIn.convertToFormat(QImage::Format_ARGB32);
    QImage out(actual.size(), QImage::Format_ARGB32);
    for (int y = 0; y < actual.height(); ++y) {
        const auto* a = reinterpret_cast<const QRgb*>(actual.constScanLine(y));
        auto* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < actual.width(); ++x) {
            const bool same = y < expected.height() && x < expected.width()
                && a[x] == reinterpret_cast<const QRgb*>(expected.constScanLine(y))[x];
            if (same) {
                const int g = 128 + qGray(a[x]) / 2;
                o[x] = qRgb(g, g, g);
            } else {
                o[x] = qRgb(255, 0, 0);
            }
        }
    }
    return out;
}

bool saveFailureArtifacts(const QString& dir, const QString& name, const QImage& actual, const QImage& expected)
{
    QDir d(dir);
    if (!d.exists() && !d.mkpath(QStringLiteral(".")))
        return false;
    bool ok = actual.save(d.filePath(name + QStringLiteral(".actual.png")));
    ok = expected.save(d.filePath(name + QStringLiteral(".expected.png"))) && ok;
    ok = diffImage(actual, expected).save(d.filePath(name + QStringLiteral(".diff.png"))) && ok;
    return ok;
}

} // namespace ss::testing
