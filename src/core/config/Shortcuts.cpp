#include "core/config/Shortcuts.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace ss {

const QList<ShortcutDef>& shortcutDefs()
{
    static const QList<ShortcutDef> defs{
        {QStringLiteral("capture"), QStringLiteral("copy"), QKeySequence(QStringLiteral("Ctrl+C")), QT_TRANSLATE_NOOP("Shortcuts", "Copy")},
        {QStringLiteral("capture"), QStringLiteral("save"), QKeySequence(QStringLiteral("Ctrl+S")), QT_TRANSLATE_NOOP("Shortcuts", "Save")},
        {QStringLiteral("capture"), QStringLiteral("quickSave"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), QT_TRANSLATE_NOOP("Shortcuts", "Quick save")},
        {QStringLiteral("capture"), QStringLiteral("pin"), QKeySequence(QStringLiteral("Ctrl+T")), QT_TRANSLATE_NOOP("Shortcuts", "Pin")},
        {QStringLiteral("capture"), QStringLiteral("print"), QKeySequence(QStringLiteral("Ctrl+P")), QT_TRANSLATE_NOOP("Shortcuts", "Print")},
        {QStringLiteral("capture"), QStringLiteral("ocrCopy"), QKeySequence(QStringLiteral("Shift+C")), QT_TRANSLATE_NOOP("Shortcuts", "Recognize text and copy")},
        {QStringLiteral("capture"), QStringLiteral("scroll"), QKeySequence(QStringLiteral("Ctrl+L")), QT_TRANSLATE_NOOP("Shortcuts", "Scrolling capture")},
        {QStringLiteral("capture"), QStringLiteral("refresh"), QKeySequence(QStringLiteral("F5")), QT_TRANSLATE_NOOP("Shortcuts", "Refresh")},
        {QStringLiteral("capture"), QStringLiteral("previewSave"), QKeySequence(QStringLiteral("Ctrl+Alt+S")), QT_TRANSLATE_NOOP("Shortcuts", "Save with encoding preview")},
        {QStringLiteral("pin"), QStringLiteral("close"), QKeySequence(QStringLiteral("Ctrl+W")), QT_TRANSLATE_NOOP("Shortcuts", "Close")},
        {QStringLiteral("pin"), QStringLiteral("destroy"), QKeySequence(QStringLiteral("Ctrl+D")), QT_TRANSLATE_NOOP("Shortcuts", "Destroy")},
        {QStringLiteral("pin"), QStringLiteral("copy"), QKeySequence(QStringLiteral("Ctrl+C")), QT_TRANSLATE_NOOP("Shortcuts", "Copy")},
        {QStringLiteral("pin"), QStringLiteral("copyOriginal"), QKeySequence(QStringLiteral("Ctrl+Shift+C")), QT_TRANSLATE_NOOP("Shortcuts", "Copy original")},
        {QStringLiteral("pin"), QStringLiteral("save"), QKeySequence(QStringLiteral("Ctrl+S")), QT_TRANSLATE_NOOP("Shortcuts", "Save")},
        {QStringLiteral("pin"), QStringLiteral("quickSave"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), QT_TRANSLATE_NOOP("Shortcuts", "Quick save")},
        {QStringLiteral("pin"), QStringLiteral("toolbar"), QKeySequence(QStringLiteral("Space")), QT_TRANSLATE_NOOP("Shortcuts", "Annotate")},
        {QStringLiteral("pin"), QStringLiteral("lock"), QKeySequence(QStringLiteral("L")), QT_TRANSLATE_NOOP("Shortcuts", "Lock")},
        {QStringLiteral("pin"), QStringLiteral("topmost"), QKeySequence(QStringLiteral("T")), QT_TRANSLATE_NOOP("Shortcuts", "Always on top")},
        {QStringLiteral("pin"), QStringLiteral("title"), QKeySequence(QStringLiteral("F2")), QT_TRANSLATE_NOOP("Shortcuts", "Title")},
        {QStringLiteral("pinImage"), QStringLiteral("grayscale"), QKeySequence(QStringLiteral("5")), QT_TRANSLATE_NOOP("Shortcuts", "Grayscale")},
        {QStringLiteral("pinImage"), QStringLiteral("invert"), QKeySequence(QStringLiteral("6")), QT_TRANSLATE_NOOP("Shortcuts", "Invert colors")},
        {QStringLiteral("pinImage"), QStringLiteral("darker"), QKeySequence(QStringLiteral("7")), QT_TRANSLATE_NOOP("Shortcuts", "Darker")},
        {QStringLiteral("pinImage"), QStringLiteral("brighter"), QKeySequence(QStringLiteral("8")), QT_TRANSLATE_NOOP("Shortcuts", "Brighter")},
        {QStringLiteral("pinImage"), QStringLiteral("resetProcessing"), QKeySequence(QStringLiteral("0")), QT_TRANSLATE_NOOP("Shortcuts", "Reset image processing")},
        {QStringLiteral("pinText"), QStringLiteral("copyText"), QKeySequence(QStringLiteral("Ctrl+Shift+T")), QT_TRANSLATE_NOOP("Shortcuts", "Copy text")},
        {QStringLiteral("pinText"), QStringLiteral("selectText"), QKeySequence(QStringLiteral("Ctrl+Shift+A")), QT_TRANSLATE_NOOP("Shortcuts", "Select text")},
    };
    return defs;
}

ShortcutMap::ShortcutMap(const QString& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        m_overrides.insert(it.key(), QKeySequence(it.value().toString()));
}

QString ShortcutMap::toJson() const
{
    QJsonObject o;
    for (auto it = m_overrides.cbegin(); it != m_overrides.cend(); ++it)
        o[it.key()] = it.value().toString();
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QKeySequence ShortcutMap::keyFor(const QString& context, const QString& action) const
{
    const QString id = context + QLatin1Char('.') + action;
    if (m_overrides.contains(id))
        return m_overrides.value(id);
    for (const ShortcutDef& d : shortcutDefs())
        if (d.context == context && d.action == action)
            return d.defaultKey;
    return {};
}

void ShortcutMap::set(const QString& context, const QString& action, const QKeySequence& key)
{
    m_overrides.insert(context + QLatin1Char('.') + action, key);
}

QString ShortcutMap::actionFor(const QString& context, int key, Qt::KeyboardModifiers modifiers) const
{
    // KeypadModifier 等与匹配无关的修饰键忽略
    const QKeySequence pressed(QKeyCombination(modifiers & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier), static_cast<Qt::Key>(key)));
    for (const ShortcutDef& d : shortcutDefs()) {
        if (d.context != context)
            continue;
        const QKeySequence k = keyFor(context, d.action);
        if (!k.isEmpty() && k == pressed)
            return d.action;
    }
    return {};
}

} // namespace ss
