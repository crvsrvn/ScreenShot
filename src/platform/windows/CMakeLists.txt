# platform_windows：Win32 / COM / WinRT 实现的平台接口
qt_add_library(platform_windows STATIC
    WinScreens.h WinScreens.cpp
    GdiScreenCapture.h GdiScreenCapture.cpp
    DxgiScreenCapture.h DxgiScreenCapture.cpp
    WinWindowDetector.h WinWindowDetector.cpp
    WinElementDetector.h WinElementDetector.cpp
    WinGlobalHotkey.h WinGlobalHotkey.cpp
    WinOcr.h WinOcr.cpp
    WinProcess.h WinProcess.cpp
    WinWindowTools.h WinWindowTools.cpp
    WinPipeServer.h WinPipeServer.cpp
    WinSystem.h WinSystem.cpp
    WinShell.h WinShell.cpp
    WinInputSimulator.h WinInputSimulator.cpp
    WinCursor.h WinCursor.cpp
    WinPlatform.h WinPlatform.cpp
)
target_link_libraries(platform_windows
    PUBLIC screenshot_core
    PRIVATE d3d11 dxgi dwmapi shcore user32 gdi32 ole32 oleaut32 uiautomationcore windowsapp iphlpapi psapi dbghelp advapi32)
# C++/WinRT 头文件体积大，需要 /bigobj；WinRT 异步接口需要协程支持（C++20 已包含）
target_compile_options(platform_windows PRIVATE /bigobj)
