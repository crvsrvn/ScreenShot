# screenshot_applib：程序逻辑（供组件测试链接）；screenshot_app：可执行程序
set(APPLIB_SOURCES
    App.h App.cpp
    CaptureSession.h CaptureSession.cpp
    OverlayWindow.h OverlayWindow.cpp
    CaptureToolbar.h CaptureToolbar.cpp
    AnnotationEditor.h AnnotationEditor.cpp
    ToolbarTarget.h
    PinWindow.h PinWindow.cpp PinWindowContent.cpp
    Theme.h Theme.cpp
    ResultDialogs.h ResultDialogs.cpp
    ActionsDialog.h ActionsDialog.cpp
    PinManager.h PinManager.cpp
    ScrollCaptureSession.h ScrollCaptureSession.cpp
    CustomCaptureDialog.h CustomCaptureDialog.cpp
    OutputActions.h OutputActions.cpp
    PreferencesDialog.h PreferencesDialog.cpp
    CommandServer.h CommandServer.cpp
)
if(SCREENSHOT_TEST_BUILD)
    list(APPEND APPLIB_SOURCES testapi/TestServer.h testapi/TestServer.cpp testapi/TestCommands.h testapi/TestCommands.cpp)
endif()

qt_add_library(screenshot_applib STATIC ${APPLIB_SOURCES})
target_link_libraries(screenshot_applib PUBLIC screenshot_core Qt6::Widgets Qt6::PrintSupport)
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

# 翻译（GEN-13）：.ts 在仓库里，.qm 构建时生成到 bin/translations
qt_add_translations(screenshot_app
    TS_FILES ${CMAKE_SOURCE_DIR}/translations/screenshot_zh_CN.ts ${CMAKE_SOURCE_DIR}/translations/screenshot_en.ts
    SOURCES ${APPLIB_SOURCES} main.cpp ${CMAKE_SOURCE_DIR}/src/core/config/Settings.cpp ${CMAKE_SOURCE_DIR}/src/core/capture/Magnifier.cpp
    QM_FILES_OUTPUT_VARIABLE SS_QM_FILES
    LUPDATE_OPTIONS -no-obsolete)
add_custom_target(screenshot_translations ALL DEPENDS ${SS_QM_FILES}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/translations
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SS_QM_FILES} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/translations
    COMMENT "copy translations"
    VERBATIM)
add_dependencies(screenshot_app screenshot_translations)

screenshot_check_zero_network(screenshot_app)
