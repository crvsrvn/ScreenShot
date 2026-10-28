# 零网络检查（NFR-10 / TST-09 的构建期部分）
# 对指定目标的产物运行 dumpbin /dependents，若导入了任何网络库则构建失败。

set(SCREENSHOT_FORBIDDEN_IMPORTS
    Qt6Network.dll Qt6Networkd.dll Qt6Qml.dll Qt6Quick.dll
    ws2_32.dll wininet.dll winhttp.dll urlmon.dll
)

function(screenshot_check_zero_network target)
    if(NOT MSVC)
        return()
    endif()
    # dumpbin 与 cl 在同一目录
    get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_dumpbin "${_cl_dir}/dumpbin.exe")
    if(NOT EXISTS "${_dumpbin}")
        message(WARNING "找不到 dumpbin，跳过 ${target} 的零网络检查")
        return()
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DDUMPBIN=${_dumpbin}
            -DBINARY=$<TARGET_FILE:${target}>
            "-DFORBIDDEN=${SCREENSHOT_FORBIDDEN_IMPORTS}"
            -P ${SCREENSHOT_ROOT}/cmake/ZeroNetworkCheck.cmake
        COMMENT "零网络检查：${target}"
        VERBATIM)
endfunction()
