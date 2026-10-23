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

#define SS_TR(text) QT_TRANSLATE_NOOP("Settings", text)

QList<SettingSpec> buildSpecs()
{
    using T = SettingType;
    QList<SettingSpec> s;
    // ---- 常规 ----
    s.append({QStringLiteral("general.language"), QStringLiteral("general"), SS_TR("Language"), SS_TR("Interface language. Takes effect after restart."), T::Enum, QStringLiteral("system"), {}, {}, {QStringLiteral("system"), QStringLiteral("zh_CN"), QStringLiteral("en")}, {SS_TR("Follow system"), SS_TR("Simplified Chinese"), SS_TR("English")}});
    s.append({QStringLiteral("general.autostart"), QStringLiteral("general"), SS_TR("Start with system"), SS_TR("Launch ScreenShot when you sign in."), T::Bool, false});
    s.append({QStringLiteral("general.trayLeftClick"), QStringLiteral("general"), SS_TR("Tray left click"), SS_TR("Action for a left click on the tray icon."), T::Enum, QStringLiteral("capture"), {}, {}, {QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("menu"), QStringLiteral("none")}, {SS_TR("Capture"), SS_TR("Pin clipboard"), SS_TR("Show menu"), SS_TR("Nothing")}});
    s.append({QStringLiteral("general.trayMiddleClick"), QStringLiteral("general"), SS_TR("Tray middle click"), SS_TR("Action for a middle click on the tray icon."), T::Enum, QStringLiteral("pin"), {}, {}, {QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("menu"), QStringLiteral("none")}, {SS_TR("Capture"), SS_TR("Pin clipboard"), SS_TR("Show menu"), SS_TR("Nothing")}});
    s.append({QStringLiteral("general.logLevel"), QStringLiteral("general"), SS_TR("Log level"), SS_TR("Minimum level written to the log file."), T::Enum, QStringLiteral("info"), {}, {}, {QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}, {SS_TR("Debug"), SS_TR("Info"), SS_TR("Warning"), SS_TR("Error")}});
    s.append({QStringLiteral("general.logMaxSizeKB"), QStringLiteral("general"), SS_TR("Log file size (KB)"), SS_TR("The log file is rotated when it grows past this size."), T::Int, 2048, 64, 1048576});
    // ---- 截图 ----
    s.append({QStringLiteral("capture.detectMode"), QStringLiteral("capture"), SS_TR("Auto detection"), SS_TR("What is highlighted under the cursor before you select: UI elements, whole windows only, or nothing."), T::Enum, QStringLiteral("element"), {}, {}, {QStringLiteral("element"), QStringLiteral("window"), QStringLiteral("off")}, {SS_TR("UI elements"), SS_TR("Windows only"), SS_TR("Off")}});
    s.append({QStringLiteral("capture.exitOnFocusLoss"), QStringLiteral("capture"), SS_TR("Exit when another window is activated"), SS_TR("Cancel the capture when another application takes focus."), T::Bool, true});
    s.append({QStringLiteral("capture.confirmEscape"), QStringLiteral("capture"), SS_TR("Confirm before Esc cancels"), SS_TR("Ask before discarding the capture with Esc."), T::Bool, false});
    s.append({QStringLiteral("capture.magnifierAuto"), QStringLiteral("capture"), SS_TR("Show magnifier automatically"), SS_TR("Show the magnifier while detecting and selecting. Alt always shows it."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierZoom"), QStringLiteral("capture"), SS_TR("Magnifier zoom"), SS_TR("Magnification factor of the magnifier."), T::Int, 8, 2, 16});
    s.append({QStringLiteral("capture.colorFormat"), QStringLiteral("capture"), SS_TR("Color format"), SS_TR("Format used when copying a pixel color with C. Shift cycles formats."), T::Enum, QStringLiteral("hex"), {}, {}, {QStringLiteral("rgb"), QStringLiteral("hex"), QStringLiteral("hsv"), QStringLiteral("hsl"), QStringLiteral("hsi")}, {SS_TR("RGB"), SS_TR("HEX"), SS_TR("HSV"), SS_TR("HSL"), SS_TR("HSI")}});
    s.append({QStringLiteral("capture.hexUppercase"), QStringLiteral("capture"), SS_TR("Uppercase HEX"), SS_TR("Write HEX colors in uppercase letters."), T::Bool, true});
    s.append({QStringLiteral("capture.swapWasdArrows"), QStringLiteral("capture"), SS_TR("Swap WASD and arrow keys"), SS_TR("Use arrow keys to move the cursor and WASD to move the selection."), T::Bool, false});
    s.append({QStringLiteral("capture.historyLimit"), QStringLiteral("capture"), SS_TR("Capture history size"), SS_TR("Number of previous captures you can recall with , and . while capturing. 0 disables history."), T::Int, 10, 0, 100});
    s.append({QStringLiteral("capture.fullScreenAllMonitors"), QStringLiteral("capture"), SS_TR("Ctrl+A selects all monitors"), SS_TR("Whether Ctrl+A selects the current monitor or every monitor at once."), T::Bool, false});
    // ---- 输出 ----
    s.append({QStringLiteral("output.saveDir"), QStringLiteral("output"), SS_TR("Save folder"), SS_TR("Default folder for the save dialog."), T::Directory, QString()});
    s.append({QStringLiteral("output.saveFormat"), QStringLiteral("output"), SS_TR("Save format"), SS_TR("Format used by the save dialog and quick save."), T::Enum, QStringLiteral("png"), {}, {}, {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")}, {SS_TR("PNG"), SS_TR("JPEG"), SS_TR("BMP"), SS_TR("WebP")}});
    s.append({QStringLiteral("output.quickSaveDir"), QStringLiteral("output"), SS_TR("Quick save folder"), SS_TR("Folder used by quick save (Ctrl+Shift+S). Empty means the Pictures folder."), T::Directory, QString()});
    s.append({QStringLiteral("output.fileNamePattern"), QStringLiteral("output"), SS_TR("File name pattern"), SS_TR("Pattern for generated file names. Variables: {date} {time} {timestamp} {width} {height} {n}."), T::String, QStringLiteral("Screenshot_{date}_{time}")});
    s.append({QStringLiteral("output.jpegQuality"), QStringLiteral("output"), SS_TR("JPEG / WebP quality"), SS_TR("0 to 100."), T::Int, 90, 0, 100});
    // ---- 控制 ----
    s.append({QStringLiteral("hotkey.capture"), QStringLiteral("control"), SS_TR("Capture hotkey"), SS_TR("Global hotkey that starts a capture."), T::KeySequence, QStringLiteral("F1")});
    s.append({QStringLiteral("hotkey.pin"), QStringLiteral("control"), SS_TR("Pin hotkey"), SS_TR("Global hotkey that pins the clipboard."), T::KeySequence, QStringLiteral("F3")});
    s.append({QStringLiteral("hotkey.togglePins"), QStringLiteral("control"), SS_TR("Hide / show pins hotkey"), SS_TR("Global hotkey that hides or shows all pins."), T::KeySequence, QStringLiteral("Shift+F3")});
    s.append({QStringLiteral("hotkey.enabled"), QStringLiteral("control"), SS_TR("Global hotkeys enabled"), SS_TR("Turn all global hotkeys on or off."), T::Bool, true});
    return s;
}

#undef SS_TR

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
