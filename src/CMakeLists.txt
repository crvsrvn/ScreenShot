cmake_minimum_required(VERSION 3.28)

project(ScreenShot VERSION 0.1.0 LANGUAGES CXX)

# ---- 语言与编译选项 ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 本文件在 src/ 下，仓库根目录（cmake/、tests/、tools/、translations/ 所在）是它的上一级
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH SCREENSHOT_ROOT)

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

include(${SCREENSHOT_ROOT}/cmake/ZeroNetwork.cmake)

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

# ---- 第三方：libwebp（动画 WebP 导出，VID-03；只编译编码库与 mux，不编译工具与解码扩展）----
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD ON CACHE BOOL "" FORCE)
FetchContent_Declare(libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG v1.6.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(libwebp)
foreach(t webp webpdecoder sharpyuv libwebpmux webpdemux)
    if(TARGET ${t})
        target_compile_options(${t} PRIVATE /w) # 第三方代码的转换警告不参与本工程的告警检查
    endif()
endforeach()

# ---- 目标 ------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(platform)
add_subdirectory(testing)
add_subdirectory(app)

if(SCREENSHOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(${SCREENSHOT_ROOT}/tools/testdriver tools/testdriver)
    add_subdirectory(${SCREENSHOT_ROOT}/tests tests)
endif()
