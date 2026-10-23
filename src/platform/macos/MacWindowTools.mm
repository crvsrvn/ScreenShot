#include "platform/macos/MacWindowTools.h"

#import <AppKit/AppKit.h>

namespace ss::mac {

namespace {

NSWindow* windowOf(void* nsView)
{
    NSView* view = (__bridge NSView*)nsView;
    return view.window;
}

} // namespace

void makeFloatingOnAllSpaces(void* nsView, bool floating)
{
    NSWindow* window = windowOf(nsView);
    if (!window)
        return;
    window.level = floating ? NSFloatingWindowLevel : NSNormalWindowLevel;
    NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorStationary;
    if (floating)
        behavior |= NSWindowCollectionBehaviorCanJoinAllSpaces;
    window.collectionBehavior = behavior;
}

void setWindowPhysicalRect(void* nsView, const QRect& physical)
{
    NSWindow* window = windowOf(nsView);
    NSScreen* primary = [NSScreen screens].firstObject;
    if (!window || !primary)
        return;
    const CGFloat scale = primary.backingScaleFactor;
    const CGFloat primaryHeightPt = primary.frame.size.height;
    // 物理像素（左上角原点）→ 点（左下角原点）
    const NSRect frame = NSMakeRect(physical.x() / scale, primaryHeightPt - (physical.y() + physical.height()) / scale,
                                    physical.width() / scale, physical.height() / scale);
    [window setFrame:frame display:YES];
}

QPoint cursorPhysicalPos()
{
    NSScreen* primary = [NSScreen screens].firstObject;
    if (!primary)
        return {};
    const NSPoint p = [NSEvent mouseLocation];
    const CGFloat scale = primary.backingScaleFactor;
    return QPoint(static_cast<int>(p.x * scale), static_cast<int>((primary.frame.size.height - p.y) * scale));
}

} // namespace ss::mac
