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
    s.append({QStringLiteral("general.runAsAdmin"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Run as administrator"), QT_TRANSLATE_NOOP("Settings", "Windows only: relaunch elevated so hotkeys also work over elevated programs. Autostart then uses a scheduled task."), T::Bool, false});
    s.append({QStringLiteral("ui.theme"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Theme"), QT_TRANSLATE_NOOP("Settings", "Light, dark or follow the system."), T::Enum, QStringLiteral("system"), {}, {}, {QStringLiteral("system"), QStringLiteral("light"), QStringLiteral("dark")}, {QT_TRANSLATE_NOOP("Settings", "Follow system"), QT_TRANSLATE_NOOP("Settings", "Light"), QT_TRANSLATE_NOOP("Settings", "Dark")}});
    s.append({QStringLiteral("ui.accentColor"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Accent color"), QT_TRANSLATE_NOOP("Settings", "Color of the selection border and the shadow of the active pin."), T::Color, QStringLiteral("#ff2878dc")});
    s.append({QStringLiteral("ui.font"), QStringLiteral("appearance"), QT_TRANSLATE_NOOP("Settings", "Interface font"), QT_TRANSLATE_NOOP("Settings", "Font family for the interface. Empty uses the system font."), T::String, QString()});
    s.append({QStringLiteral("general.processPriority"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Process priority"), QT_TRANSLATE_NOOP("Settings", "Windows only: a higher priority keeps captures responsive while other programs are busy."), T::Enum, QStringLiteral("normal"), {}, {}, {QStringLiteral("normal"), QStringLiteral("aboveNormal"), QStringLiteral("high")}, {QT_TRANSLATE_NOOP("Settings", "Normal"), QT_TRANSLATE_NOOP("Settings", "Above normal"), QT_TRANSLATE_NOOP("Settings", "High")}});
    s.append({QStringLiteral("advanced.animations"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Animations"), QT_TRANSLATE_NOOP("Settings", "Master switch for interface animations."), T::Bool, true});
    s.append({QStringLiteral("advanced.elementTimeoutMs"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Element detection timeout (ms)"), QT_TRANSLATE_NOOP("Settings", "Upper bound for one UI element query."), T::Int, 300, 50, 2000});
    s.append({QStringLiteral("general.logLevel"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Log level"), QT_TRANSLATE_NOOP("Settings", "Minimum level written to the log file."), T::Enum, QStringLiteral("info"), {}, {}, {QStringLiteral("debug"), QStringLiteral("info"), QStringLiteral("warning"), QStringLiteral("error")}, {QT_TRANSLATE_NOOP("Settings", "Debug"), QT_TRANSLATE_NOOP("Settings", "Info"), QT_TRANSLATE_NOOP("Settings", "Warning"), QT_TRANSLATE_NOOP("Settings", "Error")}});
    s.append({QStringLiteral("general.logMaxSizeKB"), QStringLiteral("general"), QT_TRANSLATE_NOOP("Settings", "Log file size (KB)"), QT_TRANSLATE_NOOP("Settings", "The log file is rotated when it grows past this size."), T::Int, 2048, 64, 1048576});
    s.append({QStringLiteral("general.preferencesGeometry"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "Settings window geometry"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically when the settings window closes."), T::String, QString()});
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
    s.append({QStringLiteral("annotation.toolbarCommon"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Toolbar tools"), QT_TRANSLATE_NOOP("Settings", "Tools shown directly on the toolbar, in order (rect, arrow, pencil, highlighter, mosaic, blur, text, eraser, step, magnifier, spotlight, smartErase). The rest go under More, together with less-used actions; the last tool used from there gets a quick button."), T::String, QStringLiteral("rect,arrow,pencil,mosaic,text,step")});
    s.append({QStringLiteral("annotation.rememberTool"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Remember the last tool"), QT_TRANSLATE_NOOP("Settings", "Captures and pins each remember the tool used last time."), T::Bool, false});
    s.append({QStringLiteral("annotation.lastToolCapture"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Last tool (capture)"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically."), T::String, QString()});
    s.append({QStringLiteral("annotation.lastToolPin"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Last tool (pin)"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically."), T::String, QString()});
    s.append({QStringLiteral("annotation.defaultCaptureTool"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Default capture tool"), QT_TRANSLATE_NOOP("Settings", "Tool activated as soon as an area is selected; none keeps the plain selection."), T::String, QStringLiteral("none")});
    s.append({QStringLiteral("annotation.arrowNote"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Double-click an arrow to add a note"), QT_TRANSLATE_NOOP("Settings", "Double-clicking a line or arrow opens a note editor at its end."), T::Bool, true});
    s.append({QStringLiteral("annotation.autoMosaic"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Automatic mosaic"), QT_TRANSLATE_NOOP("Settings", "After covering one target, cover every other place in the image with the same content; they are added and removed together."), T::Bool, false});
    s.append({QStringLiteral("annotation.spotlightOpacity"), QStringLiteral("annotation"), QT_TRANSLATE_NOOP("Settings", "Spotlight mask opacity"), QT_TRANSLATE_NOOP("Settings", "0 to 255."), T::Int, 150, 0, 255});
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
    s.append({QStringLiteral("capture.enterActions"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Actions on Enter"), QT_TRANSLATE_NOOP("Settings", "What happens when you confirm a capture: a comma-separated list of copy, pin, save, ocrCopy, executed in that fixed order."), T::String, QStringLiteral("copy")});
    s.append({QStringLiteral("capture.lastRegion"), QStringLiteral("capture"), QT_TRANSLATE_NOOP("Settings", "Last capture area"), QT_TRANSLATE_NOOP("Settings", "Remembered automatically; used by the \"last area\" quick capture."), T::String, QString()});
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
    // ---- 录屏（VID-01 ~ VID-08）----
    s.append({QStringLiteral("record.fps"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Frame rate"), QT_TRANSLATE_NOOP("Settings", "Frames captured per second."), T::Enum, QStringLiteral("30"), {}, {}, {QStringLiteral("5"), QStringLiteral("16"), QStringLiteral("24"), QStringLiteral("30"), QStringLiteral("60")}, {QT_TRANSLATE_NOOP("Settings", "5 fps"), QT_TRANSLATE_NOOP("Settings", "16 fps"), QT_TRANSLATE_NOOP("Settings", "24 fps"), QT_TRANSLATE_NOOP("Settings", "30 fps"), QT_TRANSLATE_NOOP("Settings", "60 fps")}});
    s.append({QStringLiteral("record.quality"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Quality"), QT_TRANSLATE_NOOP("Settings", "Light halves the size and compresses more; raw keeps every pixel."), T::Enum, QStringLiteral("standard"), {}, {}, {QStringLiteral("light"), QStringLiteral("standard"), QStringLiteral("raw")}, {QT_TRANSLATE_NOOP("Settings", "Light"), QT_TRANSLATE_NOOP("Settings", "Standard"), QT_TRANSLATE_NOOP("Settings", "Raw")}});
    s.append({QStringLiteral("record.mode"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Recording mode"), QT_TRANSLATE_NOOP("Settings", "Normal keeps the frames for editing and export; quick writes an MP4 while recording."), T::Enum, QStringLiteral("normal"), {}, {}, {QStringLiteral("normal"), QStringLiteral("quick")}, {QT_TRANSLATE_NOOP("Settings", "Normal (edit, then export)"), QT_TRANSLATE_NOOP("Settings", "Quick (MP4 while recording)")}});
    s.append({QStringLiteral("record.delaySec"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Start delay (seconds)"), QT_TRANSLATE_NOOP("Settings", "Countdown before recording starts."), T::Int, 0, 0, 10});
    s.append({QStringLiteral("record.mic"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Record microphone"), QT_TRANSLATE_NOOP("Settings", "Include the default microphone."), T::Bool, false});
    s.append({QStringLiteral("record.system"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Record system sound"), QT_TRANSLATE_NOOP("Settings", "Include what the computer plays."), T::Bool, false});
    s.append({QStringLiteral("record.micVolume"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Microphone volume (%)"), QT_TRANSLATE_NOOP("Settings", "Gain applied to the microphone track."), T::Int, 100, 0, 200});
    s.append({QStringLiteral("record.systemVolume"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "System sound volume (%)"), QT_TRANSLATE_NOOP("Settings", "Gain applied to the system sound track."), T::Int, 100, 0, 200});
    s.append({QStringLiteral("record.showClicks"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Show mouse clicks and wheel"), QT_TRANSLATE_NOOP("Settings", "Draw a ripple where the mouse is clicked and an arrow when the wheel turns."), T::Bool, true});
    s.append({QStringLiteral("record.showKeys"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Show pressed keys"), QT_TRANSLATE_NOOP("Settings", "Draw the keys pressed during the recording."), T::Bool, true});
    s.append({QStringLiteral("record.clickAnimMs"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Mouse animation length (ms)"), QT_TRANSLATE_NOOP("Settings", "How long the click ripple is shown."), T::Int, 500, 100, 2000});
    s.append({QStringLiteral("record.camera"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Camera picture-in-picture"), QT_TRANSLATE_NOOP("Settings", "Overlay the camera image on the recording."), T::Bool, false});
    s.append({QStringLiteral("record.cameraCorner"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Camera corner"), QT_TRANSLATE_NOOP("Settings", "Where the camera image is placed."), T::Enum, QStringLiteral("bottomRight"), {}, {}, {QStringLiteral("topLeft"), QStringLiteral("topRight"), QStringLiteral("bottomLeft"), QStringLiteral("bottomRight")}, {QT_TRANSLATE_NOOP("Settings", "Top left"), QT_TRANSLATE_NOOP("Settings", "Top right"), QT_TRANSLATE_NOOP("Settings", "Bottom left"), QT_TRANSLATE_NOOP("Settings", "Bottom right")}});
    s.append({QStringLiteral("record.cameraSize"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Camera size (% of width)"), QT_TRANSLATE_NOOP("Settings", "Width of the camera image relative to the recording."), T::Int, 25, 5, 60});
    s.append({QStringLiteral("record.format"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Export format"), QT_TRANSLATE_NOOP("Settings", "Default format in the playback window."), T::Enum, QStringLiteral("mp4"), {}, {}, {QStringLiteral("mp4"), QStringLiteral("gif"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "MP4"), QT_TRANSLATE_NOOP("Settings", "GIF"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("record.progress"), QStringLiteral("record"), QT_TRANSLATE_NOOP("Settings", "Progress overlay"), QT_TRANSLATE_NOOP("Settings", "Progress shown on the exported video."), T::Enum, QStringLiteral("none"), {}, {}, {QStringLiteral("none"), QStringLiteral("bar"), QStringLiteral("text"), QStringLiteral("countdown")}, {QT_TRANSLATE_NOOP("Settings", "None"), QT_TRANSLATE_NOOP("Settings", "Bar"), QT_TRANSLATE_NOOP("Settings", "Elapsed / total"), QT_TRANSLATE_NOOP("Settings", "Countdown")}});
    s.append({QStringLiteral("record.webpQuality"), QStringLiteral("hidden"), QT_TRANSLATE_NOOP("Settings", "WebP quality"), QT_TRANSLATE_NOOP("Settings", "0-100."), T::Int, 80, 0, 100});
    // ---- 输出 ----
    s.append({QStringLiteral("output.saveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save folder"), QT_TRANSLATE_NOOP("Settings", "Default folder for the save dialog."), T::Directory, QString()});
    s.append({QStringLiteral("output.saveFormat"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Save format"), QT_TRANSLATE_NOOP("Settings", "Format used by the save dialog."), T::Enum, QStringLiteral("png"), {}, {}, {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("bmp"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "BMP"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("output.fileNamePattern"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "File name pattern"), QT_TRANSLATE_NOOP("Settings", "Pattern for generated file names. Variables: {date} {time} {timestamp} {width} {height} {n} {title} {process} {uuid} {computer} {env:NAME}; modifiers |trunc:N |noext; / makes sub-folders."), T::String, QStringLiteral("Screenshot_{date}_{time}")});
    s.append({QStringLiteral("output.jpegQuality"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "JPEG / WebP quality"), QT_TRANSLATE_NOOP("Settings", "0 to 100."), T::Int, 90, 0, 100});
    s.append({QStringLiteral("output.copyAfterSave"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Copy after saving"), QT_TRANSLATE_NOOP("Settings", "Also put the image on the clipboard after it is saved."), T::Bool, false});
    s.append({QStringLiteral("output.metadata"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Write metadata"), QT_TRANSLATE_NOOP("Settings", "Store the source window, process and capture time in the image file (PNG / JPEG / WebP text fields)."), T::Bool, true});
    s.append({QStringLiteral("output.qualityAuto"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Automatic quality"), QT_TRANSLATE_NOOP("Settings", "Use a sensible default quality (JPEG 92, WebP 90) instead of the number above."), T::Bool, true});
    s.append({QStringLiteral("output.copyAsFile"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Copy as file"), QT_TRANSLATE_NOOP("Settings", "Also put an image file on the clipboard so it can be pasted into file managers, terminals and chat apps. Auto: only when the image has transparency."), T::Enum, QStringLiteral("off"), {}, {}, {QStringLiteral("off"), QStringLiteral("auto"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "Automatic"), QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
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
    s.append({QStringLiteral("hotkey.pin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that pins the clipboard; while capturing, pins the selection."), T::KeySequence, QStringLiteral("F3")});
    s.append({QStringLiteral("hotkey.togglePins"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that hides or shows all pins."), T::KeySequence, QStringLiteral("Shift+F3")});
    s.append({QStringLiteral("hotkey.restorePin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that brings back the last closed pin."), T::KeySequence, QStringLiteral("Ctrl+3")});
    s.append({QStringLiteral("hotkey.togglePassthrough"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Click-through hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that toggles mouse click-through for the pin under the cursor; with no pin there, turns click-through off for all pins."), T::KeySequence, QStringLiteral("Ctrl+Shift+F3")});
    s.append({QStringLiteral("shortcuts.map"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "In-app shortcuts"), QT_TRANSLATE_NOOP("Settings", "Overrides for the capture and pin shortcuts (JSON, {\"context.action\": \"Ctrl+X\"}). Contexts: capture, pin, pinImage, pinText."), T::String, QStringLiteral("{}")});
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
