#pragma once

#include <QPoint>
#include <Qt>

namespace ss {

// 输入模拟（CAP-62 自动滚动）。平台层实现；测试替身记录调用。
class IInputSimulator {
public:
    virtual ~IInputSimulator() = default;
    // 把光标移到物理位置并发送滚轮；notches 为正表示向上（内容向下），为负表示向下滚动
    virtual void mouseWheelAt(QPoint physical, int notches, bool horizontal = false) = 0;
    // 模拟按下一个按键组合（ACT-16 粘贴）；默认不做
    virtual void sendKeyCombo(Qt::KeyboardModifiers modifiers, Qt::Key key) { Q_UNUSED(modifiers); Q_UNUSED(key); }
};

} // namespace ss
