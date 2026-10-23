cmake_minimum_required(VERSION 3.28)

project(ScreenShot VERSION 0.1.0 LANGUAGES CXX)

# ---- 语言与编译选项 ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(SCREENSHOT_TEST_BUILD "编译内置测试接口（发布构建必须关闭）" ON)
option(SCREENSHOT_BUILD_TESTS "编译自动测试与测试驱动" ON)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /MP)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---- Qt --------------------------------------------------------------------
# 零网络原则：这里只允许列出的模块，Qt Network / Qml / Quick 永远不出现。
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Concurrent Test LinguistTools)
qt_standard_project_setup()

include(cmake/ZeroNetwork.cmake)

# ---- 目标 ------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/testing)
add_subdirectory(src/app)

if(SCREENSHOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tools/testdriver)
    add_subdirectory(tests)
endif()
