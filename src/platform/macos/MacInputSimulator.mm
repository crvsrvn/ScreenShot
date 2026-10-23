#include "platform/macos/MacInputSimulator.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>

namespace ss::mac {

void MacInputSimulator::mouseWheelAt(QPoint physical, int notches, bool horizontal)
{
    NSScreen* primary = [NSScreen screens].firstObject;
    const CGFloat scale = primary ? primary.backingScaleFactor : 1.0;
    const CGPoint pt = CGPointMake(physical.x() / scale, physical.y() / scale); // CG 事件用左上角原点的点坐标
    CGEventRef move = CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, pt, kCGMouseButtonLeft);
    CGEventPost(kCGHIDEventTap, move);
    CFRelease(move);
    CGEventRef wheel = CGEventCreateScrollWheelEvent(nullptr, kCGScrollEventUnitLine, 2, horizontal ? 0 : notches * 3, horizontal ? notches * 3 : 0);
    CGEventPost(kCGHIDEventTap, wheel);
    CFRelease(wheel);
}

} // namespace ss::mac
