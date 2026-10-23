#include "core/config/Actions.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ss {

QList<Action> actionsFromJson(const QString& json)
{
    QList<Action> out;
    for (const QJsonValue& v : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        Action a;
        a.name = o.value(QStringLiteral("name")).toString();
        a.command = o.value(QStringLiteral("command")).toString();
        a.hotkey = o.value(QStringLiteral("hotkey")).toString();
        a.inTray = o.value(QStringLiteral("inTray")).toBool(true);
        if (!a.name.isEmpty() && !a.command.isEmpty())
            out.append(a);
    }
    return out;
}

QString actionsToJson(const QList<Action>& actions)
{
    QJsonArray arr;
    for (const Action& a : actions)
        arr.append(QJsonObject{{QStringLiteral("name"), a.name}, {QStringLiteral("command"), a.command}, {QStringLiteral("hotkey"), a.hotkey}, {QStringLiteral("inTray"), a.inTray}});
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QStringList splitCommandLine(const QString& line)
{
    QStringList out;
    QString cur;
    bool quoted = false, has = false;
    for (const QChar ch : line) {
        if (ch == QLatin1Char('"')) {
            quoted = !quoted;
            has = true;
        } else if (ch.isSpace() && !quoted) {
            if (has)
                out.append(cur);
            cur.clear();
            has = false;
        } else {
            cur.append(ch);
            has = true;
        }
    }
    if (has)
        out.append(cur);
    return out;
}

} // namespace ss
