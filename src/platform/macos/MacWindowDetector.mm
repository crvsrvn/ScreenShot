#include "platform/macos/MacWindowDetector.h"

#import <AppKit/AppKit.h>
#include <CoreGraphics/CoreGraphics.h>

namespace ss::mac {

QList<WindowInfo> MacWindowDetector::topLevelWindows()
{
    QList<WindowInfo> list;
    // 按 Z 序从上到下，只取屏幕上的窗口
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID);
    if (!windows)
        return list;
    const pid_t self = [NSProcessInfo processInfo].processIdentifier;
    NSScreen* primary = [NSScreen screens].firstObject;
    const CGFloat primaryScale = primary ? primary.backingScaleFactor : 1.0;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex i = 0; i < count; ++i) {
        NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(windows, i);
        const int layer = [info[(id)kCGWindowLayer] intValue];
        if (layer != 0) // 只要普通窗口层
            continue;
        const pid_t pid = [info[(id)kCGWindowOwnerPID] intValue];
        if (pid == self)
            continue;
        CGRect bounds{};
        if (!CGRectMakeWithDictionaryRepresentation((__bridge CFDictionaryRef)info[(id)kCGWindowBounds], &bounds))
            continue;
        // CGWindowBounds 已是左上角原点的点坐标；按主屏缩放换算为像素（混合缩放时的精确换算在真机验证）
        WindowInfo w;
        w.handle = static_cast<quintptr>([info[(id)kCGWindowNumber] unsignedIntValue]);
        w.title = QString::fromNSString(info[(id)kCGWindowName] ?: @"");
        w.processName = QString::fromNSString(info[(id)kCGWindowOwnerName] ?: @"");
        w.frame = QRect(static_cast<int>(bounds.origin.x * primaryScale), static_cast<int>(bounds.origin.y * primaryScale),
                        static_cast<int>(bounds.size.width * primaryScale), static_cast<int>(bounds.size.height * primaryScale));
        w.zOrder = static_cast<int>(list.size());
        if (!w.frame.isEmpty())
            list.append(w);
    }
    CFRelease(windows);
    return list;
}

} // namespace ss::mac
