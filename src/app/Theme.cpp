#include "app/Theme.h"

#include "app/ThemeStyle.h"
#include "core/config/Settings.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QPainter>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ss::theme {

namespace {

QColor argb(QRgb v)
{
    return QColor::fromRgba(v);
}

// 系统强调色的变体：浅色用 Dark1、深色用 Light2（WinUI 的 AccentFillColorDefault 即取这两档）
QColor systemAccent(bool dark)
{
#ifdef Q_OS_WIN
    BYTE data[32] = {};
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent", L"AccentPalette",
                     RRF_RT_REG_BINARY, nullptr, data, &size) == ERROR_SUCCESS && size >= sizeof(data)) {
        const int i = dark ? 1 : 4; // 调色板依次为 Light3、Light2、Light1、基色、Dark1、Dark2、Dark3
        return QColor(data[i * 4], data[i * 4 + 1], data[i * 4 + 2]);
    }
#endif
    return dark ? QColor(0x99, 0xeb, 0xff) : QColor(0x00, 0x5a, 0x9e);
}

Colors lightColors()
{
    Colors c;
    c.background[0] = argb(0xFFFFF8DC);
    c.background[1] = argb(0xFFF6E78C);
    c.background[2] = argb(0xFFE0AD32);
    c.surface[0] = argb(0xFFFFFEF7);
    c.surface[1] = argb(0xFFFFF2BF);
    c.subtle[0] = argb(0xFFF9ECA8);
    c.subtle[1] = argb(0xFFECCA4E);
    c.border = argb(0xFFD2B340);
    c.text = argb(0xFF302500);
    c.muted = argb(0xFF6F5B16);
    c.accent[0] = argb(0xFFF6E78C);
    c.accent[1] = argb(0xFFE0AD32);
    c.accentHover[0] = argb(0xFFFFEEA3);
    c.accentHover[1] = argb(0xFFD79D1C);
    c.accentPressed[0] = argb(0xFFECCA4E);
    c.accentPressed[1] = argb(0xFFC98D12);
    c.onAccent = argb(0xFF302500);
    c.accentText = argb(0xFF795100);
    c.danger = argb(0xFFB5473B);
    c.controlText = argb(0xE4000000);
    c.controlTextSecondary = argb(0x9E000000);
    c.disabledText = argb(0x5C000000);
    c.controlFill = argb(0xB3FFFFFF);
    c.controlFillHover = argb(0x80F9F9F9);
    c.controlFillPressed = argb(0x4DF9F9F9);
    c.controlFillDisabled = argb(0x4DF9F9F9);
    c.inputActive = argb(0xFFFFFFFF);
    c.controlStroke = argb(0x0F000000);
    c.controlStrokeBottom = argb(0x29000000);
    c.strongStroke = argb(0x72000000);
    c.strongStrokeDisabled = argb(0x37000000);
    c.subtleHover = argb(0x09000000);
    c.subtlePressed = argb(0x06000000);
    c.altFill = argb(0x06000000);
    c.layer = argb(0x80FFFFFF);
    c.layerStroke = argb(0x0F000000);
    c.flyout = argb(0xFFF9F9F9);
    c.flyoutStroke = argb(0x24000000);
    c.accentDisabled = argb(0x37000000);
    c.closeHover = argb(0xFFC42B1C);
    return c;
}

Colors darkColors()
{
    Colors c;
    c.dark = true;
    c.background[0] = argb(0xFF1C180D);
    c.background[1] = argb(0xFF2E250F);
    c.background[2] = argb(0xFF4D370A);
    c.surface[0] = argb(0xFF292314);
    c.surface[1] = argb(0xFF3A2E12);
    c.subtle[0] = argb(0xFF3E3216);
    c.subtle[1] = argb(0xFF5A4310);
    c.border = argb(0xFF80651C);
    c.text = argb(0xFFFFF4CE);
    c.muted = argb(0xFFD5BD72);
    c.accent[0] = argb(0xFFF6E78C);
    c.accent[1] = argb(0xFFE0AD32);
    c.accentHover[0] = argb(0xFFFFEEA3);
    c.accentHover[1] = argb(0xFFE9BB47);
    c.accentPressed[0] = argb(0xFFECCA4E);
    c.accentPressed[1] = argb(0xFFD39A1B);
    c.onAccent = argb(0xFF302500);
    c.accentText = argb(0xFFFFD866);
    c.danger = argb(0xFFFF8A80);
    c.controlText = argb(0xFFFFFFFF);
    c.controlTextSecondary = argb(0xC5FFFFFF);
    c.disabledText = argb(0x5DFFFFFF);
    c.controlFill = argb(0x0FFFFFFF);
    c.controlFillHover = argb(0x15FFFFFF);
    c.controlFillPressed = argb(0x08FFFFFF);
    c.controlFillDisabled = argb(0x0BFFFFFF);
    c.inputActive = argb(0xB31E1E1E);
    c.controlStroke = argb(0x12FFFFFF);
    c.controlStrokeBottom = argb(0x18FFFFFF);
    c.strongStroke = argb(0x8BFFFFFF);
    c.strongStrokeDisabled = argb(0x28FFFFFF);
    c.subtleHover = argb(0x0FFFFFFF);
    c.subtlePressed = argb(0x0AFFFFFF);
    c.altFill = argb(0x19000000);
    c.layer = argb(0x4C3A3A3A);
    c.layerStroke = argb(0x19000000);
    c.flyout = argb(0xFF2C2C2C);
    c.flyoutStroke = argb(0x33000000);
    c.accentDisabled = argb(0x28FFFFFF);
    c.closeHover = argb(0xFFC42B1C);
    return c;
}

Colors& current()
{
    static Colors c = [] {
        Colors l = lightColors();
        l.systemAccent = systemAccent(false);
        return l;
    }();
    return c;
}

} // namespace

QString resolve(const Settings& settings)
{
    const QString t = settings.stringValue(QStringLiteral("ui.theme"));
    if (t == QLatin1String("light") || t == QLatin1String("dark"))
        return t;
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QPalette palette(const QString& resolved)
{
    const bool dark = resolved == QLatin1String("dark");
    const Colors c = dark ? darkColors() : lightColors();
    const QColor accent = systemAccent(dark);
    QPalette p;
    p.setColor(QPalette::Window, c.background[0]);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, dark ? QColor(0x2b, 0x26, 0x1a) : c.inputActive);
    p.setColor(QPalette::AlternateBase, c.surface[1]);
    p.setColor(QPalette::ToolTipBase, c.flyout);
    p.setColor(QPalette::ToolTipText, c.controlText);
    p.setColor(QPalette::Text, c.controlText);
    p.setColor(QPalette::Button, c.surface[0]);
    p.setColor(QPalette::ButtonText, c.controlText);
    p.setColor(QPalette::BrightText, c.danger);
    p.setColor(QPalette::Light, c.surface[0]);
    p.setColor(QPalette::Midlight, c.surface[1]);
    p.setColor(QPalette::Mid, c.border);
    p.setColor(QPalette::Dark, c.muted);
    p.setColor(QPalette::Link, c.accentText);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, dark ? Qt::black : Qt::white);
    p.setColor(QPalette::Accent, accent);
    p.setColor(QPalette::PlaceholderText, c.controlTextSecondary);
    for (QPalette::ColorRole role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.disabledText);
    return p;
}

const Colors& colors()
{
    return current();
}

QLinearGradient gradient(const QRectF& rect, const QColor* stops, int count)
{
    QLinearGradient g(rect.topLeft(), rect.bottomRight());
    g.setColorAt(0, stops[0]);
    if (count == 3)
        g.setColorAt(0.56, stops[1]);
    g.setColorAt(1, stops[count - 1]);
    return g;
}

void apply(const Settings& settings)
{
    const QString resolved = resolve(settings);
    Colors& c = current();
    c = resolved == QLatin1String("dark") ? darkColors() : lightColors();
    c.systemAccent = systemAccent(c.dark);

    if (!qobject_cast<ThemeStyle*>(QApplication::style()))
        QApplication::setStyle(new ThemeStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
    QApplication::setPalette(palette(resolved));
    // 字体与 AITranslator 默认一致：Microsoft YaHei UI 14px；设置里指定了字体则用指定的，
    // 其中缺的字（如西文字体里的中文）同 WinUI 一样回退到 Microsoft YaHei UI，而不是 Qt 默认的宋体
    QStringList families;
    const QString family = settings.stringValue(QStringLiteral("ui.font")).trimmed();
    if (!family.isEmpty())
        families.append(family);
#ifdef Q_OS_WIN
    families.append(QStringLiteral("Microsoft YaHei UI"));
#endif
    QFont font = QApplication::font();
    if (!families.isEmpty())
        font.setFamilies(families);
    font.setPixelSize(14);
    QApplication::setFont(font);
    qApp->setStyleSheet(styleSheet(c));
}

QColor accent(const Settings& settings)
{
    const QColor c = QColor::fromString(settings.stringValue(QStringLiteral("ui.accentColor")));
    return c.isValid() ? c : QColor(40, 120, 220);
}

void makeSurface(QWidget* window)
{
    // 透明窗口不画样式表背景，在窗口自己绘制之前由事件过滤器画底板
    class SurfacePainter : public QObject {
    protected:
        bool eventFilter(QObject* watched, QEvent* event) override
        {
            if (event->type() == QEvent::Paint) {
                auto* w = static_cast<QWidget*>(watched);
                const Colors& c = colors();
                QPainter p(w);
                p.setRenderHint(QPainter::Antialiasing);
                const QRectF box = QRectF(w->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
                p.setPen(QPen(c.border, 1));
                p.setBrush(gradient(box, c.surface, 2));
                p.drawRoundedRect(box, 6, 6);
            }
            return false;
        }
    };
    static SurfacePainter painter;
    window->setAttribute(Qt::WA_TranslucentBackground);
    window->installEventFilter(&painter);
}

} // namespace ss::theme
