#include "core/config/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSaveFile>

namespace ss {

namespace {


QList<SettingSpec> buildSpecs()
{
    using T = SettingType;
    QList<SettingSpec> s;
    // ---- 常规 ----
    s.append({QStringLiteral("general.language"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Language"), QT_TRANSLATE_NOOP("Settings", "Interface language. Takes effect after restart."), T::Enum, QStringLiteral("system"), {}, {}, {QStringLiteral("system"), QStringLiteral("zh_CN"), QStringLiteral("en")}, {QT_TRANSLATE_NOOP("Settings", "Follow system"), QT_TRANSLATE_NOOP("Settings", "Simplified Chinese"), QT_TRANSLATE_NOOP("Settings", "English")}});
    s.append({QStringLiteral("general.autostart"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Start with system"), QT_TRANSLATE_NOOP("Settings", "Launch ScreenShot when you sign in."), T::Bool, false});
    s.append({QStringLiteral("general.trayLeftClick"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Tray left click"), QT_TRANSLATE_NOOP("Settings", "Action for a left click on the tray icon."), T::Enum, QStringLiteral("capture"), {}, {}, {QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("menu"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Show menu"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("general.trayMiddleClick"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Tray middle click"), QT_TRANSLATE_NOOP("Settings", "Action for a middle click on the tray icon."), T::Enum, QStringLiteral("pin"), {}, {}, {QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("menu"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Show menu"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("general.logLevel"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Log level"), QT_TRANSLATE_NOOP("Settings", "Minimum level written to the log file."), T::Enum, QStringLiteral("info"), {}, {}, {QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}, {QT_TRANSLATE_NOOP("Settings", "Debug"), QT_TRANSLATE_NOOP("Settings", "Info"), QT_TRANSLATE_NOOP("Settings", "Warning"), QT_TRANSLATE_NOOP("Settings", "Error")}});
    s.append({QStringLiteral("general.logMaxSizeKB"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Log file size (KB)"), QT_TRANSLATE_NOOP("Settings", "The log file is rotated when it grows past this size."), T::Int, 2048, 64, 1048576});
    // ---- 截图 ----
    s.append({QStringLiteral("capture.detectMode"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Auto detection"), QT_TRANSLATE_NOOP("Settings", "What is highlighted under the cursor before you select: UI elements, whole windows only, or nothing."), T::Enum, QStringLiteral("element"), {}, {}, {QStringLiteral("element"), QStringLiteral("window"), QStringLiteral("off")}, {QT_TRANSLATE_NOOP("Settings", "UI elements"), QT_TRANSLATE_NOOP("Settings", "Windows only"), QT_TRANSLATE_NOOP("Settings", "Off")}});
    s.append({QStringLiteral("capture.exitOnFocusLoss"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Exit when another window is activated"), QT_TRANSLATE_NOOP("Settings", "Cancel the capture when another application takes focus."), T::Bool, true});
    s.append({QStringLiteral("capture.confirmEscape"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Confirm before Esc cancels"), QT_TRANSLATE_NOOP("Settings", "Ask before discarding the capture with Esc."), T::Bool, false});
    s.append({QStringLiteral("capture.magnifierAuto"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Show magnifier automatically"), QT_TRANSLATE_NOOP("Settings", "Show the magnifier while detecting and selecting. Alt always shows it."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierZoom"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Magnifier zoom"), QT_TRANSLATE_NOOP("Settings", "Magnification factor of the magnifier."), T::Int, 8, 2, 16});
    s.append({QStringLiteral("capture.colorFormat"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Color format"), QT_TRANSLATE_NOOP("Settings", "Format used when copying a pixel color with C. Shift cycles formats."), T::Enum, QStringLiteral("hex"), {}, {}, {QStringLiteral("rgb"), QStringLiteral("hex"), QStringLiteral("hsv"), QStringLiteral("hsl"), QStringLiteral("hsi")}, {QT_TRANSLATE_NOOP("Settings", "RGB"), QT_TRANSLATE_NOOP("Settings", "HEX"), QT_TRANSLATE_NOOP("Settings", "HSV"), QT_TRANSLATE_NOOP("Settings", "HSL"), QT_TRANSLATE_NOOP("Settings", "HSI")}});
    s.append({QStringLiteral("capture.hexUppercase"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Uppercase HEX"), QT_TRANSLATE_NOOP("Settings", "Write HEX colors in uppercase letters."), T::Bool, true});
    s.append({QStringLiteral("capture.swapWasdArrows"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Swap WASD and arrow keys"), QT_TRANSLATE_NOOP("Settings", "Use arrow keys to move the cursor and WASD to move the selection."), T::Bool, false});
    s.append({QStringLiteral("capture.historyLimit"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Capture history size"), QT_TRANSLATE_NOOP("Settings", "Number of previous captures you can recall with , and . while capturing. 0 disables history."), T::Int, 10, 0, 100});
    s.append({QStringLiteral("capture.fullScreenAllMonitors"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Ctrl+A selects all monitors"), QT_TRANSLATE_NOOP("Settings", "Whether Ctrl+A selects the current monitor or every monitor at once."), T::Bool, false});
    // ---- 输出 ----
    s.append({QStringLiteral("output.saveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save folder"), QT_TRANSLATE_NOOP("Settings", "Default folder for the save dialog."), T::Directory, QString()});
    s.append({QStringLiteral("output.saveFormat"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save format"), QT_TRANSLATE_NOOP("Settings", "Format used by the save dialog and quick save."), T::Enum, QStringLiteral("png"), {}, {}, {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "BMP"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("output.quickSaveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Quick save folder"), QT_TRANSLATE_NOOP("Settings", "Folder used by quick save (Ctrl+Shift+S). Empty means the Pictures folder."), T::Directory, QString()});
    s.append({QStringLiteral("output.fileNamePattern"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "File name pattern"), QT_TRANSLATE_NOOP("Settings", "Pattern for generated file names. Variables: {date} {time} {timestamp} {width} {height} {n}."), T::String, QStringLiteral("Screenshot_{date}_{time}")});
    s.append({QStringLiteral("output.jpegQuality"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "JPEG / WebP quality"), QT_TRANSLATE_NOOP("Settings", "0 to 100."), T::Int, 90, 0, 100});
    // ---- 控制 ----
    s.append({QStringLiteral("hotkey.capture"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Capture hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that starts a capture."), T::KeySequence, QStringLiteral("F1")});
    s.append({QStringLiteral("hotkey.pin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that pins the clipboard."), T::KeySequence, QStringLiteral("F3")});
    s.append({QStringLiteral("hotkey.togglePins"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that hides or shows all pins."), T::KeySequence, QStringLiteral("Shift+F3")});
    s.append({QStringLiteral("hotkey.enabled"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global hotkeys enabled"), QT_TRANSLATE_NOOP("Settings", "Turn all global hotkeys on or off."), T::Bool, true});
    return s;
}


} // namespace

const QList<SettingSpec>& settingSpecs()
{
    static const QList<SettingSpec> specs = buildSpecs();
    return specs;
}

const SettingSpec* settingSpec(const QString& key)
{
    static const QHash<QString, const SettingSpec*> index = [] {
        QHash<QString, const SettingSpec*> h;
        for (const auto& s : settingSpecs())
            h.insert(s.key, &s);
        return h;
    }();
    return index.value(key, nullptr);
}

Settings::Settings(QObject* parent)
    : QObject(parent)
{
}

QVariant Settings::value(const QString& key) const
{
    if (auto it = m_values.find(key); it != m_values.end())
        return *it;
    if (const SettingSpec* spec = settingSpec(key))
        return spec->defaultValue;
    return {};
}

QVariant Settings::coerce(const SettingSpec& spec, const QVariant& value) const
{
    switch (spec.type) {
    case SettingType::Bool:
        return value.toBool();
    case SettingType::Int: {
        int v = value.toInt();
        if (spec.minimum.isValid())
            v = std::max(v, spec.minimum.toInt());
        if (spec.maximum.isValid())
            v = std::min(v, spec.maximum.toInt());
        return v;
    }
    case SettingType::Double: {
        double v = value.toDouble();
        if (spec.minimum.isValid())
            v = std::max(v, spec.minimum.toDouble());
        if (spec.maximum.isValid())
            v = std::min(v, spec.maximum.toDouble());
        return v;
    }
    case SettingType::Enum: {
        const QString v = value.toString();
        return spec.choices.contains(v) ? v : spec.defaultValue.toString();
    }
    case SettingType::KeySequence:
        return QKeySequence(value.toString()).toString(QKeySequence::PortableText);
    case SettingType::String:
    case SettingType::Directory:
    case SettingType::Color:
        return value.toString();
    }
    return value;
}

void Settings::setValue(const QString& key, const QVariant& value)
{
    const SettingSpec* spec = settingSpec(key);
    const QVariant v = spec ? coerce(*spec, value) : value;
    if (m_values.contains(key) && m_values.value(key) == v)
        return;
    if (!m_values.contains(key) && spec && spec->defaultValue == v)
        return;
    m_values.insert(key, v);
    emit changed(key, v);
}

void Settings::resetToDefault(const QString& key)
{
    if (const SettingSpec* spec = settingSpec(key))
        setValue(key, spec->defaultValue);
}

bool Settings::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    fromMap(doc.object().toVariantMap());
    m_filePath = path;
    return true;
}

bool Settings::save(const QString& path) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(QJsonObject::fromVariantMap(toMap())).toJson(QJsonDocument::Indented));
    return f.commit();
}

QVariantMap Settings::toMap() const
{
    QVariantMap map;
    for (auto it = m_values.cbegin(); it != m_values.cend(); ++it)
        map.insert(it.key(), it.value());
    return map;
}

void Settings::fromMap(const QVariantMap& map)
{
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        setValue(it.key(), it.value());
}

QString resolveConfigPath(const QString& commandLineConfig, const QString& appDir, const QString& userConfigDir)
{
    if (!commandLineConfig.isEmpty())
        return QFileInfo(commandLineConfig).absoluteFilePath();
    // 便携模式：程序目录下已有 config 目录，或程序目录可写
    const QDir app(appDir);
    const QString portable = app.filePath(QStringLiteral("config/settings.json"));
    if (QFileInfo::exists(portable))
        return portable;
    QFile probe(app.filePath(QStringLiteral(".write-test")));
    if (probe.open(QIODevice::WriteOnly)) {
        probe.close();
        probe.remove();
        return portable;
    }
    return QDir(userConfigDir).filePath(QStringLiteral("settings.json"));
}

} // namespace ss
