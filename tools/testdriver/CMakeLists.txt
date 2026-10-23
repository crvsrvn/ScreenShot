# screenshot_testdriver：端到端测试驱动（开发工具，不随产品发布）
qt_add_executable(screenshot_testdriver
    main.cpp
    PipeClient.h PipeClient.cpp
    Scenarios.h Scenarios.cpp
)
target_link_libraries(screenshot_testdriver PRIVATE screenshot_core screenshot_testing Qt6::Gui)
if(WIN32)
    target_link_libraries(screenshot_testdriver PRIVATE platform_windows)
endif()
