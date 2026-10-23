#pragma once

#include <QObject>
#include <QPoint>

namespace ss::win {

// 低级键盘 / 鼠标钩子（ACT-10 修饰键双击、ACT-11 全局鼠标、ACT-15 过滤模拟按键）。
// 钩子回调在安装线程（界面线程）的消息循环里执行，这里只转成信号；是否吞掉鼠标事件由 swallowMouse 决定。
class WinInputHooks : public QObject {
    Q_OBJECT
public:
    explicit WinInputHooks(QObject* parent = nullptr);
    ~WinInputHooks() override;

    bool installKeyboard();
    bool installMouse();
    void uninstall();
    bool keyboardInstalled() const { return m_keyboard != nullptr; }
    bool mouseInstalled() const { return m_mouse != nullptr; }
    // 鼠标事件是否被吞掉（手势进行中）：由使用方设置
    void setSwallowMouse(bool swallow) { m_swallowMouse = swallow; }

signals:
    void keyDown(int virtualKey, bool injected, qint64 nowMs);
    void keyUp(int virtualKey, bool injected, qint64 nowMs);
    // button：1 左 2 中 3 右；pos 为物理像素
    void mouseButton(int button, bool down, QPoint pos);
    void mouseMoved(QPoint pos);
    // 滚轮：delta 正为向上（VID-06）
    void mouseWheel(int delta, QPoint pos);

private:
    void* m_keyboard = nullptr;
    void* m_mouse = nullptr;
    bool m_swallowMouse = false;
    friend struct HookDispatch;
};

} // namespace ss::win
