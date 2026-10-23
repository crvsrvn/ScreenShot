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
    s.append({QStringLiteral("output.qualityAuto"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Automatic quality"), QT_TRANSLATE_NOOP("Settings", "Use a sensible default quality (JPEG 92, WebP 90) instead of the number above."), T::Bool, true});
    s.append({QStringLiteral("output.copyAsFile"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Copy as file"), QT_TRANSLATE_NOOP("Settings", "Also put an image file on the clipboard so it can be pasted into file managers, terminals and chat apps. Auto: only when the image has transparency."), T::Enum, QStringLiteral("off"), {}, {}, {QStringLiteral("off"), QStringLiteral("auto"), QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")}, {QT_TRANSLATE_NOOP("Settings", "Off"), QT_TRANSLATE_NOOP("Settings", "Automatic"), QT_TRANSLATE_NOOP("Settings", "PNG"), QT_TRANSLATE_NOOP("Settings", "JPEG"), QT_TRANSLATE_NOOP("Settings", "WebP")}});
    s.append({QStringLiteral("output.autoSave"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Auto save"), QT_TRANSLATE_NOOP("Settings", "Save an extra copy of every successful capture."), T::Bool, false});
    s.append({QStringLiteral("output.autoSaveDir"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Auto save folder"), QT_TRANSLATE_NOOP("Settings", "Folder for auto-saved captures. Empty means Pictures/ScreenShot."), T::Directory, QString()});
    s.append({QStringLiteral("output.autoSaveKeepDays"), QStringLiteral("output"), QT_TRANSLATE_NOOP("Settings", "Delete auto-saved files after (days)"), QT_TRANSLATE_NOOP("Settings", "Auto-saved files older than this are deleted at startup. 0 keeps them forever."), T::Int, 0, 0, 3650});
    // ---- 控制 ----
    s.append({QStringLiteral("hotkey.capture"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Capture hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that starts a capture."), T::KeySequence, QStringLiteral("F1")});
    s.append({QStringLiteral("hotkey.pin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that pins the clipboard."), T::KeySequence, QStringLiteral("F3")});
    s.append({QStringLiteral("hotkey.togglePins"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Hide / show pins hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that hides or shows all pins."), T::KeySequence, QStringLiteral("Shift+F3")});
    s.append({QStringLiteral("hotkey.restorePin"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Restore closed pin hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that brings back the last closed pin."), T::KeySequence, QStringLiteral("Ctrl+3")});
    s.append({QStringLiteral("hotkey.togglePassthrough"), QStringLiteral("control"), QT_TRANSLATE_NOOP("Settings", "Click-through hotkey"), QT_TRANSLATE_NOOP("Settings", "Global hotkey that toggles mouse click-through for the pin under the cursor; with no pin there, turns click-through off for all pins."), T::KeySequence, QStringLiteral("Ctrl+Shift+F3")});
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
