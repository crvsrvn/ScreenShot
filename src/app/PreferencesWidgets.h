#pragma once

#include <QCheckBox>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QWheelEvent>

namespace ss::prefs {

// 偏好设置窗口的局部样式（侧边栏、内容面板、标题、行文字）；控件本身用全局主题
QString styleSheet();

// 侧边栏条目（WinUI NavigationView 左侧模式）：36px 高、圆角底色，选中项左侧画系统强调色短竖条
class NavDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

// 开关（WinUI ToggleSwitch）：替代复选框，滑块带过渡动画；仍是 QCheckBox，选中状态与信号不变
class SwitchButton : public QCheckBox {
public:
    explicit SwitchButton(QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    bool hitButton(const QPoint& pos) const override;
    void paintEvent(QPaintEvent* event) override;

private:
    QVariantAnimation m_anim; // 0 = 关，1 = 开
};

// 未获得焦点时滚轮交给页面滚动，避免滚动长页面时误改数值（用于数字框、下拉框）
template <class Base>
class FocusWheel : public Base {
public:
    using Base::Base;

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        if (this->hasFocus())
            Base::wheelEvent(event);
        else
            event->ignore();
    }
};

// 卡片（AITranslator 的 ToolPanelStyle）：圆角渐变底板加金色描边，可见的行之间画分隔线（搜索隐藏行后自动重排）
class Card : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* event) override;
};

// 内容面板（NavigationView 的内容区）：半透明白底，左上圆角，左、上描边；顶部栏下方画金色分隔线
class ContentPanel : public QWidget {
public:
    using QWidget::QWidget;
    void setHeaderHeight(int height) { m_header = height; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_header = 0;
};

// 状态栏（AITranslator 底部栏）：AppSubtleBrush 渐变，顶部金色描边
class StatusBar : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace ss::prefs
