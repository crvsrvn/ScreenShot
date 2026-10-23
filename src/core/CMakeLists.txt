# screenshot_core：纯 C++ 核心库，只依赖 Qt Core / Gui / Concurrent，不调用任何系统接口
qt_add_library(screenshot_core STATIC
    ports/ScreenTypes.h
    ports/IScreenCapture.h
    ports/IWindowDetector.h
    ports/IElementDetector.h
    ports/IGlobalHotkey.h
    ports/IOcrEngine.h
    ports/Platform.h
    geometry/VirtualDesktop.h geometry/VirtualDesktop.cpp
    fx/Blur.h fx/Blur.cpp
    fx/Mosaic.h fx/Mosaic.cpp
    util/Clock.h util/Clock.cpp
    util/Mipmap.h util/Mipmap.cpp
    util/Log.h util/Log.cpp
    config/Settings.h config/Settings.cpp
    color/ColorFormat.h color/ColorFormat.cpp
    output/FileNaming.h output/FileNaming.cpp
    capture/CaptureHistory.h capture/CaptureHistory.cpp
    capture/SelectionGeometry.h capture/SelectionGeometry.cpp
    capture/Magnifier.h capture/Magnifier.cpp
    capture/WindowFinder.h capture/WindowFinder.cpp
    annotation/Annotation.h annotation/Annotation.cpp
    annotation/Shapes.h annotation/Shapes.cpp
    annotation/AnnotationDocument.h annotation/AnnotationDocument.cpp
    pin/TextPin.h pin/TextPin.cpp
)
target_include_directories(screenshot_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(screenshot_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)
