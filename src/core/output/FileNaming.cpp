#include "core/output/FileNaming.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace ss {

QString expandFileNamePattern(const QString& pattern, const FileNameContext& ctx)
{
    static const QRegularExpression re(QStringLiteral("\\{(\\w+)(?::([^}]*))?\\}"));
    QString out;
    qsizetype last = 0;
    auto it = re.globalMatch(pattern);
    while (it.hasNext()) {
        const auto m = it.next();
        out += pattern.mid(last, m.capturedStart() - last);
        const QString var = m.captured(1).toLower();
        const QString fmt = m.captured(2);
        if (var == QLatin1String("date"))
            out += ctx.time.toString(fmt.isEmpty() ? QStringLiteral("yyyy-MM-dd") : fmt);
        else if (var == QLatin1String("time"))
            out += ctx.time.toString(fmt.isEmpty() ? QStringLiteral("HH-mm-ss") : fmt);
        else if (var == QLatin1String("timestamp"))
            out += QString::number(ctx.time.toSecsSinceEpoch());
        else if (var == QLatin1String("width"))
            out += QString::number(ctx.imageSize.width());
        else if (var == QLatin1String("height"))
            out += QString::number(ctx.imageSize.height());
        else if (var == QLatin1String("n"))
            out += QString::number(ctx.sequence);
        else
            out += m.captured(0); // 未知变量原样保留
        last = m.capturedEnd();
    }
    out += pattern.mid(last);
    return sanitizeFileName(out);
}

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

QString uniqueFilePath(const QString& dir, const QString& baseName, const QString& extension)
{
    const QDir d(dir);
    QString candidate = d.filePath(baseName + QLatin1Char('.') + extension);
    for (int i = 2; QFileInfo::exists(candidate); ++i)
        candidate = d.filePath(QStringLiteral("%1 (%2).%3").arg(baseName).arg(i).arg(extension));
    return candidate;
}

} // namespace ss
