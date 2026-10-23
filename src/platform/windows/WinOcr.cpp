#include "platform/windows/WinOcr.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <memorybuffer.h>
#include <windows.h>

#include <QRegularExpression>
#include <cstring>
#include <thread>

namespace ss::win {

namespace {

// Windows OCR 把中文逐字当作“单词”，行文本里字与字之间带空格；去掉 CJK 字符之间的空格
QString collapseCjkSpaces(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("(?<=[\\x{4E00}-\\x{9FFF}\\x{3000}-\\x{303F}\\x{FF00}-\\x{FFEF}])\\s+(?=[\\x{4E00}-\\x{9FFF}\\x{3000}-\\x{303F}\\x{FF00}-\\x{FFEF}])"));
    QString out = text;
    out.replace(re, QString());
    return out;
}

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

// 在 MTA 线程里同步执行，避免在 STA（Qt 主线程）上阻塞等待 WinRT 异步操作
template <typename Fn>
auto runInMta(Fn fn) -> decltype(fn())
{
    decltype(fn()) result{};
    std::thread t([&] {
        init_apartment(apartment_type::multi_threaded);
        try {
            result = fn();
        } catch (...) {
        }
        uninit_apartment();
    });
    t.join();
    return result;
}

SoftwareBitmap toSoftwareBitmap(const QImage& source)
{
    const QImage img = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    SoftwareBitmap bitmap(BitmapPixelFormat::Bgra8, img.width(), img.height(), BitmapAlphaMode::Premultiplied);
    {
        BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        auto reference = buffer.CreateReference();
        auto access = reference.as<::Windows::Foundation::IMemoryBufferByteAccess>();
        uint8_t* data = nullptr;
        uint32_t capacity = 0;
        check_hresult(access->GetBuffer(&data, &capacity));
        const BitmapPlaneDescription plane = buffer.GetPlaneDescription(0);
        const size_t rowBytes = static_cast<size_t>(img.width()) * 4;
        for (int y = 0; y < img.height(); ++y)
            std::memcpy(data + plane.StartIndex + static_cast<size_t>(y) * plane.Stride, img.constScanLine(y), rowBytes);
    }
    return bitmap;
}

} // namespace

QStringList WinOcr::availableLanguages()
{
    return runInMta([] {
        QStringList list;
        for (const auto& lang : OcrEngine::AvailableRecognizerLanguages())
            list << QString::fromStdWString(std::wstring(lang.LanguageTag()));
        return list;
    });
}

bool WinOcr::available() const
{
    return !availableLanguages().isEmpty();
}

void WinOcr::configure(const QString& key, const QString& value)
{
    if (key == QLatin1String("language"))
        m_language = value.isEmpty() ? QStringLiteral("auto") : value;
    else if (key == QLatin1String("mode"))
        m_accurate = value == QLatin1String("accurate");
}

OcrResult WinOcr::recognize(const QImage& imageIn)
{
    // 精准模式（REC-02）：放大后识别，小字更稳；结果坐标换算回原图
    const int upscale = m_accurate && imageIn.width() * 2 <= 10000 && imageIn.height() * 2 <= 10000 ? 2 : 1;
    const QImage image = upscale > 1 ? imageIn.scaled(imageIn.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation) : imageIn;
    if (image.isNull())
        return OcrResult{false, QStringLiteral("空图像"), {}};
    // Windows OCR 对图像尺寸有上限（单边 ≤ 10000 像素）
    if (image.width() > 10000 || image.height() > 10000)
        return OcrResult{false, QStringLiteral("图像过大"), {}};

    return runInMta([&]() -> OcrResult {
        OcrResult result;
        try {
            OcrEngine engine{nullptr};
            if (m_language != QLatin1String("auto")) {
                try {
                    engine = OcrEngine::TryCreateFromLanguage(::winrt::Windows::Globalization::Language(m_language.toStdWString()));
                } catch (...) {
                    engine = nullptr;
                }
            }
            if (!engine)
                engine = OcrEngine::TryCreateFromUserProfileLanguages();
            if (!engine) {
                // 用户语言没有 OCR 包时，退而取第一个可用语言
                auto langs = OcrEngine::AvailableRecognizerLanguages();
                if (langs.Size() == 0) {
                    result.error = QStringLiteral("系统没有安装任何 OCR 语言包");
                    return result;
                }
                engine = OcrEngine::TryCreateFromLanguage(langs.GetAt(0));
            }
            if (!engine) {
                result.error = QStringLiteral("无法创建 OCR 引擎");
                return result;
            }
            SoftwareBitmap bitmap = toSoftwareBitmap(image);
            ::winrt::Windows::Media::Ocr::OcrResult ocr = engine.RecognizeAsync(bitmap).get();
            for (const auto& line : ocr.Lines()) {
                OcrLine l;
                l.text = collapseCjkSpaces(QString::fromStdWString(std::wstring(line.Text())));
                QRect bounds;
                for (const auto& word : line.Words()) {
                    const auto r = word.BoundingRect();
                    const QRect wr(static_cast<int>(r.X), static_cast<int>(r.Y), static_cast<int>(r.Width), static_cast<int>(r.Height));
                    bounds = bounds.isNull() ? wr : bounds.united(wr);
                }
                l.bounds = upscale > 1 ? QRect(bounds.x() / upscale, bounds.y() / upscale, bounds.width() / upscale, bounds.height() / upscale) : bounds;
                result.lines.append(l);
            }
            result.ok = true;
        } catch (const hresult_error& e) {
            result.error = QStringLiteral("WinRT 错误 0x%1: %2")
                               .arg(static_cast<quint32>(e.code()), 8, 16, QLatin1Char('0'))
                               .arg(QString::fromStdWString(std::wstring(e.message())));
        }
        return result;
    });
}

} // namespace ss::win
