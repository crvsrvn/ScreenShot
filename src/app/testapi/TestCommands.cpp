// 内置测试接口的命令（请求 {"cmd": 名称, ...}，应答带 "ok"）：
//   ping                       → pid
//   screens                    → 当前捕获实现看到的显示器
//   useFakeScreens {screens:[{id,x,y,w,h,scale}]} / useRealScreens
//   capture {show?}            → 开始截图（相当于按下热键）
//   captureTimings             → triggerNs / captureDoneNs / shownNs / firstPaintNs / backend
//   setSelection {x,y,w,h}     → 设置物理像素选区
//   acceptCapture {path?}      → 裁剪、写 PNG、复制到剪贴板；返回宽高
//   cancelCapture
//   state                      → capturing / selection / pins / overlays
//   overlayRender {screenId,path} → 把覆盖窗口内容渲染成 PNG
//   createPins {count,width,height} → 创建贴图
//   dragPin {index,dx,dy,steps} → 逐步移动并计时
//   scalePin {index,steps,factor} → 逐步缩放并记录每帧耗时
//   closePins
//   memory                     → privateBytes
//   benchBlur {width,height,radius} / benchMosaic {width,height,block}
//   ocrSelfTest                → 用渲染出的文字图测 OCR
//   elementAt {x,y,timeoutMs}  → UI 元素检测
//   windows                    → 顶层窗口列表
//   quit
#include "app/testapi/TestCommands.h"

#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/OverlayWindow.h"
#include "app/PinWindow.h"
#include "core/fx/Blur.h"
#include "core/fx/Mosaic.h"
#include "core/util/Clock.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QPainter>

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
    return QRect(o.value(QStringLiteral("x")).toInt(), o.value(QStringLiteral("y")).toInt(), o.value(QStringLiteral("w")).toInt(),
                 o.value(QStringLiteral("h")).toInt());
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

QJsonObject cmdCapture(App& app, const QJsonObject& req)
{
    const bool show = req.value(QStringLiteral("show")).toBool(true);
    if (!app.session().start(monotonicNs(), show))
        return fail(QStringLiteral("无法开始截图（已在截图中或捕获失败）"));
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
        return fail(QStringLiteral("没有进行中的截图"));
    const QRect region = app.session().selection();
    const QImage image = app.session().accept();
    if (image.isNull())
        return fail(QStringLiteral("没有选区"));
    const QString path = req.value(QStringLiteral("path")).toString();
    if (!path.isEmpty() && !image.save(path, "PNG"))
        return fail(QStringLiteral("保存失败：") + path);
    QJsonObject o = rectToJson(region);
    o[QStringLiteral("imageW")] = image.width();
    o[QStringLiteral("imageH")] = image.height();
    return ok(o);
}

QJsonObject cmdState(App& app)
{
    QJsonObject o;
    o[QStringLiteral("capturing")] = app.session().isActive();
    o[QStringLiteral("selection")] = rectToJson(app.session().selection());
    o[QStringLiteral("pins")] = static_cast<int>(app.pins().size());
    QJsonObject hotkeys;
    for (auto it = app.hotkeyStatus().cbegin(); it != app.hotkeyStatus().cend(); ++it)
        hotkeys[QString::number(it.key())] = it.value();
    o[QStringLiteral("hotkeys")] = hotkeys;
    QJsonArray overlays;
    for (auto* w : app.session().overlays()) {
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

QJsonObject cmdCreatePins(App& app, const QJsonObject& req)
{
    const int count = req.value(QStringLiteral("count")).toInt(1);
    const int w = req.value(QStringLiteral("width")).toInt(1920);
    const int h = req.value(QStringLiteral("height")).toInt(1080);
    const QImage base = noiseImage(w, h);
    QJsonArray ids;
    for (int i = 0; i < count; ++i) {
        PinWindow* pin = app.pinImage(base, QPoint(40 + (i % 10) * 30, 40 + (i / 10) * 30));
        ids.append(static_cast<int>(app.pins().indexOf(pin)));
    }
    QApplication::processEvents();
    return ok({{QStringLiteral("count"), static_cast<int>(app.pins().size())}});
}

QJsonObject cmdDragPin(App& app, const QJsonObject& req)
{
    const int index = req.value(QStringLiteral("index")).toInt(0);
    if (index < 0 || index >= app.pins().size())
        return fail(QStringLiteral("贴图序号无效"));
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
               {QStringLiteral("movesPerSecond"), steps * 1000.0 / std::max(elapsed, 0.001)},
               {QStringLiteral("paints"), pin->paintCount() - paintsBefore}});
}

QJsonObject cmdScalePin(App& app, const QJsonObject& req)
{
    const int index = req.value(QStringLiteral("index")).toInt(0);
    if (index < 0 || index >= app.pins().size())
        return fail(QStringLiteral("贴图序号无效"));
    PinWindow* pin = app.pins().at(index);
    const int steps = req.value(QStringLiteral("steps")).toInt(40);
    const double factor = req.value(QStringLiteral("factor")).toDouble(0.97);
    pin->takePaintDurationsMs();
    const int paintsBefore = pin->paintCount();
    const qint64 t0 = monotonicNs();
    for (int i = 0; i < steps; ++i) {
        pin->zoomAt(QPointF(pin->width() / 2.0, pin->height() / 2.0), factor, true);
        pin->repaint(); // 同步绘制，使每步都产生一帧
        QApplication::processEvents(QEventLoop::AllEvents);
    }
    const double elapsed = nsToMs(monotonicNs() - t0);
    QJsonArray frames;
    double maxFrame = 0;
    for (double d : pin->takePaintDurationsMs()) {
        frames.append(d);
        maxFrame = std::max(maxFrame, d);
    }
    return ok({{QStringLiteral("elapsedMs"), elapsed}, {QStringLiteral("steps"), steps},
               {QStringLiteral("fps"), steps * 1000.0 / std::max(elapsed, 0.001)},
               {QStringLiteral("paints"), pin->paintCount() - paintsBefore}, {QStringLiteral("maxFrameMs"), maxFrame},
               {QStringLiteral("frameMs"), frames}, {QStringLiteral("scale"), pin->scale()},
               {QStringLiteral("windowW"), pin->width()}, {QStringLiteral("windowH"), pin->height()}, {QStringLiteral("clipped"), pin->isClipped()}});
}

QJsonObject cmdBenchBlur(const QJsonObject& req)
{
    const int w = req.value(QStringLiteral("width")).toInt(3840);
    const int h = req.value(QStringLiteral("height")).toInt(2160);
    const int radius = req.value(QStringLiteral("radius")).toInt(10);
    const QImage src = noiseImage(w, h);
    fx::gaussianBlur(src, radius); // 预热线程池
    const int runs = 5;
    double best = 1e9, total = 0;
    for (int i = 0; i < runs; ++i) {
        const qint64 t0 = monotonicNs();
        const QImage out = fx::gaussianBlur(src, radius);
        const double ms = nsToMs(monotonicNs() - t0);
        Q_UNUSED(out);
        best = std::min(best, ms);
        total += ms;
    }
    return ok({{QStringLiteral("bestMs"), best}, {QStringLiteral("avgMs"), total / runs}, {QStringLiteral("radius"), radius}});
}

QJsonObject cmdBenchMosaic(const QJsonObject& req)
{
    const int w = req.value(QStringLiteral("width")).toInt(3840);
    const int h = req.value(QStringLiteral("height")).toInt(2160);
    const int block = req.value(QStringLiteral("block")).toInt(12);
    const QImage src = noiseImage(w, h);
    fx::mosaic(src, block);
    const int runs = 5;
    double best = 1e9, total = 0;
    for (int i = 0; i < runs; ++i) {
        const qint64 t0 = monotonicNs();
        const QImage out = fx::mosaic(src, block);
        const double ms = nsToMs(monotonicNs() - t0);
        Q_UNUSED(out);
        best = std::min(best, ms);
        total += ms;
    }
    return ok({{QStringLiteral("bestMs"), best}, {QStringLiteral("avgMs"), total / runs}, {QStringLiteral("block"), block}});
}

QJsonObject cmdOcrSelfTest(App& app)
{
    if (!app.platform().ocr)
        return fail(QStringLiteral("没有 OCR 实现"));
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
    const double ms = nsToMs(monotonicNs() - t0);
    QJsonObject o;
    o[QStringLiteral("available")] = app.platform().ocr->available();
    o[QStringLiteral("text")] = r.text();
    o[QStringLiteral("error")] = r.error;
    o[QStringLiteral("elapsedMs")] = ms;
    o[QStringLiteral("lines")] = static_cast<int>(r.lines.size());
    o[QStringLiteral("ok")] = r.ok;
    return o;
}

QJsonObject cmdElementAt(App& app, const QJsonObject& req)
{
    if (!app.platform().elementDetector)
        return fail(QStringLiteral("没有元素检测实现"));
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
        return fail(QStringLiteral("没有窗口检测实现"));
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
        return fail(QStringLiteral("没有显示器"));
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
                return fail(QStringLiteral("保存失败"));
            return ok({{QStringLiteral("w"), img.width()}, {QStringLiteral("h"), img.height()}});
        }
    }
    return fail(QStringLiteral("没有该显示器的覆盖窗口"));
}

} // namespace

QJsonObject handleTestCommand(App& app, const QJsonObject& req)
{
    const QString cmd = req.value(QStringLiteral("cmd")).toString();
    if (cmd == QStringLiteral("ping"))
        return ok({{QStringLiteral("pid"), static_cast<double>(QCoreApplication::applicationPid())}});
    if (cmd == QStringLiteral("screens")) {
        QJsonArray arr;
        if (app.session().screenCapture()) {
            for (const auto& s : app.session().screenCapture()->screens())
                arr.append(screenToJson(s));
        }
        return ok({{QStringLiteral("screens"), arr}});
    }
    if (cmd == QStringLiteral("useFakeScreens"))
        return cmdUseFakeScreens(app, req);
    if (cmd == QStringLiteral("useRealScreens")) {
        app.useScreenCapture(nullptr);
        app.session().prepareOverlays();
        return ok();
    }
    if (cmd == QStringLiteral("capture"))
        return cmdCapture(app, req);
    if (cmd == QStringLiteral("captureTimings"))
        return cmdCaptureTimings(app);
    if (cmd == QStringLiteral("setSelection")) {
        if (!app.session().isActive())
            return fail(QStringLiteral("没有进行中的截图"));
        app.session().setSelection(jsonToRect(req));
        return ok();
    }
    if (cmd == QStringLiteral("acceptCapture"))
        return cmdAcceptCapture(app, req);
    if (cmd == QStringLiteral("cancelCapture")) {
        app.session().cancel();
        return ok();
    }
    if (cmd == QStringLiteral("state"))
        return cmdState(app);
    if (cmd == QStringLiteral("overlayRender"))
        return cmdOverlayRender(app, req);
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
    if (cmd == QStringLiteral("memory")) {
#ifdef Q_OS_WIN
        return ok({{QStringLiteral("privateBytes"), static_cast<double>(win::privateBytes())}});
#else
        return fail(QStringLiteral("未实现"));
#endif
    }
    if (cmd == QStringLiteral("benchBlur"))
        return cmdBenchBlur(req);
    if (cmd == QStringLiteral("benchMosaic"))
        return cmdBenchMosaic(req);
    if (cmd == QStringLiteral("ocrSelfTest"))
        return cmdOcrSelfTest(app);
    if (cmd == QStringLiteral("elementAt"))
        return cmdElementAt(app, req);
    if (cmd == QStringLiteral("windows"))
        return cmdWindows(app);
    if (cmd == QStringLiteral("screenshotDesktop")) {
        // 用 GDI 抓整个虚拟桌面（含本程序的窗口），用于失败截图和诊断
#ifdef Q_OS_WIN
        QRect bounds;
        for (const auto& s : win::enumerateScreens())
            bounds = bounds.united(s.physicalGeometry);
        const QImage img = win::GdiScreenCapture::captureRect(bounds);
        const QString path = req.value(QStringLiteral("path")).toString();
        if (img.isNull() || !img.save(path, "PNG"))
            return fail(QStringLiteral("抓取或保存失败"));
        QJsonObject o = rectToJson(bounds);
        o[QStringLiteral("path")] = path;
        return ok(o);
#else
        return fail(QStringLiteral("未实现"));
#endif
    }
    if (cmd == QStringLiteral("quit")) {
        QMetaObject::invokeMethod(&app, &App::quit, Qt::QueuedConnection);
        return ok();
    }
    return fail(QStringLiteral("未知命令：") + cmd);
}

} // namespace ss
