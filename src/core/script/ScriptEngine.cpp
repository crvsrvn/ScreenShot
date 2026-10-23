#include "core/script/ScriptEngine.h"

#include <QElapsedTimer>
#include <quickjs.h>

namespace ss {

struct ScriptEngine::Impl {
    ScriptHost host;
    QStringList output;
    JSRuntime* runtime = nullptr;
    JSContext* context = nullptr;
    QElapsedTimer timer;
    int timeoutMs = 0;

    static Impl* self(JSContext* ctx) { return static_cast<Impl*>(JS_GetContextOpaque(ctx)); }

    static QString toQString(JSContext* ctx, JSValueConst v)
    {
        const char* s = JS_ToCString(ctx, v);
        const QString out = s ? QString::fromUtf8(s) : QString();
        if (s)
            JS_FreeCString(ctx, s);
        return out;
    }

    static JSValue jsPrint(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        QStringList parts;
        for (int i = 0; i < argc; ++i)
            parts.append(toQString(ctx, argv[i]));
        self(ctx)->output.append(parts.join(QLatin1Char(' ')));
        return JS_UNDEFINED;
    }

    static JSValue jsCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* impl = self(ctx);
        if (argc < 1 || !impl->host.command)
            return JS_UNDEFINED;
        const QString result = impl->host.command(toQString(ctx, argv[0]));
        return JS_NewString(ctx, result.toUtf8().constData());
    }

    static JSValue jsSnip(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* impl = self(ctx);
        if (!impl->host.command)
            return JS_UNDEFINED;
        const QString args = argc >= 1 ? toQString(ctx, argv[0]) : QString();
        return JS_NewString(ctx, impl->host.command((QStringLiteral("snip ") + args).trimmed()).toUtf8().constData());
    }

    static JSValue jsPin(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* impl = self(ctx);
        if (!impl->host.command)
            return JS_UNDEFINED;
        const QString args = argc >= 1 ? toQString(ctx, argv[0]) : QString();
        return JS_NewString(ctx, impl->host.command((QStringLiteral("pin ") + args).trimmed()).toUtf8().constData());
    }

    static JSValue jsGetClipboard(JSContext* ctx, JSValueConst, int, JSValueConst*)
    {
        Impl* impl = self(ctx);
        return JS_NewString(ctx, impl->host.getClipboard ? impl->host.getClipboard().toUtf8().constData() : "");
    }

    static JSValue jsSetClipboard(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* impl = self(ctx);
        if (argc >= 1 && impl->host.setClipboard)
            impl->host.setClipboard(toQString(ctx, argv[0]));
        return JS_UNDEFINED;
    }

    static JSValue jsSleep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
    {
        Impl* impl = self(ctx);
        int32_t ms = 0;
        if (argc >= 1)
            JS_ToInt32(ctx, &ms, argv[0]);
        if (impl->host.sleep && ms > 0)
            impl->host.sleep(ms);
        return JS_UNDEFINED;
    }

    static int interrupt(JSRuntime*, void* opaque)
    {
        Impl* impl = static_cast<Impl*>(opaque);
        return impl->timeoutMs > 0 && impl->timer.elapsed() > impl->timeoutMs ? 1 : 0;
    }

    explicit Impl(ScriptHost h)
        : host(std::move(h))
    {
        runtime = JS_NewRuntime();
        JS_SetMemoryLimit(runtime, 64 * 1024 * 1024);
        JS_SetMaxStackSize(runtime, 1024 * 1024);
        JS_SetInterruptHandler(runtime, &Impl::interrupt, this);
        context = JS_NewContext(runtime);
        JS_SetContextOpaque(context, this);
        JSValue global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "print", JS_NewCFunction(context, &Impl::jsPrint, "print", 1));
        JSValue ss = JS_NewObject(context);
        JS_SetPropertyStr(context, ss, "command", JS_NewCFunction(context, &Impl::jsCommand, "command", 1));
        JS_SetPropertyStr(context, ss, "snip", JS_NewCFunction(context, &Impl::jsSnip, "snip", 1));
        JS_SetPropertyStr(context, ss, "pin", JS_NewCFunction(context, &Impl::jsPin, "pin", 1));
        JS_SetPropertyStr(context, ss, "getClipboard", JS_NewCFunction(context, &Impl::jsGetClipboard, "getClipboard", 0));
        JS_SetPropertyStr(context, ss, "setClipboard", JS_NewCFunction(context, &Impl::jsSetClipboard, "setClipboard", 1));
        JS_SetPropertyStr(context, ss, "sleep", JS_NewCFunction(context, &Impl::jsSleep, "sleep", 1));
        JS_SetPropertyStr(context, global, "ss", ss);
        JS_FreeValue(context, global);
    }

    ~Impl()
    {
        JS_FreeContext(context);
        JS_FreeRuntime(runtime);
    }
};

ScriptEngine::ScriptEngine(ScriptHost host)
    : m_impl(std::make_unique<Impl>(std::move(host)))
{
}

ScriptEngine::~ScriptEngine() = default;

ScriptResult ScriptEngine::run(const QString& source, const QString& fileName)
{
    ScriptResult r;
    m_impl->output.clear();
    m_impl->timeoutMs = m_timeoutMs;
    m_impl->timer.start();
    const QByteArray src = source.toUtf8();
    JSValue v = JS_Eval(m_impl->context, src.constData(), src.size(), fileName.toUtf8().constData(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        JSValue ex = JS_GetException(m_impl->context);
        r.error = Impl::toQString(m_impl->context, ex);
        JSValue stack = JS_GetPropertyStr(m_impl->context, ex, "stack");
        if (!JS_IsUndefined(stack))
            r.error += QLatin1Char('\n') + Impl::toQString(m_impl->context, stack);
        JS_FreeValue(m_impl->context, stack);
        JS_FreeValue(m_impl->context, ex);
    } else {
        r.ok = true;
        if (!JS_IsUndefined(v))
            r.value = Impl::toQString(m_impl->context, v);
    }
    JS_FreeValue(m_impl->context, v);
    r.output = m_impl->output.join(QLatin1Char('\n'));
    return r;
}

} // namespace ss
