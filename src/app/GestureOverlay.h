#pragma once

#include <QRect>
#include <QWidget>

namespace ss {

// 全局鼠标手势（ACT-11）拖动时的框线：覆盖整个虚拟桌面的透明窗口，不接收输入
class GestureOverlay : public QWidget {
    Q_OBJECT
public:
    GestureOverlay();
    void showRegion(const QRect& physical); // 物理像素
    void hideRegion();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QRect m_physical;
};

} // namespace ss
