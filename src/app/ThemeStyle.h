#pragma once

#include "app/Theme.h"

#include <QProxyStyle>

namespace ss::theme {

// 全局样式表：对话框底色、Fluent 按钮 / 输入框 / 下拉框 / 菜单 / 滚动条等，配色见 Colors
QString styleSheet(const Colors& c);

// 在 Fusion 上补画样式表画不了的部分（与 WinUI 3 一致）：复选框、单选框、菜单等处的细箭头；
// 菜单与下拉列表用透明窗口以显示圆角
class ThemeStyle : public QProxyStyle {
    Q_OBJECT
public:
    using QProxyStyle::QProxyStyle;

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr, const QWidget* widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption* option = nullptr, const QWidget* widget = nullptr, QStyleHintReturn* returnData = nullptr) const override;
    void polish(QWidget* widget) override;
    using QProxyStyle::polish;
};

} // namespace ss::theme
