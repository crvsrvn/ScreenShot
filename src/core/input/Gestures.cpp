#include "core/input/Gestures.h"

namespace ss {

HotCornerDetector::Corner HotCornerDetector::feed(QPoint p, const QRect& screen, qint64 nowMs)
{
    Corner c = Corner::None;
    const bool left = p.x() <= screen.left() + m_size, right = p.x() >= screen.right() - m_size;
    const bool top = p.y() <= screen.top() + m_size, bottom = p.y() >= screen.bottom() - m_size;
    if (left && top)
        c = Corner::TopLeft;
    else if (right && top)
        c = Corner::TopRight;
    else if (left && bottom)
        c = Corner::BottomLeft;
    else if (right && bottom)
        c = Corner::BottomRight;
    if (c != m_inside) {
        m_inside = c;
        m_since = nowMs;
        m_fired = false;
    }
    if (c != Corner::None && !m_fired && nowMs - m_since >= m_dwellMs) {
        m_fired = true;
        return c;
    }
    return Corner::None;
}

QString HotCornerDetector::cornerName(Corner c)
{
    switch (c) {
    case Corner::TopLeft: return QStringLiteral("topLeft");
    case Corner::TopRight: return QStringLiteral("topRight");
    case Corner::BottomLeft: return QStringLiteral("bottomLeft");
    case Corner::BottomRight: return QStringLiteral("bottomRight");
    case Corner::None: break;
    }
    return {};
}

bool DoubleTapDetector::keyDown(bool modifierKey, qint64 nowMs)
{
    if (!modifierKey) {
        m_lastTap = -1; // 中间有别的键：不算
        return false;
    }
    if (m_lastTap >= 0 && nowMs - m_lastTap <= m_thresholdMs) {
        m_lastTap = -1;
        return true;
    }
    m_lastTap = nowMs;
    return false;
}

void InjectedFilter::keyEvent(int vk, bool injected, qint64 nowMs)
{
    if (injected) {
        m_lastVk = vk;
        m_lastInjectedMs = nowMs;
    }
}

bool InjectedFilter::shouldIgnore(int vk, qint64 nowMs) const
{
    return m_lastVk == vk && m_lastInjectedMs >= 0 && nowMs - m_lastInjectedMs <= m_windowMs;
}

bool GlobalMouseGesture::press(int button, QPoint physical)
{
    if (!m_modifier || m_active)
        return false;
    m_active = true;
    m_button = button;
    m_start = m_current = physical;
    return true;
}

bool GlobalMouseGesture::move(QPoint physical)
{
    if (!m_active)
        return false;
    m_current = physical;
    return true;
}

std::optional<GlobalMouseGesture::Result> GlobalMouseGesture::release(int button, QPoint physical)
{
    if (!m_active || button != m_button)
        return std::nullopt;
    m_active = false;
    m_current = physical;
    const QRect r = QRect(m_start, m_current).normalized();
    if (r.width() < 4 || r.height() < 4)
        return std::nullopt;
    return Result{r, button};
}

} // namespace ss
