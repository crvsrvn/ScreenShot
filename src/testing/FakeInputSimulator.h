#pragma once

#include "core/ports/IInputSimulator.h"

#include <QList>
#include <functional>

namespace ss::testing {

// 记录滚轮调用；可挂回调把“滚动”作用到合成画面上
class FakeInputSimulator : public IInputSimulator {
public:
    struct Wheel {
        QPoint at;
        int notches;
        bool horizontal;
    };
    void mouseWheelAt(QPoint physical, int notches, bool horizontal) override
    {
        m_wheels.append({physical, notches, horizontal});
        if (m_onWheel)
            m_onWheel(physical, notches, horizontal);
    }
    const QList<Wheel>& wheels() const { return m_wheels; }
    void sendKeyCombo(Qt::KeyboardModifiers modifiers, Qt::Key key) override { m_keys.append({modifiers, key}); }
    const QList<QPair<Qt::KeyboardModifiers, Qt::Key>>& keyCombos() const { return m_keys; }
    void setOnWheel(std::function<void(QPoint, int, bool)> fn) { m_onWheel = std::move(fn); }

private:
    QList<Wheel> m_wheels;
    QList<QPair<Qt::KeyboardModifiers, Qt::Key>> m_keys;
    std::function<void(QPoint, int, bool)> m_onWheel;
};

} // namespace ss::testing
