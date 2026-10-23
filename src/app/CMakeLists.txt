# screenshot_applib：程序逻辑（供组件测试链接）；screenshot_app：可执行程序
set(APPLIB_SOURCES
    App.h App.cpp
    CaptureSession.h CaptureSession.cpp
    OverlayWindow.h OverlayWindow.cpp
    PinWindow.h PinWindow.cpp
)
if(SCREENSHOT_TEST_BUILD)
    list(APPEND APPLIB_SOURCES testapi/TestServer.h testapi/TestServer.cpp testapi/TestCommands.h testapi/TestCommands.cpp)
endif()

qt_add_library(screenshot_applib STATIC ${APPLIB_SOURCES})
target_link_libraries(screenshot_applib PUBLIC screenshot_core Qt6::Widgets)
if(WIN32)
    target_link_libraries(screenshot_applib PUBLIC platform_windows)
elseif(APPLE)
    target_link_libraries(screenshot_applib PUBLIC platform_macos)
endif()
if(SCREENSHOT_TEST_BUILD)
    target_compile_definitions(screenshot_applib PUBLIC SCREENSHOT_TEST_BUILD=1)
    target_link_libraries(screenshot_applib PUBLIC screenshot_testing)
endif()

qt_add_executable(screenshot_app WIN32 main.cpp)
target_link_libraries(screenshot_app PRIVATE screenshot_applib)

screenshot_check_zero_network(screenshot_app)
