#include "platform/windows/WinPlatform.h"

#include "platform/windows/MfCamera.h"
#include "platform/windows/MfVideoWriter.h"
#include "platform/windows/WasapiCapture.h"

#include "platform/windows/DxgiScreenCapture.h"
#include "platform/windows/GdiScreenCapture.h"
#include "platform/windows/WinElementDetector.h"
#include "platform/windows/WinGlobalHotkey.h"
#include "platform/windows/WinCursor.h"
#include "platform/windows/WinInputSimulator.h"
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
    std::optional<CursorImage> captureCursor() override { return captureCursorImage(); }
    void configure(const QString& key, const QString& value) override
    {
        if (key == QLatin1String("hdrStrategy"))
            m_dxgi.setHdrStrategy(value == QLatin1String("brightness") ? fx::HdrStrategy::PreserveBrightness : fx::HdrStrategy::PreserveHighlights);
    }

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
    p.input = std::make_unique<WinInputSimulator>();
    p.videoEncoder = [] { return std::unique_ptr<IVideoEncoder>(std::make_unique<MfVideoWriter>()); };
    p.audioCapture = [] { return std::unique_ptr<IAudioCapture>(std::make_unique<WasapiCapture>()); };
    p.camera = std::make_unique<MfCamera>();
    return p;
}

} // namespace ss::win
