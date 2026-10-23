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

# ---- 第三方：ZXing-C++（二维码 / 条码，REC-06；纯本地库）----
include(FetchContent)
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS ON CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zxing
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    GIT_TAG v2.3.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zxing)

# ---- 第三方：QuickJS-ng（脚本，ACT-13；不编译标准库模块，没有文件与网络接口）----
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_WITH_MIMALLOC OFF CACHE BOOL "" FORCE)
set(QJS_DISABLE_PARSER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG v0.9.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(quickjs)

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
