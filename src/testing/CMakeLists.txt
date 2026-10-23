# screenshot_testing：测试替身与图像比对工具，供测试构建、单元测试和测试驱动使用
qt_add_library(screenshot_testing STATIC
    FakeScreenCapture.h FakeScreenCapture.cpp
    FakeWindowDetector.h
    FakeElementDetector.h
    FakeHotkey.h
    FakeOcr.h
    FakeInputSimulator.h
    FakePlatform.h FakePlatform.cpp
    ImageCompare.h ImageCompare.cpp
)
target_link_libraries(screenshot_testing PUBLIC screenshot_core)
