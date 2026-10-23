#pragma once

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QRect>

namespace ss {

// 截图历史（CAP-40）：只有成功的截图进入；数量上限可设，0 为关闭。
struct HistoryEntry {
    QImage image;
    QRect physicalRegion;
    QDateTime time;
};

class CaptureHistory {
public:
    explicit CaptureHistory(int limit = 10);

    void setLimit(int limit);
    int limit() const { return m_limit; }
    void add(HistoryEntry entry); // 最新的在最前
    int size() const { return static_cast<int>(m_entries.size()); }
    bool isEmpty() const { return m_entries.isEmpty(); }
    const HistoryEntry& at(int index) const { return m_entries.at(index); }
    void clear() { m_entries.clear(); }

private:
    QList<HistoryEntry> m_entries;
    int m_limit;
};

// 在历史里前后翻页的游标：-1 表示当前（非历史）。
class HistoryCursor {
public:
    explicit HistoryCursor(const CaptureHistory& history)
        : m_history(history)
    {
    }
    int index() const { return m_index; }
    bool viewingHistory() const { return m_index >= 0; }
    bool older();  // 按 ,：更早的一张；到底返回 false
    bool newer();  // 按 .：更新的一张；回到当前时 index 变为 -1
    void reset() { m_index = -1; }

private:
    const CaptureHistory& m_history;
    int m_index = -1;
};

} // namespace ss
