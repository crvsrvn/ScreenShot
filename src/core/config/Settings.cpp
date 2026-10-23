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
    s.append({QStringLiteral("general.runAsAdmin"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Run as administrator"), QT_TRANSLATE_NOOP("Settings", "Windows only: relaunch elevated so hotkeys also work over elevated programs. Autostart then uses a scheduled task."), T::Bool, false});
    s.append({QStringLiteral("ui.theme"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Theme"), QT_TRANSLATE_NOOP("Settings", "Light, dark or follow the system."), T::Enum, QStringLiteral("system"), {}, {}, {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")}, {QT_TRANSLATE_NOOP("Settings", "Follow system"), QT_TRANSLATE_NOOP("Settings", "Light"), QT_TRANSLATE_NOOP("Settings", "Dark")}});
    s.append({QStringLiteral("ui.accentColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Accent color"), QT_TRANSLATE_NOOP("Settings", "Color of the selection border and the shadow of the active pin."), T::Color, QStringLiteral("#ff2878dc")});
    s.append({QStringLiteral("ui.font"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Interface font"), QT_TRANSLATE_NOOP("Settings", "Font family for the interface. Empty uses the system font."), T::String, QString()});
    s.append({QStringLiteral("general.notifications"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Notifications"), QT_TRANSLATE_NOOP("Settings", "Show a notification after saving a file, with buttons to show, copy the file or copy its path."), T::Bool, true});
    s.append({QStringLiteral("general.trayIconStyle"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Tray icon style"), QT_TRANSLATE_NOOP("Settings", "Follow the system theme, force a light or dark icon, or use a custom color."), T::Enum, QStringLiteral("auto"), {}, {}, {QStringLiteral("auto"), QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("custom")}, {QT_TRANSLATE_NOOP("Settings", "Follow system"), QT_TRANSLATE_NOOP("Settings", "Light"), QT_TRANSLATE_NOOP("Settings", "Dark"), QT_TRANSLATE_NOOP("Settings", "Custom color")}});
    s.append({QStringLiteral("general.trayIconColor"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Tray icon color"), QT_TRANSLATE_NOOP("Settings", "Used when the tray icon style is custom."), T::Color, QStringLiteral("#ff2878dc")});
    s.append({QStringLiteral("general.trayIconHidden"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Hide tray icon"), QT_TRANSLATE_NOOP("Settings", "Run without a tray icon. Use hotkeys or the command line (prefs) to reach the settings."), T::Bool, false});
    s.append({QStringLiteral("general.floatingBar"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Floating toolbar"), QT_TRANSLATE_NOOP("Settings", "Show a small always-on-top bar with capture and pin buttons; drop images or text on it to pin them."), T::Bool, false});
    s.append({QStringLiteral("general.floatingBarPos"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Floating toolbar position"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically."), T::String, QString()});
    s.append({QStringLiteral("general.processPriority"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Process priority"), QT_TRANSLATE_NOOP("Settings", "Windows only: a higher priority keeps captures responsive while other programs are busy."), T::Enum, QStringLiteral("normal"), {}, {}, {QStringLiteral("normal"), QStringLiteral("aboveNormal"), QStringLiteral("high")}, {QT_TRANSLATE_NOOP("Settings", "Normal"), QT_TRANSLATE_NOOP("Settings", "Above normal"), QT_TRANSLATE_NOOP("Settings", "High")}});
    s.append({QStringLiteral("advanced.animations"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Animations"), QT_TRANSLATE_NOOP("Settings", "Master switch for interface animations."), T::Bool, true});
    s.append({QStringLiteral("advanced.notificationMs"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Notification duration (ms)"), QT_TRANSLATE_NOOP("Settings", "How long notifications stay."), T::Int, 5000, 500, 60000});
    s.append({QStringLiteral("advanced.elementTimeoutMs"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Element detection timeout (ms)"), QT_TRANSLATE_NOOP("Settings", "Upper bound for one UI element query."), T::Int, 300, 50, 2000});
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
    // ---- 标注 ----
    s.append({QStringLiteral("annotation.color"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Default color"), QT_TRANSLATE_NOOP("Settings", "Color used by new annotations."), T::Color, QStringLiteral("#ffe63c3c")});
    s.append({QStringLiteral("annotation.width"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Default pen width"), QT_TRANSLATE_NOOP("Settings", "Stroke width of new annotations in pixels."), T::Int, 3, 1, 100});
    s.append({QStringLiteral("annotation.widthPresets"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Pen width presets"), QT_TRANSLATE_NOOP("Settings", "Up to five preset widths shown on the toolbar, separated by commas."), T::String, QStringLiteral("2,4,6,10,16")});
    s.append({QStringLiteral("annotation.rememberColorPerTool"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Remember color per tool"), QT_TRANSLATE_NOOP("Settings", "Each tool keeps its own color."), T::Bool, true});
    s.append({QStringLiteral("annotation.secondEdit"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Edit finished annotations"), QT_TRANSLATE_NOOP("Settings", "Click a finished annotation to select it, then move, resize, rotate or restyle it."), T::Bool, true});
    s.append({QStringLiteral("annotation.selectAfterCreate"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Select new annotations"), QT_TRANSLATE_NOOP("Settings", "Select an annotation right after drawing it."), T::Bool, false});
    s.append({QStringLiteral("annotation.toolOverridesDrag"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Dragging on an annotation draws"), QT_TRANSLATE_NOOP("Settings", "With a tool active, dragging over an existing annotation draws a new one instead of moving it."), T::Bool, true});
    s.append({QStringLiteral("annotation.pencilSmooth"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Smooth pencil strokes"), QT_TRANSLATE_NOOP("Settings", "Smooth free-hand pencil lines."), T::Bool, true});
    s.append({QStringLiteral("annotation.pencilAngle"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Pencil straight-line angle"), QT_TRANSLATE_NOOP("Settings", "Angle step used when drawing a straight pencil line with Shift."), T::Enum, QStringLiteral("45"), {}, {}, {QStringLiteral("free"), QStringLiteral("45"), QStringLiteral("30"), QStringLiteral("15"), QStringLiteral("10"), QStringLiteral("5")}, {QT_TRANSLATE_NOOP("Settings", "Free"), QT_TRANSLATE_NOOP("Settings", "45 degrees"), QT_TRANSLATE_NOOP("Settings", "30 degrees"), QT_TRANSLATE_NOOP("Settings", "15 degrees"), QT_TRANSLATE_NOOP("Settings", "10 degrees"), QT_TRANSLATE_NOOP("Settings", "5 degrees")}});
    s.append({QStringLiteral("annotation.highlighterBlend"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Highlighter blend"), QT_TRANSLATE_NOOP("Settings", "Multiply keeps dark content readable; translucent overlays the color."), T::Enum, QStringLiteral("multiply"), {}, {}, {QStringLiteral("multiply"), QStringLiteral("translucent")}, {QT_TRANSLATE_NOOP("Settings", "Multiply"), QT_TRANSLATE_NOOP("Settings", "Translucent")}});
    s.append({QStringLiteral("annotation.highlighterAlpha"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Highlighter opacity"), QT_TRANSLATE_NOOP("Settings", "Opacity of the highlighter in translucent mode, 0 to 255."), T::Int, 110, 1, 255});
    s.append({QStringLiteral("annotation.textFont"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text font"), QT_TRANSLATE_NOOP("Settings", "Font family for the text tool."), T::String, QStringLiteral("Microsoft YaHei")});
    s.append({QStringLiteral("annotation.textSize"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text size"), QT_TRANSLATE_NOOP("Settings", "Text size in pixels."), T::Int, 22, 6, 200});
    s.append({QStringLiteral("annotation.palette"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Color palette"), QT_TRANSLATE_NOOP("Settings", "Palette colors on the toolbar, separated by commas (#RRGGBB or #AARRGGBB). Right-click a color button to edit, reorder, import or export."), T::String, QStringLiteral("#e63c3c,#ffa000,#ffe600,#3cb44b,#2878dc,#963cdc,#ffffff,#000000")});
    s.append({QStringLiteral("annotation.toolbarCommon"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Toolbar tools"), QT_TRANSLATE_NOOP("Settings", "Tools shown directly on the toolbar, in order (rect, ellipse, line, arrow, pencil, highlighter, mosaic, blur, text, eraser, step, magnifier, spotlight, smartErase). The rest go under More; the last one used from there gets a quick button."), T::String, QStringLiteral("rect,ellipse,line,arrow,pencil,highlighter,mosaic,blur,text,eraser,step,magnifier,spotlight,smartErase")});
    s.append({QStringLiteral("annotation.rememberTool"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Remember the last tool"), QT_TRANSLATE_NOOP("Settings", "Captures and pins each remember the tool used last time."), T::Bool, false});
    s.append({QStringLiteral("annotation.lastToolCapture"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Last tool (capture)"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically."), T::String, QString()});
    s.append({QStringLiteral("annotation.lastToolPin"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Last tool (pin)"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically."), T::String, QString()});
    s.append({QStringLiteral("annotation.defaultCaptureTool"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Default capture tool"), QT_TRANSLATE_NOOP("Settings", "Tool activated as soon as an area is selected; none keeps the plain selection."), T::String, QStringLiteral("none")});
    s.append({QStringLiteral("annotation.arrowNote"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Double-click an arrow to add a note"), QT_TRANSLATE_NOOP("Settings", "Double-clicking a line or arrow opens a note editor at its end."), T::Bool, true});
    s.append({QStringLiteral("annotation.autoMosaic"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Automatic mosaic"), QT_TRANSLATE_NOOP("Settings", "After covering one target, cover every other place in the image with the same content; they are added and removed together."), T::Bool, false});
    s.append({QStringLiteral("annotation.spotlightOpacity"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Spotlight mask opacity"), QT_TRANSLATE_NOOP("Settings", "0 to 255."), T::Int, 150, 0, 255});
    s.append({QStringLiteral("annotation.watermarkText"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark text"), QT_TRANSLATE_NOOP("Settings", "Variables: {date} {time} {datetime}."), T::String, QStringLiteral("ScreenShot {date}")});
    s.append({QStringLiteral("annotation.watermarkPlacement"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark placement"), QT_TRANSLATE_NOOP("Settings", "Tiled across the image or at one of nine positions."), T::Enum, QStringLiteral("bottomRight"), {}, {}, {QStringLiteral("tiled"), QStringLiteral("topLeft"), QStringLiteral("top"), QStringLiteral("topRight"), QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"), QStringLiteral("bottomLeft"), QStringLiteral("bottom"), QStringLiteral("bottomRight")}, {QT_TRANSLATE_NOOP("Settings", "Tiled"), QT_TRANSLATE_NOOP("Settings", "Top left"), QT_TRANSLATE_NOOP("Settings", "Top"), QT_TRANSLATE_NOOP("Settings", "Top right"), QT_TRANSLATE_NOOP("Settings", "Left"), QT_TRANSLATE_NOOP("Settings", "Center"), QT_TRANSLATE_NOOP("Settings", "Right"), QT_TRANSLATE_NOOP("Settings", "Bottom left"), QT_TRANSLATE_NOOP("Settings", "Bottom"), QT_TRANSLATE_NOOP("Settings", "Bottom right")}});
    s.append({QStringLiteral("annotation.watermarkOpacity"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark opacity"), QT_TRANSLATE_NOOP("Settings", "0 to 255."), T::Int, 110, 0, 255});
    s.append({QStringLiteral("annotation.watermarkSize"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark font size"), QT_TRANSLATE_NOOP("Settings", "Point size."), T::Int, 18, 6, 200});
    s.append({QStringLiteral("annotation.watermarkSpacing"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark spacing"), QT_TRANSLATE_NOOP("Settings", "Gap between tiled copies in pixels."), T::Int, 120, 0, 2000});
    s.append({QStringLiteral("annotation.watermarkFont"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Watermark font"), QT_TRANSLATE_NOOP("Settings", "Font family."), T::String, QStringLiteral("Microsoft YaHei")});
    s.append({QStringLiteral("annotation.textOutlineWidth"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text outline width"), QT_TRANSLATE_NOOP("Settings", "Outline width in pixels when the text outline is on."), T::Int, 2, 1, 20});
    s.append({QStringLiteral("annotation.textBackground"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text background color"), QT_TRANSLATE_NOOP("Settings", "Background color (with opacity) when the text background is on."), T::Color, QStringLiteral("#c8ffffff")});
    s.append({QStringLiteral("annotation.textBackgroundRadius"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text background corner radius"), QT_TRANSLATE_NOOP("Settings", "Corner radius of the text background in pixels."), T::Int, 4, 0, 100});
    s.append({QStringLiteral("annotation.textBackgroundPadding"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text background padding"), QT_TRANSLATE_NOOP("Settings", "Space between the text and the edge of its background in pixels."), T::Int, 6, 0, 100});
    s.append({QStringLiteral("annotation.textShadowColor"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Text shadow color"), QT_TRANSLATE_NOOP("Settings", "Color (with opacity) of the text shadow."), T::Color, QStringLiteral("#80000000")});
    s.append({QStringLiteral("annotation.stepStyle"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Step number style"), QT_TRANSLATE_NOOP("Settings", "How step numbers are written."), T::Enum, QStringLiteral("numeric"), {}, {}, {QStringLiteral("numeric"), QStringLiteral("alpha"), QStringLiteral("roman")}, {QT_TRANSLATE_NOOP("Settings", "Numbers"), QT_TRANSLATE_NOOP("Settings", "Letters"), QT_TRANSLATE_NOOP("Settings", "Roman numerals")}});
    s.append({QStringLiteral("annotation.stepRenumber"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Renumber steps after deleting one"), QT_TRANSLATE_NOOP("Settings", "When a step number is deleted, later steps move down to fill the gap."), T::Bool, true});
    s.append({QStringLiteral("annotation.magnifierZoom"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Magnifier annotation zoom"), QT_TRANSLATE_NOOP("Settings", "Default zoom factor of the magnifier annotation, 1 to 8."), T::Int, 2, 1, 8});
    // ---- 贴图 ----
    s.append({QStringLiteral("pin.defaultOpacity"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Default opacity"), QT_TRANSLATE_NOOP("Settings", "Opacity of new pins, 10 to 100 percent."), T::Int, 100, 10, 100});
    s.append({QStringLiteral("pin.opacityStep"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Opacity step"), QT_TRANSLATE_NOOP("Settings", "Change per Ctrl+wheel notch or Ctrl+plus / minus, in percent."), T::Int, 10, 1, 50});
    s.append({QStringLiteral("pin.zoomStep"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Zoom step"), QT_TRANSLATE_NOOP("Settings", "Change per wheel notch or plus / minus, in percent."), T::Int, 10, 1, 100});
    s.append({QStringLiteral("pin.dragEdgeZoom"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Resize by dragging the edge"), QT_TRANSLATE_NOOP("Settings", "Allow zooming a pin by dragging its border."), T::Bool, true});
    s.append({QStringLiteral("pin.passthroughOpacity"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Opacity when click-through"), QT_TRANSLATE_NOOP("Settings", "Pins become this opaque while mouse click-through is on; 100 keeps the current opacity."), T::Int, 50, 10, 100});
    s.append({QStringLiteral("pin.restoreLimit"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Closed pins to keep"), QT_TRANSLATE_NOOP("Settings", "How many closed pins can be restored."), T::Int, 10, 0, 100});
    s.append({QStringLiteral("pin.restoreOrder"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Restore order"), QT_TRANSLATE_NOOP("Settings", "Which closed pin comes back first."), T::Enum, QStringLiteral("closed"), {}, {}, {QStringLiteral("closed"), QStringLiteral("created")}, {QT_TRANSLATE_NOOP("Settings", "Most recently closed"), QT_TRANSLATE_NOOP("Settings", "Most recently created")}});
    s.append({QStringLiteral("pin.maxSizePercent"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Maximum initial size"), QT_TRANSLATE_NOOP("Settings", "New pins larger than this percentage of the screen are shrunk to fit."), T::Int, 90, 10, 100});
    s.append({QStringLiteral("pin.textFont"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin font"), QT_TRANSLATE_NOOP("Settings", "Font family for text pins."), T::String, QStringLiteral("Microsoft YaHei")});
    s.append({QStringLiteral("pin.textSize"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin font size"), QT_TRANSLATE_NOOP("Settings", "Point size for text pins."), T::Int, 12, 6, 72});
    s.append({QStringLiteral("pin.textColor"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin color"), QT_TRANSLATE_NOOP("Settings", "Text color for text pins."), T::Color, QStringLiteral("#ff202020")});
    s.append({QStringLiteral("pin.textBackground"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin background"), QT_TRANSLATE_NOOP("Settings", "Background color for text pins."), T::Color, QStringLiteral("#fffff8dc")});
    s.append({QStringLiteral("pin.textMaxWidth"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin max width"), QT_TRANSLATE_NOOP("Settings", "Lines wrap beyond this width in pixels."), T::Int, 600, 100, 4000});
    s.append({QStringLiteral("pin.textPadding"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Text pin padding"), QT_TRANSLATE_NOOP("Settings", "Space around the text in pixels."), T::Int, 8, 0, 100});
    s.append({QStringLiteral("pin.mouseDoubleClick"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Double-click on a pin"), QT_TRANSLATE_NOOP("Settings", "What a double-click does."), T::Enum, QStringLiteral("close"), {}, {}, {QStringLiteral("close"), QStringLiteral("destroy"), QStringLiteral("copy"), QStringLiteral("reset"), QStringLiteral("thumbnail"), QStringLiteral("menu"), QStringLiteral("passthrough"), QStringLiteral("topmost"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Close"), QT_TRANSLATE_NOOP("Settings", "Destroy"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Reset"), QT_TRANSLATE_NOOP("Settings", "Thumbnail"), QT_TRANSLATE_NOOP("Settings", "Menu"), QT_TRANSLATE_NOOP("Settings", "Click-through"), QT_TRANSLATE_NOOP("Settings", "Always on top"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("pin.mouseMiddle"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Middle click on a pin"), QT_TRANSLATE_NOOP("Settings", "What a middle click does."), T::Enum, QStringLiteral("reset"), {}, {}, {QStringLiteral("close"), QStringLiteral("destroy"), QStringLiteral("copy"), QStringLiteral("reset"), QStringLiteral("thumbnail"), QStringLiteral("menu"), QStringLiteral("passthrough"), QStringLiteral("topmost"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Close"), QT_TRANSLATE_NOOP("Settings", "Destroy"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Reset"), QT_TRANSLATE_NOOP("Settings", "Thumbnail"), QT_TRANSLATE_NOOP("Settings", "Menu"), QT_TRANSLATE_NOOP("Settings", "Click-through"), QT_TRANSLATE_NOOP("Settings", "Always on top"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("pin.mouseShiftDoubleClick"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Shift+double-click on a pin"), QT_TRANSLATE_NOOP("Settings", "What Shift+double-click does."), T::Enum, QStringLiteral("thumbnail"), {}, {}, {QStringLiteral("close"), QStringLiteral("destroy"), QStringLiteral("copy"), QStringLiteral("reset"), QStringLiteral("thumbnail"), QStringLiteral("menu"), QStringLiteral("passthrough"), QStringLiteral("topmost"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Close"), QT_TRANSLATE_NOOP("Settings", "Destroy"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Reset"), QT_TRANSLATE_NOOP("Settings", "Thumbnail"), QT_TRANSLATE_NOOP("Settings", "Menu"), QT_TRANSLATE_NOOP("Settings", "Click-through"), QT_TRANSLATE_NOOP("Settings", "Always on top"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("pin.mouseShiftRight"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Shift+right-click on a pin"), QT_TRANSLATE_NOOP("Settings", "What Shift+right-click does."), T::Enum, QStringLiteral("menu"), {}, {}, {QStringLiteral("close"), QStringLiteral("destroy"), QStringLiteral("copy"), QStringLiteral("reset"), QStringLiteral("thumbnail"), QStringLiteral("menu"), QStringLiteral("passthrough"), QStringLiteral("topmost"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Close"), QT_TRANSLATE_NOOP("Settings", "Destroy"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Reset"), QT_TRANSLATE_NOOP("Settings", "Thumbnail"), QT_TRANSLATE_NOOP("Settings", "Menu"), QT_TRANSLATE_NOOP("Settings", "Click-through"), QT_TRANSLATE_NOOP("Settings", "Always on top"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("pin.newPosition"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Where new pins appear"), QT_TRANSLATE_NOOP("Settings", "Capture position (clipboard pins use the cursor), cursor position, center of the primary screen, or center of the screen under the cursor."), T::Enum, QStringLiteral("capture"), {}, {}, {QStringLiteral("capture"), QStringLiteral("cursor"), QStringLiteral("primaryCenter"), QStringLiteral("cursorScreenCenter")}, {QT_TRANSLATE_NOOP("Settings", "Capture position"), QT_TRANSLATE_NOOP("Settings", "Cursor position"), QT_TRANSLATE_NOOP("Settings", "Center of primary screen"), QT_TRANSLATE_NOOP("Settings", "Center of screen under cursor")}});
    s.append({QStringLiteral("pin.neverPinFilePaths"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Never pin file paths"), QT_TRANSLATE_NOOP("Settings", "Ignore non-image files instead of showing a file card."), T::Bool, false});
    s.append({QStringLiteral("pin.restoreOnStart"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Restore pins after restart"), QT_TRANSLATE_NOOP("Settings", "Pins that are still open when ScreenShot quits come back on the next start (position, zoom, opacity, annotations, group)."), T::Bool, true});
    s.append({QStringLiteral("pin.duplicateBehavior"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Pinning the same content again"), QT_TRANSLATE_NOOP("Settings", "Shake the existing pin, restore the closed one, or pin it again."), T::Enum, QStringLiteral("duplicate"), {}, {}, {QStringLiteral("shake"), QStringLiteral("restore"), QStringLiteral("duplicate")}, {QT_TRANSLATE_NOOP("Settings", "Shake the existing pin"), QT_TRANSLATE_NOOP("Settings", "Restore the closed pin"), QT_TRANSLATE_NOOP("Settings", "Pin it again")}});
    s.append({QStringLiteral("pin.zoomAnchor"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Zoom around"), QT_TRANSLATE_NOOP("Settings", "Point that stays fixed while zooming with the wheel or keys."), T::Enum, QStringLiteral("cursor"), {}, {}, {QStringLiteral("cursor"), QStringLiteral("center"), QStringLiteral("topLeft"), QStringLiteral("topRight"), QStringLiteral("bottomLeft"), QStringLiteral("bottomRight")}, {QT_TRANSLATE_NOOP("Settings", "Cursor"), QT_TRANSLATE_NOOP("Settings", "Center"), QT_TRANSLATE_NOOP("Settings", "Top-left corner"), QT_TRANSLATE_NOOP("Settings", "Top-right corner"), QT_TRANSLATE_NOOP("Settings", "Bottom-left corner"), QT_TRANSLATE_NOOP("Settings", "Bottom-right corner")}});
    s.append({QStringLiteral("pin.zoomFineStep"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Fine zoom step"), QT_TRANSLATE_NOOP("Settings", "Zoom change per wheel notch while Shift is held, in percent."), T::Int, 2, 1, 50});
    s.append({QStringLiteral("pin.smoothZoom"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Smooth zoom"), QT_TRANSLATE_NOOP("Settings", "Resample smoothly when zoomed. Off keeps pixels sharp, which suits enlarged text."), T::Bool, true});
    s.append({QStringLiteral("pin.lockType"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Lock (L) prevents"), QT_TRANSLATE_NOOP("Settings", "What a locked pin refuses: everything (move, zoom, close), only moving, or only zooming."), T::Enum, QStringLiteral("all"), {}, {}, {QStringLiteral("all"), QStringLiteral("move"), QStringLiteral("zoom")}, {QT_TRANSLATE_NOOP("Settings", "Moving, zooming and closing"), QT_TRANSLATE_NOOP("Settings", "Moving"), QT_TRANSLATE_NOOP("Settings", "Zooming")}});
    s.append({QStringLiteral("pin.titlePosition"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Pin title position"), QT_TRANSLATE_NOOP("Settings", "Where the title (F2) is drawn."), T::Enum, QStringLiteral("top"), {}, {}, {QStringLiteral("top"), QStringLiteral("bottom")}, {QT_TRANSLATE_NOOP("Settings", "Top"), QT_TRANSLATE_NOOP("Settings", "Bottom")}});
    s.append({QStringLiteral("pin.alphaBackground"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Background for transparent images"), QT_TRANSLATE_NOOP("Settings", "Transparent (clicks pass through transparent areas), pseudo-transparent, dark or light checkerboard."), T::Enum, QStringLiteral("checkerLight"), {}, {}, {QStringLiteral("transparent"), QStringLiteral("pseudo"), QStringLiteral("checkerDark"), QStringLiteral("checkerLight")}, {QT_TRANSLATE_NOOP("Settings", "Transparent"), QT_TRANSLATE_NOOP("Settings", "Pseudo-transparent"), QT_TRANSLATE_NOOP("Settings", "Dark checkerboard"), QT_TRANSLATE_NOOP("Settings", "Light checkerboard")}});
    s.append({QStringLiteral("pin.allDesktops"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Show pins on all virtual desktops"), QT_TRANSLATE_NOOP("Settings", "Windows only; uses an undocumented system interface and may not work on every build."), T::Bool, false});
    s.append({QStringLiteral("pin.textHtml"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Keep rich text formatting"), QT_TRANSLATE_NOOP("Settings", "Pin HTML text with its colors and sizes. Off pins plain text only."), T::Bool, true});
    s.append({QStringLiteral("pin.replacePolicy"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "When replacing a pin"), QT_TRANSLATE_NOOP("Settings", "What happens to the old pin when Ctrl+V or a dropped file replaces it."), T::Enum, QStringLiteral("close"), {}, {}, {QStringLiteral("keep"), QStringLiteral("close"), QStringLiteral("destroy")}, {QT_TRANSLATE_NOOP("Settings", "Keep it"), QT_TRANSLATE_NOOP("Settings", "Close it (restorable)"), QT_TRANSLATE_NOOP("Settings", "Destroy it")}});
    s.append({QStringLiteral("pin.snapDistance"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Snap distance"), QT_TRANSLATE_NOOP("Settings", "While dragging with Shift, pins snap to other pins and screen edges within this many pixels."), T::Int, 12, 1, 100});
    s.append({QStringLiteral("pin.thumbnailSize"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Thumbnail size"), QT_TRANSLATE_NOOP("Settings", "Longest side of a pin in thumbnail mode (Shift+double-click), in pixels."), T::Int, 160, 16, 2000});
    s.append({QStringLiteral("pin.shadow"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Window shadow"), QT_TRANSLATE_NOOP("Settings", "Draw a shadow around pins. Its color shows the state: active, inactive, click-through, selected, group."), T::Bool, true});
    s.append({QStringLiteral("pin.confirmClose"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Confirm closing a pin"), QT_TRANSLATE_NOOP("Settings", "Ask before Esc or a double-click closes a pin."), T::Bool, false});
    s.append({QStringLiteral("pin.confirmBatch"), QStringLiteral("pin"), QT_TRANSLATE_NOOP("Settings", "Confirm closing several pins"), QT_TRANSLATE_NOOP("Settings", "Ask before closing or destroying more than one pin at once."), T::Bool, true});
    s.append({QStringLiteral("capture.includeCursor"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Include mouse cursor"), QT_TRANSLATE_NOOP("Settings", "Draw the mouse cursor into captures by default. The ` key toggles it while capturing."), T::Bool, false});
    s.append({QStringLiteral("capture.enterActions"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Actions on Enter"), QT_TRANSLATE_NOOP("Settings", "What happens when you confirm a capture: a comma-separated list of copy, pin, save, quickSave, ocrCopy, executed in that fixed order."), T::String, QStringLiteral("copy")});
    s.append({QStringLiteral("capture.presets"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Custom capture presets"), QT_TRANSLATE_NOOP("Settings", "Named presets for the custom capture dialog (JSON, managed by the dialog)."), T::String, QStringLiteral("[]")});
    s.append({QStringLiteral("capture.lastRegion"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Last capture area"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically; used by the \"last area\" quick capture."), T::String, QString()});
    s.append({QStringLiteral("capture.whiteboardColor"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Whiteboard color"), QT_TRANSLATE_NOOP("Settings", "Background color of the whiteboard."), T::Color, QStringLiteral("#ffffffff")});
    s.append({QStringLiteral("capture.whiteboardCurrentScreen"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Whiteboard covers the current screen only"), QT_TRANSLATE_NOOP("Settings", "Otherwise the whiteboard covers every screen."), T::Bool, true});
    s.append({QStringLiteral("output.cornerRadius"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Rounded corners"), QT_TRANSLATE_NOOP("Settings", "Corner radius applied to captures in pixels; 0 keeps square corners. Corners outside the radius are transparent."), T::Int, 0, 0, 200});
    s.append({QStringLiteral("output.shadow"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Add shadow"), QT_TRANSLATE_NOOP("Settings", "Add a drop shadow around copied, saved and pinned captures."), T::Bool, false});
    s.append({QStringLiteral("output.shadowColor"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Shadow color"), QT_TRANSLATE_NOOP("Settings", "Color (with opacity) of the shadow."), T::Color, QStringLiteral("#8c000000")});
    s.append({QStringLiteral("output.border"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Add border"), QT_TRANSLATE_NOOP("Settings", "Draw a border around copied, saved and pinned captures."), T::Bool, false});
    s.append({QStringLiteral("output.borderColor"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Border color"), QT_TRANSLATE_NOOP("Settings", "Color of the border."), T::Color, QStringLiteral("#ff2878dc")});
    s.append({QStringLiteral("output.borderWidth"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Border width"), QT_TRANSLATE_NOOP("Settings", "Width of the border in pixels."), T::Int, 2, 1, 50});
    // ---- 滚动截图 ----
    s.append({QStringLiteral("scroll.autoScroll"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Auto scroll by default"), QT_TRANSLATE_NOOP("Settings", "Start scrolling captures with automatic scrolling on."), T::Bool, false});
    s.append({QStringLiteral("scroll.speed"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Auto scroll speed"), QT_TRANSLATE_NOOP("Settings", "Wheel notches per step when auto scrolling."), T::Int, 3, 1, 10});
    s.append({QStringLiteral("scroll.direction"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Scrolling direction"), QT_TRANSLATE_NOOP("Settings", "Default stitching direction for scrolling captures."), T::Enum, QStringLiteral("vertical"), {}, {}, {QStringLiteral("vertical"), QStringLiteral("horizontal")}, {QT_TRANSLATE_NOOP("Settings", "Vertical"), QT_TRANSLATE_NOOP("Settings", "Horizontal")}});
    s.append({QStringLiteral("scroll.tickMs"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Scrolling capture interval (ms)"), QT_TRANSLATE_NOOP("Settings", "How often the area is captured while scrolling."), T::Int, 80, 30, 500});
    s.append({QStringLiteral("scroll.stableFrames"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Stable frames before stitching"), QT_TRANSLATE_NOOP("Settings", "Consecutive identical captures required before a frame is used."), T::Int, 2, 2, 5});
    s.append({QStringLiteral("scroll.endFrames"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Frames without movement that mean the end"), QT_TRANSLATE_NOOP("Settings", "Auto scrolling stops after this many unchanged captures."), T::Int, 3, 2, 10});
    // ---- 输出 ----
    s.append({QStringLiteral("output.saveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save folder"), QT_TRANSLATE_NOOP("Settings", "Default folder for the save dialog."), T::Directory, QString()});
    s.append({QStringLiteral("output.saveFormat"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save format"), QT_TRANSLATE_NOOP("Settings", "Format used by the save dialog and quick save."), T::Enum, QStringLiteral("png"), {}, {}, {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "BMP"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("output.quickSaveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Quick save folder"), QT_TRANSLATE_NOOP("Settings", "Folder used by quick save (Ctrl+Shift+S). Empty means the Pictures folder."), T::Directory, QString()});
    s.append({QStringLiteral("output.fileNamePattern"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "File name pattern"), QT_TRANSLATE_NOOP("Settings", "Pattern for generated file names. Variables: {date} {time} {timestamp} {width} {height} {n} {title} {process} {uuid} {computer} {env:NAME}; modifiers |trunc:N |noext; / makes sub-folders."), T::String, QStringLiteral("Screenshot_{date}_{time}")});
    s.append({QStringLiteral("output.jpegQuality"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "JPEG / WebP quality"), QT_TRANSLATE_NOOP("Settings", "0 to 100."), T::Int, 90, 0, 100});
    s.append({QStringLiteral("output.copyAfterSave"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Copy after saving"), QT_TRANSLATE_NOOP("Settings", "Also put the image on the clipboard after it is saved."), T::Bool, false});
    s.append({QStringLiteral("output.metadata"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Write metadata"), QT_TRANSLATE_NOOP("Settings", "Store the source window, process and capture time in the image file (PNG / JPEG / WebP text fields)."), T::Bool, true});
    s.append({QStringLiteral("output.postCommand"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Run after saving"), QT_TRANSLATE_NOOP("Settings", "External command run after a file is saved; {path} is replaced by the file path. Empty disables it."), T::String, QString()});
    s.append({QStringLiteral("output.pdfMarginMm"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "PDF page margin (mm)"), QT_TRANSLATE_NOOP("Settings", "Margin around the image when saving as PDF."), T::Int, 10, 0, 50});
    s.append({QStringLiteral("output.pdfPaginate"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Split tall images into PDF pages"), QT_TRANSLATE_NOOP("Settings", "Long captures are cut into A4 pages instead of being scaled onto one page."), T::Bool, true});
    s.append({QStringLiteral("output.qualityAuto"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Automatic quality"), QT_TRANSLATE_NOOP("Settings", "Use a sensible default quality (JPEG 92, WebP 90) instead of the number above."), T::Bool, true});
    s.append({QStringLiteral("output.copyAsFile"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Copy as file"), QT_TRANSLATE_NOOP("Settings", "Also put an image file on the clipboard so it can be pasted into file managers, terminals and chat apps. Auto: only when the image has transparency."), T::Enum, QStringLiteral("off"), {}, {}, {QStringLiteral("off"), QStringLiteral("auto"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "Automatic"), QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("output.autoSave"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Auto save"), QT_TRANSLATE_NOOP("Settings", "Save an extra copy of every successful capture."), T::Bool, false});
    s.append({QStringLiteral("output.autoSaveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Auto save folder"), QT_TRANSLATE_NOOP("Settings", "Folder for auto-saved captures. Empty means Pictures/ScreenShot."), T::Directory, QString()});
    s.append({QStringLiteral("output.autoSaveKeepDays"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Delete auto-saved files after (days)"), QT_TRANSLATE_NOOP("Settings", "Auto-saved files older than this are deleted at startup. 0 keeps them forever."), T::Int, 0, 0, 3650});
    // ---- 控制 ----
    s.append({QStringLiteral("ocr.showResult"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "Show OCR result window"), QT_TRANSLATE_NOOP("Settings", "After recognizing text, open an editable result window besides copying the text."), T::Bool, true});
    s.append({QStringLiteral("ocr.language"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "OCR language"), QT_TRANSLATE_NOOP("Settings", "Language tag of an installed OCR pack (for example zh-Hans-CN or en-US); auto picks from your profile languages."), T::String, QStringLiteral("auto")});
    s.append({QStringLiteral("ocr.mode"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "OCR mode"), QT_TRANSLATE_NOOP("Settings", "Fast uses the image as is; accurate enlarges it first, which helps with small text but takes longer."), T::Enum, QStringLiteral("fast"), {}, {}, {QStringLiteral("fast"), QStringLiteral("accurate")}, {QT_TRANSLATE_NOOP("Settings", "Fast"), QT_TRANSLATE_NOOP("Settings", "Accurate")}});
    s.append({QStringLiteral("ocr.layout"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "OCR text layout"), QT_TRANSLATE_NOOP("Settings", "Auto joins the lines of a paragraph; no newlines puts everything on one line; multiline keeps one line per recognized line."), T::Enum, QStringLiteral("auto"), {}, {}, {QStringLiteral("auto"), QStringLiteral("noNewlines"), QStringLiteral("multiline")}, {QT_TRANSLATE_NOOP("Settings", "Automatic"), QT_TRANSLATE_NOOP("Settings", "No newlines"), QT_TRANSLATE_NOOP("Settings", "Multiline")}});
    s.append({QStringLiteral("ocr.punctuation"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "OCR punctuation"), QT_TRANSLATE_NOOP("Settings", "Keep as recognized, or convert to half-width / full-width."), T::Enum, QStringLiteral("keep"), {}, {}, {QStringLiteral("keep"), QStringLiteral("halfWidth"), QStringLiteral("fullWidth")}, {QT_TRANSLATE_NOOP("Settings", "Keep"), QT_TRANSLATE_NOOP("Settings", "Half-width"), QT_TRANSLATE_NOOP("Settings", "Full-width")}});
    s.append({QStringLiteral("ocr.pinSelectText"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "Selectable text on image pins"), QT_TRANSLATE_NOOP("Settings", "Recognize text on image pins so it can be selected, copied and dragged out; Alt+drag moves the pin instead."), T::Bool, true});
    s.append({QStringLiteral("capture.qrDetect"), QStringLiteral("recognition"), QT_TRANSLATE_NOOP("Settings", "QR / barcode detection while capturing"), QT_TRANSLATE_NOOP("Settings", "Off; manual (toolbar button); auto (a button appears when a code is found); show (the content opens right away)."), T::Enum, QStringLiteral("auto"), {}, {}, {QStringLiteral("off"), QStringLiteral("manual"), QStringLiteral("auto"), QStringLiteral("show")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "Manual"), QT_TRANSLATE_NOOP("Settings", "Automatic"), QT_TRANSLATE_NOOP("Settings", "Show content")}});
    s.append({QStringLiteral("capture.regionHistory"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Recent capture areas"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically; R loads the last area, Shift+R earlier ones."), T::String, QString()});
    s.append({QStringLiteral("capture.regionHistoryLimit"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Recent capture areas to keep"), QT_TRANSLATE_NOOP("Settings", "How many areas R / Shift+R can go back through."), T::Int, 10, 1, 100});
    s.append({QStringLiteral("capture.detectExcludeProcesses"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Skip element detection for"), QT_TRANSLATE_NOOP("Settings", "Process names, separated by commas. Only the window frame is detected for these programs."), T::String, QString()});
    s.append({QStringLiteral("capture.detectConservativeProcesses"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Conservative element detection for"), QT_TRANSLATE_NOOP("Settings", "Process names, separated by commas. These programs get a short timeout and no element hierarchy, which avoids freezes."), T::String, QString()});
    s.append({QStringLiteral("capture.sizeUnit"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Size unit"), QT_TRANSLATE_NOOP("Settings", "Show the selection size in physical or logical (scaled) pixels. Ctrl+U toggles while capturing."), T::Enum, QStringLiteral("physical"), {}, {}, {QStringLiteral("physical"), QStringLiteral("logical")}, {QT_TRANSLATE_NOOP("Settings", "Physical pixels"), QT_TRANSLATE_NOOP("Settings", "Logical pixels")}});
    s.append({QStringLiteral("capture.anchors"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Selection anchors"), QT_TRANSLATE_NOOP("Settings", "Handles drawn on the selection: none, the four corners, or corners and edges."), T::Enum, QStringLiteral("eight"), {}, {}, {QStringLiteral("none"), QStringLiteral("four"), QStringLiteral("eight")}, {QT_TRANSLATE_NOOP("Settings", "None"), QT_TRANSLATE_NOOP("Settings", "Four corners"), QT_TRANSLATE_NOOP("Settings", "Eight")}});
    s.append({QStringLiteral("capture.anchorHideBelow"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Hide anchors below (pixels)"), QT_TRANSLATE_NOOP("Settings", "Anchors are hidden when the selection is smaller than this on either side."), T::Int, 40, 0, 1000});
    s.append({QStringLiteral("capture.borderWidth"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Selection border width"), QT_TRANSLATE_NOOP("Settings", "In pixels."), T::Int, 1, 1, 10});
    s.append({QStringLiteral("capture.borderColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Selection border color"), QT_TRANSLATE_NOOP("Settings", "Empty uses the accent color."), T::Color, QString()});
    s.append({QStringLiteral("capture.maskColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Mask color"), QT_TRANSLATE_NOOP("Settings", "Color (with opacity) drawn over the screen outside the selection."), T::Color, QStringLiteral("#6e000000")});
    s.append({QStringLiteral("capture.detectAnimation"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Animate the detection frame"), QT_TRANSLATE_NOOP("Settings", "Slide the highlight smoothly from one window or element to the next."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierEnabled"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier"), QT_TRANSLATE_NOOP("Settings", "Off hides the magnifier completely, including with Alt."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierPixels"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier area (pixels)"), QT_TRANSLATE_NOOP("Settings", "Width of the magnified area in screen pixels (odd numbers keep the cursor centered)."), T::Int, 23, 5, 61});
    s.append({QStringLiteral("capture.magnifierGrid"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier grid"), QT_TRANSLATE_NOOP("Settings", "Draw a pixel grid in the magnifier."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierGridColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier grid color"), QT_TRANSLATE_NOOP("Settings", "Color (with opacity) of the grid lines."), T::Color, QStringLiteral("#28ffffff")});
    s.append({QStringLiteral("capture.magnifierHint"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier shortcut hint"), QT_TRANSLATE_NOOP("Settings", "Show the C / Shift hint line."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierCoords"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier coordinates"), QT_TRANSLATE_NOOP("Settings", "Show the cursor position and selection size."), T::Bool, true});
    s.append({QStringLiteral("capture.magnifierColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Magnifier color value"), QT_TRANSLATE_NOOP("Settings", "Show the color of the pixel under the cursor."), T::Bool, true});
    s.append({QStringLiteral("capture.guides"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Guide lines"), QT_TRANSLATE_NOOP("Settings", "When to show the guides: never, while Alt is held, or always."), T::Enum, QStringLiteral("alt"), {}, {}, {QStringLiteral("off"), QStringLiteral("alt"), QStringLiteral("always")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "While Alt is held"), QT_TRANSLATE_NOOP("Settings", "Always")}});
    s.append({QStringLiteral("capture.guideCrosshair"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Cursor crosshair"), QT_TRANSLATE_NOOP("Settings", "Full-screen lines through the cursor."), T::Bool, true});
    s.append({QStringLiteral("capture.guideCenterLines"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Selection center lines"), QT_TRANSLATE_NOOP("Settings", "Lines through the center of the selection."), T::Bool, true});
    s.append({QStringLiteral("capture.guideEdgeLines"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Selection edge lines"), QT_TRANSLATE_NOOP("Settings", "Extend the selection edges to the screen borders."), T::Bool, false});
    s.append({QStringLiteral("capture.historyIncludeCancelled"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Keep cancelled captures in history"), QT_TRANSLATE_NOOP("Settings", "Pressing Esc with a selection still records it, so a mistaken Esc loses nothing."), T::Bool, false});
    s.append({QStringLiteral("capture.historyDir"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "History folder"), QT_TRANSLATE_NOOP("Settings", "Also save every history entry as a file here and load them back on start. Empty keeps the history in memory only."), T::Directory, QString()});
    s.append({QStringLiteral("capture.hdrStrategy"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "HDR color correction"), QT_TRANSLATE_NOOP("Settings", "How HDR screens are converted to SDR captures: keep highlights (soft compression) or keep brightness (clip highlights)."), T::Enum, QStringLiteral("highlights"), {}, {}, {QStringLiteral("highlights"), QStringLiteral("brightness")}, {QT_TRANSLATE_NOOP("Settings", "Keep highlights"), QT_TRANSLATE_NOOP("Settings", "Keep brightness")}});
    s.append({QStringLiteral("hotkey.capture"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Capture hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that starts a capture."), T::KeySequence, QStringLiteral("F1")});
    s.append({QStringLiteral("hotkey.pin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that pins the clipboard."), T::KeySequence, QStringLiteral("F3")});
    s.append({QStringLiteral("hotkey.togglePins"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that hides or shows all pins."), T::KeySequence, QStringLiteral("Shift+F3")});
    s.append({QStringLiteral("hotkey.restorePin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that brings back the last closed pin."), T::KeySequence, QStringLiteral("Ctrl+3")});
    s.append({QStringLiteral("hotkey.togglePassthrough"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Click-through hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that toggles mouse click-through for the pin under the cursor; with no pin there, turns click-through off for all pins."), T::KeySequence, QStringLiteral("Ctrl+Shift+F3")});
    s.append({QStringLiteral("hotkey.nextGroup"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Next pin group hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that switches to the next pin group."), T::KeySequence, QStringLiteral("Ctrl+Shift+3")});
    s.append({QStringLiteral("hotkey.previousGroup"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Previous pin group hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that goes back to the previous pin group."), T::KeySequence, QString()});
    s.append({QStringLiteral("actions.list"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Actions"), QT_TRANSLATE_NOOP("Settings", "Custom actions (JSON). Edit them from the tray menu: Actions..."), T::String, QStringLiteral("[]")});
    s.append({QStringLiteral("hotkey.ignoreProcesses"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Ignore hotkeys when these programs are in front"), QT_TRANSLATE_NOOP("Settings", "Process names, separated by commas."), T::String, QString()});
    s.append({QStringLiteral("hotkey.allowSingleKey"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Allow single-key hotkeys"), QT_TRANSLATE_NOOP("Settings", "Let a plain letter, digit or symbol act as a global hotkey (function keys are always allowed)."), T::Bool, false});
    s.append({QStringLiteral("hotkey.toggleWindowTopmost"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Toggle window on top hotkey"), QT_TRANSLATE_NOOP("Settings", "Windows only: keep or release the window under the cursor on top."), T::KeySequence, QString()});
    s.append({QStringLiteral("hotkey.pasteAsImage"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Paste as image hotkey"), QT_TRANSLATE_NOOP("Settings", "Convert the clipboard to an image, then paste it into the active program."), T::KeySequence, QString()});
    s.append({QStringLiteral("hotkey.pasteAsText"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Paste as plain text hotkey"), QT_TRANSLATE_NOOP("Settings", "Strip formatting from the clipboard, then paste it into the active program."), T::KeySequence, QString()});
    s.append({QStringLiteral("shortcuts.map"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "In-app shortcuts"), QT_TRANSLATE_NOOP("Settings", "Overrides for the capture and pin shortcuts (JSON, {\"context.action\": \"Ctrl+X\"}). Contexts: capture, pin, pinImage, pinText."), T::String, QStringLiteral("{}")});
    s.append({QStringLiteral("hotkey.doubleTap"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Double-tap modifier"), QT_TRANSLATE_NOOP("Settings", "Tapping this modifier twice quickly triggers the action below (Windows only)."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("ctrl"), QStringLiteral("shift"), QStringLiteral("alt")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "Ctrl"), QT_TRANSLATE_NOOP("Settings", "Shift"), QT_TRANSLATE_NOOP("Settings", "Alt")}});
    s.append({QStringLiteral("hotkey.doubleTapMs"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Double-tap interval (ms)"), QT_TRANSLATE_NOOP("Settings", "Maximum time between the two taps."), T::Int, 300, 100, 1000});
    s.append({QStringLiteral("hotkey.doubleTapAction"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Double-tap action"), QT_TRANSLATE_NOOP("Settings", "What a modifier double-tap does."), T::Enum, QStringLiteral("capture"), {}, {}, {QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("togglePins")}, {QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins")}});
    s.append({QStringLiteral("hotkey.ignoreInjected"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Ignore simulated key presses"), QT_TRANSLATE_NOOP("Settings", "Hotkeys sent by macro tools or other programs are ignored (Windows only)."), T::Bool, true});
    s.append({QStringLiteral("globalMouse.enabled"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global mouse capture"), QT_TRANSLATE_NOOP("Settings", "Hold the modifier and drag with a mouse button over any program to capture that area (Windows only)."), T::Bool, false});
    s.append({QStringLiteral("globalMouse.modifier"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global mouse modifier"), QT_TRANSLATE_NOOP("Settings", "Key to hold while dragging."), T::Enum, QStringLiteral("win"), {}, {}, {QStringLiteral("win"), QStringLiteral("alt"), QStringLiteral("ctrl"), QStringLiteral("shift")}, {QT_TRANSLATE_NOOP("Settings", "Win"), QT_TRANSLATE_NOOP("Settings", "Alt"), QT_TRANSLATE_NOOP("Settings", "Ctrl"), QT_TRANSLATE_NOOP("Settings", "Shift")}});
    s.append({QStringLiteral("globalMouse.left"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global mouse: left button"), QT_TRANSLATE_NOOP("Settings", "What happens with the dragged area."), T::Enum, QStringLiteral("pin"), {}, {}, {QStringLiteral("pin"), QStringLiteral("copy"), QStringLiteral("ocrCopy"), QStringLiteral("save"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Pin"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Copy recognized text"), QT_TRANSLATE_NOOP("Settings", "Save"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("globalMouse.middle"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global mouse: middle button"), QT_TRANSLATE_NOOP("Settings", "What happens with the dragged area."), T::Enum, QStringLiteral("copy"), {}, {}, {QStringLiteral("pin"), QStringLiteral("copy"), QStringLiteral("ocrCopy"), QStringLiteral("save"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Pin"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Copy recognized text"), QT_TRANSLATE_NOOP("Settings", "Save"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("globalMouse.right"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Global mouse: right button"), QT_TRANSLATE_NOOP("Settings", "What happens with the dragged area."), T::Enum, QStringLiteral("ocrCopy"), {}, {}, {QStringLiteral("pin"), QStringLiteral("copy"), QStringLiteral("ocrCopy"), QStringLiteral("save"), QStringLiteral("none")}, {QT_TRANSLATE_NOOP("Settings", "Pin"), QT_TRANSLATE_NOOP("Settings", "Copy"), QT_TRANSLATE_NOOP("Settings", "Copy recognized text"), QT_TRANSLATE_NOOP("Settings", "Save"), QT_TRANSLATE_NOOP("Settings", "Nothing")}});
    s.append({QStringLiteral("hotCorner.dwellMs"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hot corner dwell (ms)"), QT_TRANSLATE_NOOP("Settings", "How long the cursor must rest in a corner."), T::Int, 300, 0, 3000});
    s.append({QStringLiteral("hotCorner.topLeft"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hot corner: top-left"), QT_TRANSLATE_NOOP("Settings", "Action when the cursor rests in this corner."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("togglePins"), QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("restorePin")}, {QT_TRANSLATE_NOOP("Settings", "Nothing"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins"), QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin")}});
    s.append({QStringLiteral("hotCorner.topRight"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hot corner: top-right"), QT_TRANSLATE_NOOP("Settings", "Action when the cursor rests in this corner."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("togglePins"), QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("restorePin")}, {QT_TRANSLATE_NOOP("Settings", "Nothing"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins"), QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin")}});
    s.append({QStringLiteral("hotCorner.bottomLeft"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hot corner: bottom-left"), QT_TRANSLATE_NOOP("Settings", "Action when the cursor rests in this corner."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("togglePins"), QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("restorePin")}, {QT_TRANSLATE_NOOP("Settings", "Nothing"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins"), QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin")}});
    s.append({QStringLiteral("hotCorner.bottomRight"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hot corner: bottom-right"), QT_TRANSLATE_NOOP("Settings", "Action when the cursor rests in this corner."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("togglePins"), QStringLiteral("capture"), QStringLiteral("pin"), QStringLiteral("restorePin")}, {QT_TRANSLATE_NOOP("Settings", "Nothing"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins"), QT_TRANSLATE_NOOP("Settings", "Capture"), QT_TRANSLATE_NOOP("Settings", "Pin clipboard"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin")}});
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

bool Settings::exportTo(const QString& path) const
{
    return save(path);
}

bool Settings::importFrom(const QString& path)
{
    Settings other;
    if (!other.load(path))
        return false;
    fromMap(other.toMap());
    return true;
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
