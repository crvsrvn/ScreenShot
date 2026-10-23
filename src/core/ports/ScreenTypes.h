#pragma once

#include <QImage>
#include <QRect>
#include <QString>

namespace ss {

// 一台显示器。坐标全部是虚拟桌面上的物理像素（NFR-07），可以为负。
struct ScreenInfo {
    QString id;              // 平台提供的稳定标识，Windows 为 \\.\DISPLAYn
    QRect physicalGeometry;  // 物理像素矩形
    qreal scale = 1.0;       // 系统缩放比例，1.5 表示 150%
    bool primary = false;
    bool hdr = false;

    bool operator==(const ScreenInfo&) const = default;
};

// 一台显示器的一次冻结画面。image 创建后只读，尺寸等于 physicalGeometry.size()。
// 约定：image.devicePixelRatio() == screen.scale，由捕获实现在创建图像时设置（避免之后再设置引发拷贝），
// 这样覆盖窗口直接 drawImage 即可 1:1 显示。
struct ScreenSnapshot {
    ScreenInfo screen;
    QImage image;            // Format_RGB32 或 Format_ARGB32，物理像素
    qint64 capturedAtNs = 0; // 单调时钟，见 util/Clock.h
};

// 鼠标指针图像（带透明通道）及其左上角的物理位置
struct CursorImage {
    QImage image;
    QPoint physicalTopLeft;
};

} // namespace ss
