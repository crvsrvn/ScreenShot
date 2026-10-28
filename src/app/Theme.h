#pragma once

#include <QColor>
#include <QLinearGradient>
#include <QPalette>
#include <QString>

class QWidget;

namespace ss {

class Settings;

// 主题（CFG-05）：浅色 / 深色 / 跟随系统；主题色；界面字体
namespace theme {

// 界面配色：取自 AITranslator（App.xaml 的 App*Brush）与 WinUI 3 的 Fluent 中性色，两边保持一致
struct Colors {
    bool dark = false;
    QColor background[3];               // AppBackgroundBrush：左上 → 右下三段渐变（0 / 0.56 / 1）
    QColor surface[2], subtle[2];        // AppSurfaceBrush（面板、卡片）、AppSubtleBrush（状态栏、悬停）
    QColor border, text, muted;          // AppBorderBrush、AppTextBrush、AppMutedTextBrush
    QColor accent[2], accentHover[2], accentPressed[2]; // 金色主按钮
    QColor onAccent, accentText, danger;
    QColor systemAccent;                 // 系统强调色（导航选中条、复选框、输入框焦点线，与 WinUI 一致）
    // Fluent 中性色（带透明度，叠在底色上）
    QColor controlText, controlTextSecondary, disabledText;
    QColor controlFill, controlFillHover, controlFillPressed, controlFillDisabled, inputActive;
    QColor controlStroke, controlStrokeBottom, strongStroke, strongStrokeDisabled;
    QColor subtleHover, subtlePressed, altFill;
    QColor layer, layerStroke;           // 内容区底板（半透明白）及描边
    QColor flyout, flyoutStroke;         // 菜单、下拉列表
    QColor accentDisabled;
    QColor closeHover;                   // 标题栏关闭按钮悬停
};

// 解析出实际主题："light" 或 "dark"
QString resolve(const Settings& settings);
QPalette palette(const QString& resolved);
// 当前主题的配色（apply 之后有效；之前为浅色）
const Colors& colors();
// 对角渐变：stops 为 2 段或 3 段（中间段在 0.56）
QLinearGradient gradient(const QRectF& rect, const QColor* stops, int count);
// 应用到整个程序（样式、调色板、字体、全局样式表）
void apply(const Settings& settings);
// 强调蓝：激活贴图的阴影、未设颜色时的选区边框
QColor accent();

// 浮动面板（截图工具栏、悬浮条、通知等无边框小窗）：AppSurfaceBrush 圆角底板 + 金色描边，
// 内部按钮为 Fluent 按钮；须在窗口显示前调用
void makeSurface(QWidget* window);

} // namespace theme

} // namespace ss
