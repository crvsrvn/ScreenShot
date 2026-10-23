#include "core/capture/CaptureHistory.h"

namespace ss {

CaptureHistory::CaptureHistory(int limit)
    : m_limit(std::max(0, limit))
{
}

void CaptureHistory::setLimit(int limit)
{
    m_limit = std::max(0, limit);
    while (m_entries.size() > m_limit)
        m_entries.removeLast();
}

void CaptureHistory::add(HistoryEntry entry)
{
    if (m_limit == 0)
        return;
    m_entries.prepend(std::move(entry));
    while (m_entries.size() > m_limit)
        m_entries.removeLast();
}

bool HistoryCursor::older()
{
    if (m_index + 1 >= m_history.size())
        return false;
    ++m_index;
    return true;
}

bool HistoryCursor::newer()
{
    if (m_index < 0)
        return false;
    --m_index;
    return true;
}

} // namespace ss
