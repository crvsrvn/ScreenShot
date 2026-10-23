#include "platform/windows/WinShell.h"

#include <windows.h>
#include <ole2.h>
#include <oleauto.h>
#include <exdisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace ss::win {

QStringList selectedExplorerFiles()
{
    QStringList files;
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&windows))))
        return files;
    long count = 0;
    windows->get_Count(&count);
    const HWND foreground = GetForegroundWindow();
    HWND bestHwnd = nullptr;
    ComPtr<IShellBrowser> bestBrowser;
    for (long i = 0; i < count; ++i) {
        VARIANT index;
        VariantInit(&index);
        index.vt = VT_I4;
        index.lVal = i;
        ComPtr<IDispatch> dispatch;
        if (FAILED(windows->Item(index, &dispatch)) || !dispatch)
            continue;
        ComPtr<IWebBrowserApp> app;
        if (FAILED(dispatch.As(&app)))
            continue;
        SHANDLE_PTR hwndValue = 0;
        app->get_HWND(&hwndValue);
        const HWND hwnd = reinterpret_cast<HWND>(hwndValue);
        ComPtr<IServiceProvider> services;
        if (FAILED(app.As(&services)))
            continue;
        ComPtr<IShellBrowser> browser;
        if (FAILED(services->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))))
            continue;
        // 优先前台的资源管理器窗口；否则取第一个
        if (!bestBrowser || hwnd == foreground) {
            bestBrowser = browser;
            bestHwnd = hwnd;
            if (hwnd == foreground)
                break;
        }
    }
    Q_UNUSED(bestHwnd);
    if (!bestBrowser)
        return files;
    ComPtr<IShellView> view;
    if (FAILED(bestBrowser->QueryActiveShellView(&view)))
        return files;
    ComPtr<IFolderView2> folder;
    if (FAILED(view.As(&folder)))
        return files;
    ComPtr<IShellItemArray> items;
    if (FAILED(folder->GetSelection(FALSE, &items)) || !items)
        return files;
    DWORD n = 0;
    items->GetCount(&n);
    for (DWORD i = 0; i < n; ++i) {
        ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(i, &item)))
            continue;
        wchar_t* path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
            files.append(QString::fromWCharArray(path));
            CoTaskMemFree(path);
        }
    }
    return files;
}

namespace {

// 虚拟桌面“固定到所有桌面”的未公开接口（Windows 10 / 11 通用的 GUID）
const CLSID kClsidImmersiveShell = {0xC2F03A33, 0x21F5, 0x47FA, {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
const CLSID kClsidVirtualDesktopPinnedApps = {0xB5A399E7, 0x1C58, 0x46CB, {0x9D, 0x44, 0x18, 0xEA, 0x5E, 0xCD, 0x2A, 0x0F}};
const IID kIidVirtualDesktopPinnedApps = {0x4CE81583, 0x1E4C, 0x4632, {0xA6, 0x21, 0x07, 0xA5, 0x35, 0x43, 0x14, 0x8F}};

struct __declspec(uuid("4CE81583-1E4C-4632-A621-07A53543148F")) IVirtualDesktopPinnedApps : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsAppIdPinned(PCWSTR appId, BOOL* pinned) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinAppID(PCWSTR appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinAppID(PCWSTR appId) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsViewPinned(IUnknown* view, BOOL* pinned) = 0;
    virtual HRESULT STDMETHODCALLTYPE PinView(IUnknown* view) = 0;
    virtual HRESULT STDMETHODCALLTYPE UnpinView(IUnknown* view) = 0;
};

const IID kIidApplicationViewCollection = {0x1841C6D7, 0x4F9D, 0x42C0, {0xAF, 0x41, 0x87, 0x47, 0x53, 0x8F, 0x10, 0xE5}};

struct __declspec(uuid("1841C6D7-4F9D-42C0-AF41-8747538F10E5")) IApplicationViewCollection : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetViews(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByZOrder(void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewsByAppUserModelId(PCWSTR, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetViewForHwnd(HWND window, IUnknown** view) = 0;
};

} // namespace

bool setWindowOnAllDesktops(void* hwnd, bool on)
{
    ComPtr<IServiceProvider> shell;
    if (FAILED(CoCreateInstance(kClsidImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&shell))))
        return false;
    ComPtr<IVirtualDesktopPinnedApps> pinned;
    if (FAILED(shell->QueryService(kClsidVirtualDesktopPinnedApps, kIidVirtualDesktopPinnedApps, &pinned)) || !pinned)
        return false;
    ComPtr<IApplicationViewCollection> views;
    if (FAILED(shell->QueryService(kIidApplicationViewCollection, kIidApplicationViewCollection, &views)) || !views)
        return false;
    ComPtr<IUnknown> view;
    if (FAILED(views->GetViewForHwnd(static_cast<HWND>(hwnd), &view)) || !view)
        return false;
    return SUCCEEDED(on ? pinned->PinView(view.Get()) : pinned->UnpinView(view.Get()));
}

} // namespace ss::win
