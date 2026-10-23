# 自动测试：单元（core）、组件（无显示器界面）、端到端（真实桌面）、需求覆盖矩阵、零网络检查
set(SS_QT_BIN "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
set(SS_TEST_ENV "QT_QPA_PLATFORM=offscreen" "SS_FAILURE_DIR=${CMAKE_BINARY_DIR}/test-failures")
set(SS_E2E_ENV "SS_FAILURE_DIR=${CMAKE_BINARY_DIR}/test-failures")
set(SS_PATH_MOD "PATH=path_list_prepend:${SS_QT_BIN}")

# 单元 / 组件测试：一个源文件一个可执行程序
function(ss_add_qtest name source layer)
    qt_add_executable(${name} ${source} support/TestSupport.h)
    target_link_libraries(${name} PRIVATE screenshot_core screenshot_testing Qt6::Test Qt6::Widgets)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(${layer} STREQUAL "component")
        target_link_libraries(${name} PRIVATE screenshot_applib)
    endif()
    add_test(NAME ${name} COMMAND ${name} -o ${CMAKE_BINARY_DIR}/test-results/${name}.xml,junitxml -o -,txt)
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "${SS_TEST_ENV}"
        ENVIRONMENT_MODIFICATION "${SS_PATH_MOD}"
        LABELS ${layer} TIMEOUT 300)
endfunction()

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-results)

ss_add_qtest(tst_virtualdesktop unit/tst_virtualdesktop.cpp unit)
ss_add_qtest(tst_blur unit/tst_blur.cpp unit)
ss_add_qtest(tst_mosaic unit/tst_mosaic.cpp unit)
ss_add_qtest(tst_mipmap unit/tst_mipmap.cpp unit)
ss_add_qtest(tst_imagecompare unit/tst_imagecompare.cpp unit)
ss_add_qtest(tst_fakeplatform unit/tst_fakeplatform.cpp unit)
ss_add_qtest(tst_settings unit/tst_settings.cpp unit)
ss_add_qtest(tst_log unit/tst_log.cpp unit)
ss_add_qtest(tst_capturecore unit/tst_capturecore.cpp unit)
ss_add_qtest(tst_annotation unit/tst_annotation.cpp unit)
ss_add_qtest(tst_capturesession component/tst_capturesession.cpp component)
ss_add_qtest(tst_overlay component/tst_overlay.cpp component)
ss_add_qtest(tst_pinwindow component/tst_pinwindow.cpp component)
ss_add_qtest(tst_app component/tst_app.cpp component)
ss_add_qtest(tst_preferences component/tst_preferences.cpp component)
ss_add_qtest(tst_i18n component/tst_i18n.cpp component)
set_property(TEST tst_i18n APPEND PROPERTY ENVIRONMENT "SS_TRANSLATIONS_DIR=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/translations" "SS_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
add_dependencies(tst_i18n screenshot_translations)
if(WIN32)
    ss_add_qtest(tst_winsystem unit/tst_winsystem.cpp unit)
    target_link_libraries(tst_winsystem PRIVATE platform_windows)
elseif(APPLE)
    ss_add_qtest(tst_mac_permissions macos/tst_mac_permissions.cpp unit)
    target_link_libraries(tst_mac_permissions PRIVATE platform_macos)
endif()

# 端到端：测试驱动启动真实程序（需要桌面会话）
add_test(NAME e2e_m0
    COMMAND screenshot_testdriver run-all
        --app $<TARGET_FILE:screenshot_app>
        --report ${CMAKE_BINARY_DIR}/test-results/e2e_m0.json
        --quick)
set_tests_properties(e2e_m0 PROPERTIES
    ENVIRONMENT "${SS_E2E_ENV}" ENVIRONMENT_MODIFICATION "${SS_PATH_MOD}" LABELS e2e TIMEOUT 1800 RUN_SERIAL ON)

# 零网络：构建产物导入表检查（TST-09 构建期）
add_test(NAME zero_network_imports
    COMMAND screenshot_testdriver check-imports --binary $<TARGET_FILE:screenshot_app>)
set_tests_properties(zero_network_imports PROPERTIES ENVIRONMENT_MODIFICATION "${SS_PATH_MOD}" LABELS unit)

# 需求覆盖矩阵（TST-01）
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME coverage_matrix
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/coverage/gen_matrix.py
            --docs ${CMAKE_SOURCE_DIR}/docs/01-requirements.md
            --tests ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/tools/testdriver
            --scope ${CMAKE_SOURCE_DIR}/tests/delivered_scope.txt
            --out ${CMAKE_BINARY_DIR}/test-results/coverage.md)
    set_tests_properties(coverage_matrix PROPERTIES LABELS unit)
endif()
