#include "app/Theme.h"

#include "core/config/Settings.h"

#include <QApplication>
#include <QFont>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace ss::theme {

QString resolve(const Settings& settings)
{
    const QString t = settings.stringValue(QStringLiteral("ui.theme"));
    if (t == QLatin1String("light") || t == QLatin1String("dark"))
        return t;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QPalette palette(const QString& resolved)
{
    if (resolved != QLatin1String("dark"))
        return QApplication::style() ? QApplication::style()->standardPalette() : QPalette();
    QPalette p;
    const QColor window(45, 45, 48), base(30, 30, 32), text(230, 230, 230), disabled(120, 120, 120), highlight(40, 120, 220);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::ToolTipBase, base);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, highlight);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

void apply(const Settings& settings)
{
    const QString resolved = resolve(settings);
    if (resolved == QLatin1String("dark")) {
        if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
            QApplication::setStyle(fusion);
    }
    QApplication::setPalette(palette(resolved));
    const QString family = settings.stringValue(QStringLiteral("ui.font")).trimmed();
    QFont font = QApplication::font();
    if (!family.isEmpty()) {
        font.setFamily(family);
        QApplication::setFont(font);
    }
}

QColor accent(const Settings& settings)
{
    const QColor c = QColor::fromString(settings.stringValue(QStringLiteral("ui.accentColor")));
    return c.isValid() ? c : QColor(40, 120, 220);
}

} // namespace ss::theme
