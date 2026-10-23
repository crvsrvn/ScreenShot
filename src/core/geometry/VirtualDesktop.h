#pragma once

#include "core/ports/ScreenTypes.h"

#include <QList>
#include <QPointF>
#include <QRect>
#include <optional>

namespace ss {

// 虚拟桌面：多台显示器冻结画面的集合，负责物理像素坐标下的查找与裁剪（CAP-02、NFR-07）。
// 不做任何缩放：输出像素与捕获像素完全一致（NFR-08）。
class VirtualDesktop {
public:
    VirtualDesktop() = default;
    explicit VirtualDesktop(QList<ScreenSnapshot> snapshots);

    const QList<ScreenSnapshot>& snapshots() const { return m_snapshots; }
    bool isEmpty() const { return m_snapshots.isEmpty(); }

    // 全部显示器的并集。
    QRect bounds() const;

    // 包含该物理点的显示器；不在任何显示器上返回空。
    std::optional<ScreenInfo> screenAt(QPoint physical) const;

    // 按物理矩形裁出图像。跨屏时逐屏拷贝；不属于任何显示器的部分填黑。
    // 结果为 Format_ARGB32，尺寸等于 region.size()。
    QImage crop(const QRect& region) const;

    // 物理点处的像素；不在任何显示器上返回空。
    std::optional<QRgb> pixelAt(QPoint physical) const;

    // 逻辑坐标 ↔ 物理坐标（给定所在显示器及其逻辑原点）。
    static QPoint logicalToPhysical(QPointF logical, const ScreenInfo& screen, QPointF screenLogicalOrigin);
    static QPointF physicalToLogical(QPoint physical, const ScreenInfo& screen, QPointF screenLogicalOrigin);

private:
    QList<ScreenSnapshot> m_snapshots;
};

} // namespace ss
