#pragma once

#include <QKeySequence>
#include <QObject>

namespace ss {

// 全局热键（ACT-01）。触发信号在界面线程发出，携带触发时刻的单调时钟（用于 NFR-01 计时）。
class IGlobalHotkey : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    // 注册失败（冲突）返回 false。
    virtual bool registerHotkey(int id, const QKeySequence& sequence) = 0;
    virtual void unregisterHotkey(int id) = 0;

signals:
    void triggered(int id, qint64 atNs);
};

} // namespace ss
