# platform_macos：macOS 平台层。只在 macOS 上构建；目前没有 Mac 设备，代码未经编译验证。
# Objective-C++ 只出现在这里的 .mm 文件中，对外只暴露纯 C++ 接口。
enable_language(OBJCXX)
qt_add_library(platform_macos STATIC
    MacScreens.h MacScreens.mm
    MacScreenCapture.h MacScreenCapture.mm
    MacWindowDetector.h MacWindowDetector.mm
    MacElementDetector.h MacElementDetector.mm
    MacGlobalHotkey.h MacGlobalHotkey.mm
    MacOcr.h MacOcr.mm
    MacPermissions.h MacPermissions.mm
    MacWindowTools.h MacWindowTools.mm
    MacPlatform.h MacPlatform.mm
)
target_link_libraries(platform_macos
    PUBLIC screenshot_core
    PRIVATE
        "-framework AppKit"
        "-framework ScreenCaptureKit"
        "-framework CoreGraphics"
        "-framework Carbon"
        "-framework Vision"
        "-framework ApplicationServices")
set_target_properties(platform_macos PROPERTIES OBJCXX_STANDARD 20)
