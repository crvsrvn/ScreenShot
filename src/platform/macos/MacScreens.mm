#include "platform/macos/MacScreens.h"

#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>

namespace ss::mac {

QList<ScreenInfo> enumerateScreens()
{
    QList<ScreenInfo> list;
    NSScreen* primary = [NSScreen screens].firstObject; // 主屏：菜单栏所在屏幕
    if (!primary)
        return list;
    const CGFloat primaryHeightPt = primary.frame.size.height;
    const CGFloat primaryScale = primary.backingScaleFactor;
    for (NSScreen* screen in [NSScreen screens]) {
        NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
        const CGDirectDisplayID displayId = static_cast<CGDirectDisplayID>(number.unsignedIntValue);
        const NSRect frame = screen.frame; // 点坐标，左下角原点
        const CGFloat scale = screen.backingScaleFactor;
        // 物理像素由各屏自己的缩放比例决定；位置按主屏缩放换算到统一的像素坐标系
        ScreenInfo s;
        s.id = QString::number(displayId);
        const int x = static_cast<int>(frame.origin.x * primaryScale);
        const int yTop = static_cast<int>((primaryHeightPt - (frame.origin.y + frame.size.height)) * primaryScale);
        s.physicalGeometry = QRect(x, yTop, static_cast<int>(frame.size.width * scale), static_cast<int>(frame.size.height * scale));
        s.scale = scale;
        s.primary = (screen == primary);
        s.hdr = screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1.0;
        list.append(s);
    }
    return list;
}

uint32_t displayIdOf(const ScreenInfo& screen)
{
    return screen.id.toUInt();
}

} // namespace ss::mac
