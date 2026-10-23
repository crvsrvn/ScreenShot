#pragma once

#include "core/ports/IGlobalHotkey.h"
#include "core/util/Clock.h"

#include <QMap>

namespace ss::testing {

class FakeHotkey : public IGlobalHotkey {
    Q_OBJECT
public:
    using IGlobalHotkey::IGlobalHotkey;

    bool registerHotkey(int id, const QKeySequence& sequence) override
    {
        if (m_conflicts.contains(sequence))
            return false;
        m_registered[id] = sequence;
        return true;
    }
    void unregisterHotkey(int id) override { m_registered.remove(id); }

    void setConflicting(const QKeySequence& sequence) { m_conflicts.append(sequence); }
    bool isRegistered(int id) const { return m_registered.contains(id); }

    // 模拟按下热键
    void trigger(int id) { emit triggered(id, monotonicNs()); }

private:
    QMap<int, QKeySequence> m_registered;
    QList<QKeySequence> m_conflicts;
};

} // namespace ss::testing
