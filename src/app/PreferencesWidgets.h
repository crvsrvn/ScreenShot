#pragma once

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QWheelEvent>

namespace ss::prefs {

// 偏好设置窗口的配色：奶油色到金黄的暖色调，主题色取自设置
struct Colors {
    QColor windowTop, windowBottom;    // 窗口底色渐变（左上 → 右下）
    QColor panel, panelBorder;         // 右侧内容面板（半透明，透出底色）
    QColor card, cardEnd, cardBorder;  // 卡片渐变与描边
    QColor divider;
    QColor text, secondary;
    QColor control, controlHover, controlBorder;
    QColor hover, selected, switchOff, scroll;
    QColor accent;

    static Colors make(const QColor& accent);
};

// 整个窗口的样式表（底色、面板、输入框、滚动条、提示）
QString styleSheet(const Colors& c);

// 侧边栏条目：圆角底色，选中项左侧画一段主题色短竖条
class NavDelegate : public QStyledItemDelegate {
public:
    NavDelegate(const Colors& colors, QObject* parent = nullptr);
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    Colors m_colors;
};

// 开关：替代复选框，滑块带过渡动画；仍是 QCheckBox，选中状态与信号不变
class SwitchButton : public QCheckBox {
public:
    SwitchButton(const Colors& colors, QWidget* parent = nullptr);
    QSize sizeHint() const override;

protected:
    bool hitButton(const QPoint& pos) const override;
    void paintEvent(QPaintEvent* event) override;

private:
    Colors m_colors;
    QVariantAnimation m_anim; // 0 = 关，1 = 开
};

// 下拉框：自绘圆角边框与细箭头；未获得焦点时滚轮交给页面滚动
class ComboBox : public QComboBox {
public:
    ComboBox(const Colors& colors, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    Colors m_colors;
};

// 未获得焦点时滚轮交给页面滚动，避免滚动长页面时误改数值（用于数字框）
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

// 卡片：圆角渐变底板加描边，可见的行之间画分隔线（搜索隐藏行后自动重排）
class Card : public QWidget {
public:
    Card(const Colors& colors, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Colors m_colors;
};

} // namespace ss::prefs
