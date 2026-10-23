#pragma once

#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>
#include <optional>

namespace ss {

// 屏幕热角（ACT-12）：光标停在屏幕角落一段时间触发；离开后才会再次触发
class HotCornerDetector {
public:
    enum class Corner { None, TopLeft, TopRight, BottomLeft, BottomRight };
    HotCornerDetector(int cornerSize = 3, int dwellMs = 300)
        : m_size(cornerSize)
        , m_dwellMs(dwellMs)
    {
    }
    // 喂入当前光标位置与时间；返回本次触发的角（没有触发返回 None）
    Corner feed(QPoint physical, const QRect& screen, qint64 nowMs);
    static QString cornerName(Corner c);

private:
    int m_size;
    int m_dwellMs;
    Corner m_inside = Corner::None;
    qint64 m_since = 0;
    bool m_fired = false;
};

// 修饰键双击（ACT-10）：同一修饰键在阈值内按下两次（中间无其他键）
class DoubleTapDetector {
public:
    explicit DoubleTapDetector(int thresholdMs = 300)
        : m_thresholdMs(thresholdMs)
    {
    }
    void setThreshold(int ms) { m_thresholdMs = ms; }
    // 按键按下事件；modifierKey 为 true 表示这是被监视的修饰键。返回是否构成双击
    bool keyDown(bool modifierKey, qint64 nowMs);

private:
    int m_thresholdMs;
    qint64 m_lastTap = -1;
};

// 模拟按键过滤（ACT-15）：记录注入事件，热键触发时若最近有注入的同键事件则忽略
class InjectedFilter {
public:
    explicit InjectedFilter(int windowMs = 80)
        : m_windowMs(windowMs)
    {
    }
    void keyEvent(int vk, bool injected, qint64 nowMs);
    bool shouldIgnore(int vk, qint64 nowMs) const;

private:
    int m_windowMs;
    int m_lastVk = -1;
    qint64 m_lastInjectedMs = -1;
};

// 全局鼠标手势（ACT-11）：按住修饰键 + 某个鼠标键拖出区域
class GlobalMouseGesture {
public:
    struct Result {
        QRect region;
        int button = 0; // 1 左 2 中 3 右
    };
    void setModifierHeld(bool held) { m_modifier = held; }
    bool modifierHeld() const { return m_modifier; }
    bool active() const { return m_active; }
    // 返回 true 表示事件被手势消费（不应再传给其他程序）
    bool press(int button, QPoint physical);
    bool move(QPoint physical);
    std::optional<Result> release(int button, QPoint physical);
    QRect currentRegion() const { return m_active ? QRect(m_start, m_current).normalized() : QRect(); }

private:
    bool m_modifier = false;
    bool m_active = false;
    int m_button = 0;
    QPoint m_start;
    QPoint m_current;
};

} // namespace ss
