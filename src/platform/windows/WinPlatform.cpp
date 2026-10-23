#include "platform/windows/WinPlatform.h"

#include "platform/windows/DxgiScreenCapture.h"
#include "platform/windows/GdiScreenCapture.h"
#include "platform/windows/WinElementDetector.h"
#include "platform/windows/WinGlobalHotkey.h"
#include "platform/windows/WinOcr.h"
#include "platform/windows/WinWindowDetector.h"

namespace ss::win {

namespace {

// DXGI 为主、GDI 兜底
class FallbackScreenCapture : public IScreenCapture {
public:
    QList<ScreenInfo> screens() override { return m_gdi.screens(); }

    std::optional<ScreenSnapshot> capture(const ScreenInfo& screen) override
    {
        if (auto snap = m_dxgi.capture(screen)) {
            m_lastBackend = QStringLiteral("dxgi");
            return snap;
        }
        m_lastBackend = QStringLiteral("gdi");
        return m_gdi.capture(screen);
    }

    QString backendName() const override { return m_lastBackend; }

private:
    DxgiScreenCapture m_dxgi;
    GdiScreenCapture m_gdi;
    QString m_lastBackend = QStringLiteral("dxgi");
};

} // namespace

Platform makeWindowsPlatform()
{
    Platform p;
    p.screenCapture = std::make_unique<FallbackScreenCapture>();
    p.windowDetector = std::make_unique<WinWindowDetector>();
    p.elementDetector = std::make_unique<WinElementDetector>();
    p.hotkey = std::make_unique<WinGlobalHotkey>();
    p.ocr = std::make_unique<WinOcr>();
    return p;
}

} // namespace ss::win
