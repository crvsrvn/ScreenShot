#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

namespace ss {

// 应用内快捷键（ACT-09）：截图界面、所有贴图、图像贴图、文本贴图四个上下文分别设置。
// 每个动作有默认键；用户改动存在设置项 shortcuts.map（JSON：{"context.action": "Ctrl+X"}）。
struct ShortcutDef {
    QString context; // capture / pin / pinImage / pinText
    QString action;
    QKeySequence defaultKey;
    const char* title;
};

const QList<ShortcutDef>& shortcutDefs();

class ShortcutMap {
public:
    ShortcutMap() = default;
    explicit ShortcutMap(const QString& json);
    QString toJson() const;

    QKeySequence keyFor(const QString& context, const QString& action) const;
    void set(const QString& context, const QString& action, const QKeySequence& key);
    // 按键（含修饰键）对应的动作；没有返回空
    QString actionFor(const QString& context, int key, Qt::KeyboardModifiers modifiers) const;

private:
    QHash<QString, QKeySequence> m_overrides;
};

} // namespace ss
