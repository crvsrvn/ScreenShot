#include "platform/macos/MacScreenCapture.h"

#include "core/util/Clock.h"
#include "platform/macos/MacScreens.h"

#import <AppKit/AppKit.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <dispatch/dispatch.h>

#include <cstring>

namespace ss::mac {

namespace {

// 同步取得可共享内容（显示器与窗口列表）
SCShareableContent* shareableContentSync()
{
    __block SCShareableContent* result = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:NO
                                              onScreenWindowsOnly:YES
                                                completionHandler:^(SCShareableContent* content, NSError* error) {
                                                    if (!error)
                                                        result = content;
                                                    dispatch_semaphore_signal(done);
                                                }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
    return result;
}

QImage cgImageToQImage(CGImageRef image, qreal scale)
{
    const size_t w = CGImageGetWidth(image);
    const size_t h = CGImageGetHeight(image);
    QImage result(static_cast<int>(w), static_cast<int>(h), QImage::Format_ARGB32_Premultiplied);
    result.setDevicePixelRatio(scale);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(result.bits(), w, h, 8, static_cast<size_t>(result.bytesPerLine()), colorSpace,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), image);
    CGContextRelease(ctx);
    CGColorSpaceRelease(colorSpace);
    return result;
}

} // namespace

QList<ScreenInfo> MacScreenCapture::screens()
{
    return enumerateScreens();
}

std::optional<ScreenSnapshot> MacScreenCapture::capture(const ScreenInfo& screen)
{
    SCShareableContent* content = shareableContentSync();
    if (!content)
        return std::nullopt;
    const CGDirectDisplayID wanted = displayIdOf(screen);
    SCDisplay* display = nil;
    for (SCDisplay* d in content.displays) {
        if (d.displayID == wanted) {
            display = d;
            break;
        }
    }
    if (!display)
        return std::nullopt;

    // 排除本进程的窗口（覆盖窗口、贴图、滚动截图边框）
    NSMutableArray<SCWindow*>* own = [NSMutableArray array];
    const pid_t self = [NSProcessInfo processInfo].processIdentifier;
    for (SCWindow* w in content.windows) {
        if (w.owningApplication.processID == self)
            [own addObject:w];
    }
    SCContentFilter* filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:own];
    SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
    config.width = static_cast<size_t>(screen.physicalGeometry.width());
    config.height = static_cast<size_t>(screen.physicalGeometry.height());
    config.showsCursor = NO;
    config.pixelFormat = kCVPixelFormatType_32BGRA;

    __block CGImageRef captured = nullptr;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCScreenshotManager captureImageWithFilter:filter
                                  configuration:config
                              completionHandler:^(CGImageRef image, NSError* error) {
                                  if (!error && image)
                                      captured = CGImageRetain(image);
                                  dispatch_semaphore_signal(done);
                              }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
    if (!captured)
        return std::nullopt;
    QImage img = cgImageToQImage(captured, screen.scale);
    CGImageRelease(captured);
    if (img.size() != screen.physicalGeometry.size())
        return std::nullopt;
    return ScreenSnapshot{screen, std::move(img), monotonicNs()};
}

} // namespace ss::mac
