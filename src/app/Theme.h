#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

namespace ss {

class Settings;

// 主题（CFG-05）：浅色 / 深色 / 跟随系统；主题色；界面字体
namespace theme {

// 解析出实际主题："light" 或 "dark"
QString resolve(const Settings& settings);
QPalette palette(const QString& resolved);
// 应用到整个程序（样式、调色板、字体）
void apply(const Settings& settings);
// 主题色：贴图阴影（激活）、选区边框等
QColor accent(const Settings& settings);

} // namespace theme

} // namespace ss
