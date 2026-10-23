#include "platform/macos/MacPermissions.h"

#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <CoreGraphics/CoreGraphics.h>

namespace ss::mac {

bool hasScreenRecordingPermission()
{
    return CGPreflightScreenCaptureAccess();
}

bool requestScreenRecordingPermission()
{
    return CGRequestScreenCaptureAccess();
}

bool hasAccessibilityPermission()
{
    return AXIsProcessTrusted();
}

bool requestAccessibilityPermission(bool prompt)
{
    NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt : @(prompt)};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void openScreenRecordingSettings()
{
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}

void openAccessibilitySettings()
{
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"]];
}

} // namespace ss::mac
