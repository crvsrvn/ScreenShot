# 由 ZeroNetwork.cmake 在 POST_BUILD 阶段以脚本模式调用
execute_process(
    COMMAND "${DUMPBIN}" /nologo /dependents "${BINARY}"
    OUTPUT_VARIABLE _out
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "dumpbin 失败：${BINARY}")
endif()
string(TOLOWER "${_out}" _out_lower)
foreach(_lib IN LISTS FORBIDDEN)
    string(TOLOWER "${_lib}" _lib_lower)
    string(FIND "${_out_lower}" "${_lib_lower}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR "零网络检查失败：${BINARY} 导入了 ${_lib}")
    endif()
endforeach()
