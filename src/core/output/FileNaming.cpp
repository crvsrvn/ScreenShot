#include "core/output/FileNaming.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUuid>

namespace ss {

namespace {

QString applyModifiers(QString value, const QStringList& modifiers)
{
    for (const QString& m : modifiers) {
        const QString mod = m.trimmed();
        if (mod.startsWith(QStringLiteral("trunc:"))) {
            const int n = mod.mid(6).toInt();
            if (n > 0 && value.size() > n)
                value = value.left(n);
        } else if (mod == QLatin1String("noext")) {
            const int dot = static_cast<int>(value.lastIndexOf(QLatin1Char('.')));
            if (dot > 0)
                value = value.left(dot);
        } else if (mod == QLatin1String("lower")) {
            value = value.toLower();
        } else if (mod == QLatin1String("upper")) {
            value = value.toUpper();
        }
    }
    return value;
}

QString expandSegment(const QString& pattern, const FileNameContext& ctx)
{
    static const QRegularExpression re(QStringLiteral("\\{([A-Za-z]+)(?::([^}|]*))?((?:\\|[^}]*)?)\\}"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(pattern);
    while (it.hasNext()) {
        const auto m = it.next();
        out += pattern.mid(last, m.capturedStart() - last);
        const QString var = m.captured(1).toLower();
        const QString fmt = m.captured(2);
        QStringList modifiers = m.captured(3).split(QLatin1Char('|'), Qt::SkipEmptyParts);
        QString value;
        bool known = true;
        if (var == QLatin1String("date"))
            value = ctx.time.toString(fmt.isEmpty() ? QStringLiteral("yyyy-MM-dd") : fmt);
        else if (var == QLatin1String("time"))
            value = ctx.time.toString(fmt.isEmpty() ? QStringLiteral("HH-mm-ss") : fmt);
        else if (var == QLatin1String("timestamp"))
            value = QString::number(ctx.time.toSecsSinceEpoch());
        else if (var == QLatin1String("width"))
            value = QString::number(ctx.imageSize.width());
        else if (var == QLatin1String("height"))
            value = QString::number(ctx.imageSize.height());
        else if (var == QLatin1String("n"))
            value = QString::number(ctx.sequence);
        else if (var == QLatin1String("title"))
            value = ctx.windowTitle;
        else if (var == QLatin1String("process"))
            value = ctx.processName;
        else if (var == QLatin1String("uuid"))
            value = QUuid::createUuid().toString(QUuid::WithoutBraces);
        else if (var == QLatin1String("computer"))
            value = ctx.computerName.isEmpty() ? QSysInfo::machineHostName() : ctx.computerName;
        else if (var == QLatin1String("env"))
            value = qEnvironmentVariable(fmt.toUtf8().constData());
        else
            known = false;
        out += known ? applyModifiers(value, modifiers) : m.captured(0);
        last = m.capturedEnd();
    }
    out += pattern.mid(last);
    return out;
}

} // namespace

QString sanitizeFileName(const QString& name)
{
    static const QRegularExpression illegal(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1F]"));
    QString s = name;
    s.replace(illegal, QStringLiteral("_"));
    s = s.trimmed();
    while (s.endsWith(QLatin1Char('.')))
        s.chop(1);
    return s.isEmpty() ? QStringLiteral("Screenshot") : s;
}

QString expandFileNamePattern(const QString& pattern, const FileNameContext& ctx)
{
    // 先按模板里的 / 分段，再逐段展开并清理，保证变量值里的 / 不会变成子目录
    QStringList segments;
    for (const QString& seg : pattern.split(QRegularExpression(QStringLiteral("[\\\\/]+")), Qt::SkipEmptyParts))
        segments << sanitizeFileName(expandSegment(seg, ctx));
    if (segments.isEmpty())
        return QStringLiteral("Screenshot");
    return segments.join(QLatin1Char('/'));
}

QString uniqueFilePath(const QString& dir, const QString& baseName, const QString& extension)
{
    const QDir d(dir);
    QString candidate = d.filePath(baseName + QLatin1Char('.') + extension);
    for (int i = 2; QFileInfo::exists(candidate); ++i)
        candidate = d.filePath(QStringLiteral("%1 (%2).%3").arg(baseName).arg(i).arg(extension));
    return candidate;
}

} // namespace ss
