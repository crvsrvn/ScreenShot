// 内置测试接口的命令（请求 {"cmd": 名称, ...}，应答带 "ok"）：
//   ping                       → pid
//   screens                    → 当前捕获实现看到的显示器
//   useFakeScreens {screens:[{id,x,y,w,h,scale}]} / useRealScreens
//   capture {show?}            → 开始截图（相当于按下热键）
//   captureTimings             → triggerNs / captureDoneNs / shownNs / firstPaintNs / backend
//   setSelection {x,y,w,h}     → 设置物理像素选区
//   acceptCapture {path?}      → 裁剪、写 PNG、复制到剪贴板；返回宽高
//   cancelCapture
//   state                      → capturing / state / detectMode / selection / highlight / cursor / history / pins / overlays / hotkeys
//   mouse {action:press|move|release|dblclick, x, y, button?}   → 物理坐标鼠标事件直达会话
//   key {key:"Ctrl+S"|"Escape"|…, release?}                      → 键盘事件直达会话
//   hover {x,y}                → 悬停检测
//   currentImage {path}        → 把当前选区 / 历史图写成 PNG
//   overlayRender {screenId,path} → 把覆盖窗口内容渲染成 PNG
//   setting {key, value?}      → 读 / 写设置
//   setDialogOverride {path}   → 保存对话框直接用该路径
//   createPins {count,width,height} / dragPin {index,dx,dy,steps} / scalePin {index,steps,factor} / closePins
//   memory / benchBlur / benchMosaic / ocrSelfTest / elementAt {x,y,timeoutMs} / windows / screenshotDesktop {path}
//   runCommand {args:[...]}    → 等同命令行命令
//   quit
#include "app/testapi/TestCommands.h"

#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/OutputActions.h"
#include "app/OverlayWindow.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/ScrollCaptureSession.h"
#include "core/config/Settings.h"
#include "core/fx/Blur.h"
#include "core/fx/Mosaic.h"
#include "core/util/Clock.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QKeyEvent>
#include <QPainter>
#include <QPlainTextEdit>

#ifdef Q_OS_WIN
#include "platform/windows/GdiScreenCapture.h"
#include "platform/windows/WinProcess.h"
#include "platform/windows/WinScreens.h"
#include "platform/windows/WinWindowTools.h"
#endif

namespace ss {

namespace {

QJsonObject ok(QJsonObject o = {})
{
    o[QStringLiteral("ok")] = true;
    return o;
}

QJsonObject fail(const QString& error)
{
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QJsonObject rectToJson(const QRect& r)
{
    return QJsonObject{{QStringLiteral("x"), r.x()}, {QStringLiteral("y"), r.y()}, {QStringLiteral("w"), r.width()}, {QStringLiteral("h"), r.height()}};
}

QRect jsonToRect(const QJsonObject& o)
{
    return QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(), o.value(QStringLiteral("w")).toInt(), o.value(QStringLiteral("h")).toInt());
}

QJsonObject screenToJson(const ScreenInfo& s)
{
    QJsonObject o = rectToJson(s.physicalGeometry);
    o[QStringLiteral("id")] = s.id;
    o[QStringLiteral("scale")] = s.scale;
    o[QStringLiteral("primary")] = s.primary;
    return o;
}

QImage noiseImage(int w, int h)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    quint32 seed = 12345;
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            seed = seed * 1664525u + 1013904223u;
            line[x] = 0xFF000000u | (seed >> 8);
        }
    }
    return img;
}

QString stateName(CaptureSession::State s)
{
    switch (s) {
    case CaptureSession::State::Idle: return QStringLiteral("idle");
    case CaptureSession::State::Detecting: return QStringLiteral("detecting");
    case CaptureSession::State::Selecting: return QStringLiteral("selecting");
    case CaptureSession::State::Selected: return QStringLiteral("selected");
    case CaptureSession::State::History: return QStringLiteral("history");
    case CaptureSession::State::Annotating: return QStringLiteral("annotating");
    }
    return {};
}

QString detectModeName(CaptureSession::DetectMode m)
{
    switch (m) {
    case CaptureSession::DetectMode::Element: return QStringLiteral("element");
    case CaptureSession::DetectMode::Window: return QStringLiteral("window");
    case CaptureSession::DetectMode::Off: return QStringLiteral("off");
    }
    return {};
}

QJsonObject cmdCapture(App& app, const QJsonObject& req)
{
    const bool show = req.value(QStringLiteral("show")).toBool(true);
    if (!app.session().start(monotonicNs(), show))
        return fail(QStringLiteral("cannot start capture (already active or capture failed)"));
    return ok();
}

QJsonObject cmdCaptureTimings(App& app)
{
    const auto t = app.session().timings();
    QJsonObject o;
    o[QStringLiteral("triggerNs")] = static_cast<double>(t.triggerNs);
    o[QStringLiteral("captureDoneNs")] = static_cast<double>(t.captureDoneNs);
    o[QStringLiteral("shownNs")] = static_cast<double>(t.shownNs);
    o[QStringLiteral("firstPaintNs")] = static_cast<double>(t.firstPaintNs);
    o[QStringLiteral("complete")] = t.complete();
    o[QStringLiteral("captureMs")] = nsToMs(t.captureDoneNs - t.triggerNs);
    o[QStringLiteral("totalMs")] = t.complete() ? nsToMs(t.firstPaintNs - t.triggerNs) : -1.0;
    o[QStringLiteral("backend")] = app.session().screenCaptureBackend();
    return ok(o);
}

QJsonObject cmdAcceptCapture(App& app, const QJsonObject& req)
{
    if (!app.session().isActive())
        return fail(QStringLiteral("no active capture"));
    const QRect region = app.session().selection();
    const QImage image = app.session().accept();
    if (image.isNull())
        return fail(QStringLiteral("no selection"));
    const QString path = req.value(QStringLiteral("path")).toString();
    if (!path.isEmpty() && !image.save(path, "PNG"))
        return fail(QStringLiteral("save failed: ") + path);
    QJsonObject o = rectToJson(region);
    o[QStringLiteral("imageW")] = image.width();
    o[QStringLiteral("imageH")] = image.height();
    return ok(o);
}

QJsonObject cmdState(App& app)
{
    CaptureSession& s = app.session();
    QJsonObject o;
    o[QStringLiteral("capturing")] = s.isActive();
    o[QStringLiteral("state")] = stateName(s.state());
    o[QStringLiteral("detectMode")] = detectModeName(s.detectMode());
    o[QStringLiteral("selection")] = rectToJson(s.selection());
    o[QStringLiteral("highlight")] = s.highlight() ? rectToJson(*s.highlight()) : QJsonObject();
    o[QStringLiteral("cursor")] = QJsonObject{{QStringLiteral("x"), s.cursor().x()}, {QStringLiteral("y"), s.cursor().y()}};
    o[QStringLiteral("colorFormat")] = colorFormatName(s.colorFormat());
    o[QStringLiteral("colorText")] = s.colorTextAtCursor();
    o[QStringLiteral("magnifier")] = s.magnifierVisible();
    o[QStringLiteral("historySize")] = app.history().size();
    o[QStringLiteral("historyIndex")] = s.historyIndex();
    o[QStringLiteral("toolbarVisible")] = s.toolbar() && s.toolbar()->isVisible();
    o[QStringLiteral("pins")] = static_cast<int>(app.pins().size());
    o[QStringLiteral("pinsHidden")] = app.pinsHidden();
    o[QStringLiteral("clipboardHasImage")] = !QApplication::clipboard()->image().isNull();
    o[QStringLiteral("clipboardText")] = QApplication::clipboard()->text();
    QJsonObject hotkeys;
    for (auto it = app.hotkeyStatus().cbegin(); it != app.hotkeyStatus().cend(); ++it)
        hotkeys[QString::number(it.key())] = it.value();
    o[QStringLiteral("hotkeys")] = hotkeys;
    QJsonArray overlays;
    for (auto* w : s.overlays()) {
        QJsonObject ov = screenToJson(w->screenInfo());
        ov[QStringLiteral("visible")] = w->isVisible();
        ov[QStringLiteral("paintCount")] = w->paintCount();
        ov[QStringLiteral("logical")] = rectToJson(w->geometry());
#ifdef Q_OS_WIN
        ov[QStringLiteral("native")] = rectToJson(win::windowPhysicalRect(reinterpret_cast<void*>(w->winId())));
#endif
        overlays.append(ov);
    }
    o[QStringLiteral("overlays")] = overlays;
    return ok(o);
}

QJsonObject cmdMouse(App& app, const QJsonObject& req)
{
    const QString action = req.value(QStringLiteral("action")).toString();
    const QPoint p(req.value(QStringLiteral("x")).toInt(), req.value(QStringLiteral("y")).toInt());
    const QString b = req.value(QStringLiteral("button")).toString(QStringLiteral("left"));
    const Qt::MouseButton button = b == QLatin1String("right") ? Qt::RightButton : b == QLatin1String("middle") ? Qt::MiddleButton : Qt::LeftButton;
    CaptureSession& s = app.session();
    if (action == QLatin1String("press"))
        s.pressAt(p, button);
    else if (action == QLatin1String("move"))
        s.dragKind() == CaptureSession::Drag::None ? s.hoverAt(p) : s.dragTo(p);
    else if (action == QLatin1String("release"))
        s.releaseAt(p, button);
    else if (action == QLatin1String("dblclick"))
        s.doubleClickAt(p);
    else
        return fail(QStringLiteral("unknown mouse action"));
    QApplication::processEvents();
    return ok();
}

QJsonObject cmdKey(App& app, const QJsonObject& req)
{
    const QKeySequence seq(req.value(QStringLiteral("key")).toString());
    if (seq.isEmpty())
        return fail(QStringLiteral("bad key"));
    const QKeyCombination combo = seq[0];
    const bool release = req.value(QStringLiteral("release")).toBool(false);
    QKeyEvent press(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
    QKeyEvent rel(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
    bool handled = false;
    if (release)
        handled = app.session().handleKeyRelease(&rel);
    else
        handled = app.session().handleKeyPress(&press);
    QApplication::processEvents();
    return ok({{QStringLiteral("handled"), handled}});
}

QJsonObject cmdCreatePins(App& app, const QJsonObject& req)
{
    const int count = req.value(QStringLiteral("count")).toInt(1);
    const int w = req.value(QStringLiteral("width")).toInt(1920);
    const int h = req.value(QStringLiteral("height")).toInt(1080);
    const QImage base = noiseImage(w, h);
    for (int i = 0; i < count; ++i)
        app.pinImage(base, QPoint(40 + (i % 10) * 30, 40 + (i / 10) * 30));
    QApplication::processEvents();
    return ok({{QStringLiteral("count"), static_cast<int>(app.pins().size())}});
}

QJsonObject cmdDragPin(App& app, const QJsonObject& req)
{
    const int index = req.value(QStringLiteral("index")).toInt(0);
    if (index < 0 || index >= app.pins().size())
        return fail(QStringLiteral("bad pin index"));
    PinWindow* pin = app.pins().at(index);
    const int dx = req.value(QStringLiteral("dx")).toInt(2);
    const int dy = req.value(QStringLiteral("dy")).toInt(1);
    const int steps = req.value(QStringLiteral("steps")).toInt(100);
    const int paintsBefore = pin->paintCount();
    const qint64 t0 = monotonicNs();
    for (int i = 0; i < steps; ++i) {
        pin->moveContentTo(pin->contentTopLeft() + QPointF(dx, dy));
        QApplication::processEvents(QEventLoop::AllEvents);
    }
    const double elapsed = nsToMs(monotonicNs() - t0);
    return ok({{QStringLiteral("elapsedMs"), elapsed}, {QStringLiteral("steps"), steps},
               {QStringLiteral("movesPerSecond"), steps * 1000.0 / std::max(elapsed, 0.001)}, {QStringLiteral("paints"), pin->paintCount() - paintsBefore}});
}

QJsonObject cmdScalePin(App& app, const QJsonObject& req)
{
    const int index = req.value(QStringLiteral("index")).toInt(0);
    if (index < 0 || index >= app.pins().size())
        return fail(QStringLiteral("bad pin index"));
    PinWindow* pin = app.pins().at(index);
    const int steps = req.value(QStringLiteral("steps")).toInt(40);
    const double factor = req.value(QStringLiteral("factor")).toDouble(0.97);
    pin->takePaintDurationsMs();
    const int paintsBefore = pin->paintCount();
    const qint64 t0 = monotonicNs();
    for (int i = 0; i < steps; ++i) {
        pin->zoomAt(QPointF(pin->width() / 2.0, pin->height() / 2.0), factor, true);
        pin->repaint();
        QApplication::processEvents(QEventLoop::AllEvents);
    }
    const double elapsed = nsToMs(monotonicNs() - t0);
    QJsonArray frames;
    double maxFrame = 0;
    for (double d : pin->takePaintDurationsMs()) {
        frames.append(d);
        maxFrame = std::max(maxFrame, d);
    }
    return ok({{QStringLiteral("elapsedMs"), elapsed}, {QStringLiteral("steps"), steps}, {QStringLiteral("fps"), steps * 1000.0 / std::max(elapsed, 0.001)},
               {QStringLiteral("paints"), pin->paintCount() - paintsBefore}, {QStringLiteral("maxFrameMs"), maxFrame}, {QStringLiteral("frameMs"), frames},
               {QStringLiteral("scale"), pin->scale()}, {QStringLiteral("windowW"), pin->width()}, {QStringLiteral("windowH"), pin->height()},
               {QStringLiteral("clipped"), pin->isClipped()}});
}

QJsonObject benchImage(const QJsonObject& req, const std::function<QImage(const QImage&)>& fn, const QString& paramName, int param)
{
    const int w = req.value(QStringLiteral("width")).toInt(3840);
    const int h = req.value(QStringLiteral("height")).toInt(2160);
    const QImage src = noiseImage(w, h);
    fn(src);
    const int runs = 5;
    double best = 1e9, total = 0;
    for (int i = 0; i < runs; ++i) {
        const qint64 t0 = monotonicNs();
        const QImage out = fn(src);
        const double ms = nsToMs(monotonicNs() - t0);
        Q_UNUSED(out);
        best = std::min(best, ms);
        total += ms;
    }
    return ok({{QStringLiteral("bestMs"), best}, {QStringLiteral("avgMs"), total / runs}, {paramName, param}});
}

QJsonObject cmdOcrSelfTest(App& app)
{
    if (!app.platform().ocr)
        return fail(QStringLiteral("no OCR implementation"));
    QImage img(800, 240, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setPen(Qt::black);
        QFont f = p.font();
        f.setPointSize(28);
        p.setFont(f);
        p.drawText(QRect(20, 20, 760, 90), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("ScreenShot OCR test 12345"));
        p.drawText(QRect(20, 120, 760, 90), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("截图工具 本地识别 测试"));
    }
    const qint64 t0 = monotonicNs();
    const OcrResult r = app.platform().ocr->recognize(img);
    QJsonObject o;
    o[QStringLiteral("available")] = app.platform().ocr->available();
    o[QStringLiteral("text")] = r.text();
    o[QStringLiteral("error")] = r.error;
    o[QStringLiteral("elapsedMs")] = nsToMs(monotonicNs() - t0);
    o[QStringLiteral("lines")] = static_cast<int>(r.lines.size());
    o[QStringLiteral("ok")] = r.ok;
    return o;
}

QJsonObject cmdElementAt(App& app, const QJsonObject& req)
{
    if (!app.platform().elementDetector)
        return fail(QStringLiteral("no element detector"));
    const QPoint pt(req.value(QStringLiteral("x")).toInt(), req.value(QStringLiteral("y")).toInt());
    const int timeoutMs = req.value(QStringLiteral("timeoutMs")).toInt(300);
    const qint64 t0 = monotonicNs();
    const auto r = app.platform().elementDetector->elementAt(pt, std::chrono::milliseconds(timeoutMs));
    QJsonObject o;
    o[QStringLiteral("elapsedMs")] = nsToMs(monotonicNs() - t0);
    o[QStringLiteral("found")] = r.has_value();
    if (r)
        o[QStringLiteral("rect")] = rectToJson(*r);
    return ok(o);
}

QJsonObject cmdWindows(App& app)
{
    if (!app.platform().windowDetector)
        return fail(QStringLiteral("no window detector"));
    QJsonArray arr;
    for (const auto& w : app.platform().windowDetector->topLevelWindows()) {
        QJsonObject o = rectToJson(w.frame);
        o[QStringLiteral("title")] = w.title;
        o[QStringLiteral("process")] = w.processName;
        o[QStringLiteral("z")] = w.zOrder;
        arr.append(o);
    }
    return ok({{QStringLiteral("windows"), arr}});
}

QJsonObject cmdUseFakeScreens(App& app, const QJsonObject& req)
{
    QList<ScreenInfo> screens;
    for (const auto& v : req.value(QStringLiteral("screens")).toArray()) {
        const QJsonObject o = v.toObject();
        ScreenInfo s;
        s.id = o.value(QStringLiteral("id")).toString(QStringLiteral("FAKE-%1").arg(screens.size()));
        s.physicalGeometry = jsonToRect(o);
        s.scale = o.value(QStringLiteral("scale")).toDouble(1.0);
        s.primary = screens.isEmpty();
        screens.append(s);
    }
    if (screens.isEmpty())
        return fail(QStringLiteral("no screens"));
    app.useScreenCapture(std::make_unique<testing::FakeScreenCapture>(screens));
    app.session().prepareOverlays();
    return ok();
}

QJsonObject cmdOverlayRender(App& app, const QJsonObject& req)
{
    const QString id = req.value(QStringLiteral("screenId")).toString();
    const QString path = req.value(QStringLiteral("path")).toString();
    for (auto* w : app.session().overlays()) {
        if (w->screenInfo().id == id) {
            const QImage img = w->renderToImage();
            if (!img.save(path, "PNG"))
                return fail(QStringLiteral("save failed"));
            return ok({{QStringLiteral("w"), img.width()}, {QStringLiteral("h"), img.height()}});
        }
    }
    return fail(QStringLiteral("no overlay for screen"));
}

QJsonObject cmdSetting(App& app, const QJsonObject& req)
{
    const QString key = req.value(QStringLiteral("key")).toString();
    if (!settingSpec(key) && !req.contains(QStringLiteral("value")))
        return fail(QStringLiteral("unknown setting"));
    if (req.contains(QStringLiteral("value")))
        app.settings().setValue(key, req.value(QStringLiteral("value")).toVariant());
    return ok({{QStringLiteral("value"), QJsonValue::fromVariant(app.settings().value(key))}});
}

} // namespace

QJsonObject handleTestCommand(App& app, const QJsonObject& req)
{
    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    CaptureSession& session = app.session();
    if (cmd == QStringLiteral("ping"))
        return ok({{QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid())}});
    if (cmd == QStringLiteral("screens")) {
        QJsonArray arr;
        if (session.screenCapture())
            for (const auto& s : session.screenCapture()->screens())
                arr.append(screenToJson(s));
        return ok({{QStringLiteral("screens"), arr}});
    }
    if (cmd == QStringLiteral("useFakeScreens"))
        return cmdUseFakeScreens(app, req);
    if (cmd == QStringLiteral("useRealScreens")) {
        app.useScreenCapture(nullptr);
        session.prepareOverlays();
        return ok();
    }
    if (cmd == QStringLiteral("capture"))
        return cmdCapture(app, req);
    if (cmd == QStringLiteral("captureTimings"))
        return cmdCaptureTimings(app);
    if (cmd == QStringLiteral("setSelection")) {
        if (!session.isActive())
            return fail(QStringLiteral("no active capture"));
        session.setSelection(jsonToRect(req));
        return ok();
    }
    if (cmd == QStringLiteral("acceptCapture"))
        return cmdAcceptCapture(app, req);
    if (cmd == QStringLiteral("cancelCapture")) {
        session.cancel();
        return ok();
    }
    if (cmd == QStringLiteral("state"))
        return cmdState(app);
    if (cmd == QStringLiteral("mouse"))
        return cmdMouse(app, req);
    if (cmd == QStringLiteral("key"))
        return cmdKey(app, req);
    if (cmd == QStringLiteral("hover")) {
        session.hoverAt(QPoint(req.value(QStringLiteral("x")).toInt(), req.value(QStringLiteral("y")).toInt()));
        QApplication::processEvents();
        return ok();
    }
    if (cmd == QStringLiteral("currentImage")) {
        const QImage img = session.currentImage();
        if (img.isNull())
            return fail(QStringLiteral("no image"));
        if (!img.save(req.value(QStringLiteral("path")).toString(), "PNG"))
            return fail(QStringLiteral("save failed"));
        return ok({{QStringLiteral("w"), img.width()}, {QStringLiteral("h"), img.height()}});
    }
    if (cmd == QStringLiteral("overlayRender"))
        return cmdOverlayRender(app, req);
    if (cmd == QStringLiteral("setting"))
        return cmdSetting(app, req);
    if (cmd == QStringLiteral("setDialogOverride")) {
        app.output().setDialogOverride(req.value(QStringLiteral("path")).toString());
        return ok();
    }
    if (cmd == QStringLiteral("createPins"))
        return cmdCreatePins(app, req);
    if (cmd == QStringLiteral("dragPin"))
        return cmdDragPin(app, req);
    if (cmd == QStringLiteral("scalePin"))
        return cmdScalePin(app, req);
    if (cmd == QStringLiteral("closePins")) {
        app.closeAllPins();
        return ok();
    }
    if (cmd == QStringLiteral("pins")) {
        QJsonArray arr;
        for (auto* p : app.pins()) {
            QJsonObject o = rectToJson(p->physicalContentRect());
            o[QStringLiteral("scale")] = p->scale();
            o[QStringLiteral("opacity")] = p->opacity();
            o[QStringLiteral("rotation")] = p->rotation();
            o[QStringLiteral("flipH")] = p->flippedH();
            o[QStringLiteral("flipV")] = p->flippedV();
            o[QStringLiteral("passthrough")] = p->passthrough();
            o[QStringLiteral("visible")] = p->isVisible();
            o[QStringLiteral("annotating")] = p->annotating();
            o[QStringLiteral("gray")] = p->grayscale();
            o[QStringLiteral("invert")] = p->inverted();
            o[QStringLiteral("thumbnail")] = p->thumbnail();
            o[QStringLiteral("topmost")] = p->topmost();
            o[QStringLiteral("selected")] = p->selected();
            o[QStringLiteral("kind")] = static_cast<int>(p->contentKind());
            o[QStringLiteral("cropped")] = !p->cropRect().isNull();
            o[QStringLiteral("imageW")] = p->originalImage().width();
            o[QStringLiteral("imageH")] = p->originalImage().height();
#ifdef Q_OS_WIN
            o[QStringLiteral("native")] = rectToJson(win::windowPhysicalRect(reinterpret_cast<void*>(p->winId())));
#endif
            arr.append(o);
        }
        return ok({{QStringLiteral("pins"), arr}, {QStringLiteral("closed"), static_cast<int>(app.pinManager().closedPins().size())}, {QStringLiteral("hidden"), app.pinsHidden()}});
    }
    if (cmd == QStringLiteral("pinGroups")) {
        // {action: new|switch|next|previous|clear, name}
        PinManager& m = app.pinManager();
        const QString action = req.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("new"))
            m.newGroup(req.value(QStringLiteral("name")).toString());
        else if (action == QLatin1String("switch"))
            m.switchToGroupNamed(req.value(QStringLiteral("name")).toString());
        else if (action == QLatin1String("next"))
            m.nextGroup();
        else if (action == QLatin1String("previous"))
            m.previousGroup();
        else if (action == QLatin1String("clear"))
            m.clearCurrentGroup();
        return ok({{QStringLiteral("groups"), QJsonArray::fromStringList(m.groupNames())}, {QStringLiteral("current"), m.currentGroupIndex()}, {QStringLiteral("pins"), static_cast<int>(m.pins().size())}});
    }
    if (cmd == QStringLiteral("pinAction")) {
        const int index = req.value(QStringLiteral("index")).toInt(0);
        const QString action = req.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("restore")) {
            app.restoreClosedPin();
            return ok({{QStringLiteral("pins"), static_cast<int>(app.pins().size())}});
        }
        if (action == QLatin1String("toggleHidden")) {
            app.togglePinsHidden();
            return ok();
        }
        if (action == QLatin1String("passthroughAt")) {
            app.pinManager().togglePassthroughAt(QPoint(req.value(QStringLiteral("x")).toInt(), req.value(QStringLiteral("y")).toInt()));
            return ok();
        }
        if (action == QLatin1String("clipboardImage")) {
            // 把合成图像放进剪贴板后按贴图热键语义贴出
            QApplication::clipboard()->setImage(noiseImage(req.value(QStringLiteral("width")).toInt(300), req.value(QStringLiteral("height")).toInt(200)));
            app.pinManager().markClipboardDirty();
            PinWindow* pin = app.pinManager().pinHotkey(QPoint(req.value(QStringLiteral("x")).toInt(100), req.value(QStringLiteral("y")).toInt(100)));
            return pin ? ok() : fail(QStringLiteral("nothing pinned"));
        }
        if (action == QLatin1String("text")) {
            PinWindow* pin = app.pinManager().pinText(req.value(QStringLiteral("text")).toString(), QPoint(req.value(QStringLiteral("x")).toInt(100), req.value(QStringLiteral("y")).toInt(100)));
            return pin ? ok({{QStringLiteral("w"), pin->originalImage().width()}, {QStringLiteral("h"), pin->originalImage().height()}}) : fail(QStringLiteral("nothing pinned"));
        }
        if (index < 0 || index >= app.pins().size())
            return fail(QStringLiteral("bad pin index"));
        PinWindow* pin = app.pins().at(index);
        if (action == QLatin1String("zoom"))
            pin->zoomStep(req.value(QStringLiteral("steps")).toInt(1), QPointF(pin->width() / 2.0, pin->height() / 2.0));
        else if (action == QLatin1String("opacity"))
            pin->adjustOpacity(req.value(QStringLiteral("delta")).toInt(-10));
        else if (action == QLatin1String("rotate"))
            pin->rotateQuarter(req.value(QStringLiteral("turns")).toInt(1));
        else if (action == QLatin1String("flipH"))
            pin->flipHorizontal();
        else if (action == QLatin1String("flipV"))
            pin->flipVertical();
        else if (action == QLatin1String("reset"))
            pin->toggleReset();
        else if (action == QLatin1String("gray"))
            pin->setGrayscale(!pin->grayscale());
        else if (action == QLatin1String("invert"))
            pin->setInverted(!pin->inverted());
        else if (action == QLatin1String("thumbnail"))
            pin->toggleThumbnail();
        else if (action == QLatin1String("topmost"))
            pin->setTopmost(!pin->topmost());
        else if (action == QLatin1String("copyText"))
            pin->copyText();
        else if (action == QLatin1String("crop"))
            pin->cropTo(QRectF(req.value(QStringLiteral("x")).toDouble(), req.value(QStringLiteral("y")).toDouble(), req.value(QStringLiteral("w")).toDouble(), req.value(QStringLiteral("h")).toDouble()));
        else if (action == QLatin1String("restoreCrop"))
            pin->restoreCrop();
        else if (action == QLatin1String("confirmCrop"))
            pin->confirmCrop();
        else if (action == QLatin1String("view"))
            pin->setViewRect(QRectF(req.value(QStringLiteral("x")).toDouble(), req.value(QStringLiteral("y")).toDouble(), req.value(QStringLiteral("w")).toDouble(), req.value(QStringLiteral("h")).toDouble()));
        else if (action == QLatin1String("clearView"))
            pin->clearViewRect();
        else if (action == QLatin1String("select"))
            app.pinManager().toggleSelected(pin);
        else if (action == QLatin1String("selectAll"))
            app.pinManager().selectAllVisible();
        else if (action == QLatin1String("replaceClipboard"))
            app.pinManager().replacePin(pin, QApplication::clipboard()->mimeData());
        else if (action == QLatin1String("passthrough"))
            pin->setPassthrough(req.value(QStringLiteral("on")).toBool(true));
        else if (action == QLatin1String("close"))
            pin->closePin();
        else if (action == QLatin1String("destroy"))
            pin->destroyPin();
        else if (action == QLatin1String("toolbar"))
            pin->toggleToolbar();
        else if (action == QLatin1String("move"))
            pin->moveToPhysical(QPoint(req.value(QStringLiteral("x")).toInt(), req.value(QStringLiteral("y")).toInt()));
        else if (action == QLatin1String("copy"))
            pin->copyDisplayed();
        else if (action == QLatin1String("key")) {
            const QKeyCombination c = QKeySequence(req.value(QStringLiteral("key")).toString())[0];
            QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
            QApplication::sendEvent(pin, &e);
        } else if (action == QLatin1String("savePng")) {
            if (!pin->displayedImage().save(req.value(QStringLiteral("path")).toString(), "PNG"))
                return fail(QStringLiteral("save failed"));
        } else
            return fail(QStringLiteral("unknown pin action"));
        QApplication::processEvents();
        return ok();
    }
    if (cmd == QStringLiteral("memory")) {
#ifdef Q_OS_WIN
        return ok({{QStringLiteral("privateBytes"), static_cast<double>(win::privateBytes())}});
#else
        return fail(QStringLiteral("not implemented"));
#endif
    }
    if (cmd == QStringLiteral("benchBlur")) {
        const int radius = req.value(QStringLiteral("radius")).toInt(10);
        return benchImage(req, [radius](const QImage& i) { return fx::gaussianBlur(i, radius); }, QStringLiteral("radius"), radius);
    }
    if (cmd == QStringLiteral("benchMosaic")) {
        const int block = req.value(QStringLiteral("block")).toInt(12);
        return benchImage(req, [block](const QImage& i) { return fx::mosaic(i, block); }, QStringLiteral("block"), block);
    }
    if (cmd == QStringLiteral("ocrSelfTest"))
        return cmdOcrSelfTest(app);
    if (cmd == QStringLiteral("elementAt"))
        return cmdElementAt(app, req);
    if (cmd == QStringLiteral("windows"))
        return cmdWindows(app);
    if (cmd == QStringLiteral("screenshotDesktop")) {
#ifdef Q_OS_WIN
        QRect bounds;
        for (const auto& s : win::enumerateScreens())
            bounds = bounds.united(s.physicalGeometry);
        const QImage img = win::GdiScreenCapture::captureRect(bounds);
        const QString path = req.value(QStringLiteral("path")).toString();
        if (img.isNull() || !img.save(path, "PNG"))
            return fail(QStringLiteral("capture or save failed"));
        QJsonObject o = rectToJson(bounds);
        o[QStringLiteral("path")] = path;
        return ok(o);
#else
        return fail(QStringLiteral("not implemented"));
#endif
    }
    if (cmd == QStringLiteral("annotate")) {
        // 选择标注工具（进入标注状态）；{"tool":"rect"|...|"none"}
        session.setTool(CaptureToolbar::toolFromName(req.value(QStringLiteral("tool")).toString()));
        QApplication::processEvents();
        return ok();
    }
    if (cmd == QStringLiteral("annotateState")) {
        AnnotationEditor& e = session.editor();
        QJsonObject o;
        o[QStringLiteral("active")] = e.active();
        o[QStringLiteral("tool")] = CaptureToolbar::toolName(e.tool());
        o[QStringLiteral("count")] = e.document().count();
        o[QStringLiteral("selectedId")] = e.selectedId();
        o[QStringLiteral("hasDraft")] = e.hasDraft();
        o[QStringLiteral("textEditing")] = e.textEditing();
        o[QStringLiteral("canUndo")] = e.document().canUndo();
        o[QStringLiteral("canRedo")] = e.document().canRedo();
        o[QStringLiteral("color")] = e.style().stroke.name(QColor::HexArgb);
        o[QStringLiteral("width")] = e.style().width;
        o[QStringLiteral("penStyle")] = e.style().penStyle == Qt::DashLine ? QStringLiteral("dash") : e.style().penStyle == Qt::DotLine ? QStringLiteral("dot") : QStringLiteral("solid");
        o[QStringLiteral("radius")] = e.cornerRadius();
        QJsonArray types;
        for (int i = 0; i < e.document().count(); ++i)
            types.append(e.document().at(i)->type());
        o[QStringLiteral("types")] = types;
        return ok(o);
    }
    if (cmd == QStringLiteral("annotateStyle")) {
        AnnotationEditor& e = session.editor();
        if (req.contains(QStringLiteral("color")))
            e.setColor(QColor::fromString(req.value(QStringLiteral("color")).toString()));
        if (req.contains(QStringLiteral("width")))
            e.setWidth(req.value(QStringLiteral("width")).toInt());
        if (req.contains(QStringLiteral("filled")))
            e.setFilled(req.value(QStringLiteral("filled")).toBool());
        if (req.contains(QStringLiteral("strength")))
            e.setEffectStrength(req.value(QStringLiteral("strength")).toInt());
        if (req.contains(QStringLiteral("penStyle"))) {
            const QString ps = req.value(QStringLiteral("penStyle")).toString();
            e.setPenStyle(ps == QLatin1String("dash") ? Qt::DashLine : ps == QLatin1String("dot") ? Qt::DotLine : Qt::SolidLine);
        }
        if (req.contains(QStringLiteral("radius")))
            e.setCornerRadius(req.value(QStringLiteral("radius")).toDouble());
        if (req.contains(QStringLiteral("arrowStart")) || req.contains(QStringLiteral("arrowEnd")))
            e.setArrowHeads(LineAnnotation::headFromName(req.value(QStringLiteral("arrowStart")).toString(LineAnnotation::headName(e.arrowHeadStart()))),
                            LineAnnotation::headFromName(req.value(QStringLiteral("arrowEnd")).toString(LineAnnotation::headName(e.arrowHeadEnd()))));
        if (req.contains(QStringLiteral("zoom")))
            e.setMagnifierZoom(req.value(QStringLiteral("zoom")).toDouble());
        if (req.contains(QStringLiteral("stepValue")))
            e.setStepNumber(req.value(QStringLiteral("stepValue")).toInt());
        if (req.contains(QStringLiteral("outline")) || req.contains(QStringLiteral("background")) || req.contains(QStringLiteral("textShadow"))) {
            TextDecor d = e.textDecor();
            d.outline = req.value(QStringLiteral("outline")).toBool(d.outline);
            d.background = req.value(QStringLiteral("background")).toBool(d.background);
            d.shadow = req.value(QStringLiteral("textShadow")).toBool(d.shadow);
            e.setTextDecor(d);
        }
        return ok();
    }
    if (cmd == QStringLiteral("annotateText")) {
        // 把文字写进正在编辑的输入框并提交
        AnnotationEditor& e = session.editor();
        if (!e.textEditing())
            return fail(QStringLiteral("no text input active"));
        if (auto* edit = session.overlays().isEmpty() ? nullptr : session.overlays().first()->findChild<QPlainTextEdit*>())
            edit->setPlainText(req.value(QStringLiteral("text")).toString());
        for (auto* w : session.overlays())
            if (auto* edit = w->findChild<QPlainTextEdit*>())
                edit->setPlainText(req.value(QStringLiteral("text")).toString());
        e.commitText();
        QApplication::processEvents();
        return ok({{QStringLiteral("count"), e.document().count()}});
    }
    if (cmd == QStringLiteral("scrollStart")) {
        const bool ok = app.startScroll(jsonToRect(req), req.value(QStringLiteral("show")).toBool(true));
        if (ok && req.contains(QStringLiteral("auto")))
            app.scrollSession()->setAutoScroll(req.value(QStringLiteral("auto")).toBool());
        return ok ? ::ss::ok() : fail(app.scrollError());
    }
    if (cmd == QStringLiteral("scrollTick")) {
        if (!app.scrollSession())
            return fail(QStringLiteral("no scroll session"));
        const int n = req.value(QStringLiteral("count")).toInt(1);
        for (int i = 0; i < n; ++i)
            app.scrollSession()->tick();
        QApplication::processEvents();
        return ::ss::ok();
    }
    if (cmd == QStringLiteral("scrollState")) {
        ScrollCaptureSession* sc = app.scrollSession();
        if (!sc)
            return ::ss::ok({{QStringLiteral("active"), false}});
        return ::ss::ok({{QStringLiteral("active"), true}, {QStringLiteral("state"), static_cast<int>(sc->state())}, {QStringLiteral("height"), sc->stitchedHeight()},
                         {QStringLiteral("frames"), sc->frameCount()}, {QStringLiteral("lastOk"), sc->lastMatchOk()}, {QStringLiteral("reason"), sc->lastReason()},
                         {QStringLiteral("reachedEnd"), sc->reachedEnd()}, {QStringLiteral("rejected"), sc->rejectedFrames()}, {QStringLiteral("autoScroll"), sc->autoScroll()}});
    }
    if (cmd == QStringLiteral("scrollFinish")) {
        if (!app.scrollSession())
            return fail(QStringLiteral("no scroll session"));
        const QImage img = app.scrollSession()->finish();
        const QString path = req.value(QStringLiteral("path")).toString();
        if (!path.isEmpty() && !img.save(path, "PNG"))
            return fail(QStringLiteral("save failed"));
        return ::ss::ok({{QStringLiteral("w"), img.width()}, {QStringLiteral("h"), img.height()}});
    }
    if (cmd == QStringLiteral("scrollCancel")) {
        if (app.scrollSession())
            app.scrollSession()->cancel();
        QApplication::processEvents();
        return ::ss::ok();
    }
    if (cmd == QStringLiteral("quickCapture"))
        return app.quickCapture(req.value(QStringLiteral("kind")).toString()) ? ::ss::ok() : fail(QStringLiteral("quick capture failed"));
    if (cmd == QStringLiteral("whiteboard")) {
        app.startWhiteboard();
        return ::ss::ok({{QStringLiteral("state"), stateName(session.state())}});
    }
    if (cmd == QStringLiteral("cursorToggle")) {
        session.setIncludeCursor(!session.includeCursor());
        return ::ss::ok({{QStringLiteral("includeCursor"), session.includeCursor()}});
    }
    if (cmd == QStringLiteral("refresh"))
        return session.refresh() ? ::ss::ok() : fail(QStringLiteral("refresh failed"));
    if (cmd == QStringLiteral("finalImage")) {
        const QImage img = session.finalImage();
        if (img.isNull() || !img.save(req.value(QStringLiteral("path")).toString(), "PNG"))
            return fail(QStringLiteral("no image"));
        return ::ss::ok({{QStringLiteral("w"), img.width()}, {QStringLiteral("h"), img.height()}});
    }
    if (cmd == QStringLiteral("runCommand")) {
        QStringList args;
        for (const auto& v : req.value(QStringLiteral("args")).toArray())
            args << v.toString();
        app.runCommand(args);
        QApplication::processEvents();
        return ok();
    }
    if (cmd == QStringLiteral("quit")) {
        QMetaObject::invokeMethod(&app, &App::quit, Qt::QueuedConnection);
        return ok();
    }
    return fail(QStringLiteral("unknown command: ") + cmd);
}

} // namespace ss
