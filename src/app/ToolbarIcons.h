#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace ss {

// 工具栏图标（ANN-14 / CAP-17）：按名称用 QPainter 画成矢量风格的单色图标，无外部资源；
// 名称与 CaptureToolbar 的按钮 objectName 一致（rect、ellipse、…、copy、save、cancel）。未知名称返回空图标。
QIcon toolbarIcon(const QString& name, const QColor& color = Qt::white);
bool hasToolbarIcon(const QString& name);

} // namespace ss
