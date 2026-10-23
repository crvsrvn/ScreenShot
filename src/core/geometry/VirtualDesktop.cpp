#include "core/geometry/VirtualDesktop.h"

#include <algorithm>
#include <cmath>

namespace ss {

VirtualDesktop::VirtualDesktop(QList<ScreenSnapshot> snapshots)
    : m_snapshots(std::move(snapshots))
{
}

QRect VirtualDesktop::bounds() const
{
    QRect r;
    for (const auto& s : m_snapshots)
        r = r.united(s.screen.physicalGeometry);
    return r;
}

std::optional<ScreenInfo> VirtualDesktop::screenAt(QPoint physical) const
{
    for (const auto& s : m_snapshots) {
        if (s.screen.physicalGeometry.contains(physical))
            return s.screen;
    }
    return std::nullopt;
}

QImage VirtualDesktop::crop(const QRect& region) const
{
    QImage result(region.size(), QImage::Format_ARGB32);
    if (result.isNull())
        return result;
    result.fill(Qt::black);
    result.setDevicePixelRatio(1.0); // 输出图像使用物理像素（NFR-07）

    for (const auto& s : m_snapshots) {
        const QRect overlap = region.intersected(s.screen.physicalGeometry);
        if (overlap.isEmpty())
            continue;
        // 源：显示器图像内的局部矩形；目标：结果图像内的偏移
        const QRect src = overlap.translated(-s.screen.physicalGeometry.topLeft());
        const QPoint dst = overlap.topLeft() - region.topLeft();
        const QImage piece = s.image.copy(src).convertToFormat(QImage::Format_ARGB32);
        // 逐行拷贝，避免 QPainter 的合成和潜在的格式转换
        for (int y = 0; y < piece.height(); ++y) {
            const auto* srcLine = reinterpret_cast<const QRgb*>(piece.constScanLine(y));
            auto* dstLine = reinterpret_cast<QRgb*>(result.scanLine(dst.y() + y)) + dst.x();
            std::copy(srcLine, srcLine + piece.width(), dstLine);
        }
    }
    return result;
}

std::optional<QRgb> VirtualDesktop::pixelAt(QPoint physical) const
{
    for (const auto& s : m_snapshots) {
        if (s.screen.physicalGeometry.contains(physical))
            return s.image.pixel(physical - s.screen.physicalGeometry.topLeft());
    }
    return std::nullopt;
}

QPoint VirtualDesktop::logicalToPhysical(QPointF logical, const ScreenInfo& screen, QPointF screenLogicalOrigin)
{
    const QPointF rel = (logical - screenLogicalOrigin) * screen.scale;
    return screen.physicalGeometry.topLeft()
        + QPoint(static_cast<int>(std::floor(rel.x())), static_cast<int>(std::floor(rel.y())));
}

QPointF VirtualDesktop::physicalToLogical(QPoint physical, const ScreenInfo& screen, QPointF screenLogicalOrigin)
{
    const QPoint rel = physical - screen.physicalGeometry.topLeft();
    return screenLogicalOrigin + QPointF(rel) / screen.scale;
}

} // namespace ss
