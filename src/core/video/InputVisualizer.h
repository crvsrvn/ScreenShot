#pragma once

#include "core/video/Recording.h"

#include <QImage>
#include <QPainter>

namespace ss {

// 鼠标 / 键盘可视化（VID-06）：把点击涟漪、滚轮箭头、按键标签画到帧上。
// 动画速度（涟漪持续时间）可调；只画出现在当前帧时刻附近的事件
struct InputVisualOptions {
    bool showClicks = true;
    bool showWheel = true;
    bool showKeys = true;
    int clickDurationMs = 500;  // 涟漪动画时长（“鼠标动画速度”）
    int keyDurationMs = 1200;   // 按键标签显示时长
    int radius = 24;
};

void paintInputEvents(QPainter& painter, const QList<InputEvent>& events, qint64 frameTimeMs, const QSize& frameSize, const InputVisualOptions& options);
QImage renderWithInputEvents(const QImage& frame, const QList<InputEvent>& events, qint64 frameTimeMs, const InputVisualOptions& options);

} // namespace ss
