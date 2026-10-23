#include "core/video/WebpAnimEncoder.h"

#include <QFile>
#include <webp/encode.h>
#include <webp/mux.h>

namespace ss {

struct WebpAnimEncoder::Impl {
    QString path;
    QSize size;
    WebPAnimEncoder* encoder = nullptr;
    WebPConfig config{};
    int lastTimestamp = 0;
    ~Impl()
    {
        if (encoder)
            WebPAnimEncoderDelete(encoder);
    }
};

WebpAnimEncoder::WebpAnimEncoder()
    : m_impl(std::make_unique<Impl>())
{
}

WebpAnimEncoder::~WebpAnimEncoder() = default;

bool WebpAnimEncoder::open(const QString& path, QSize size, int quality, bool lossless)
{
    m_impl->path = path;
    m_impl->size = size;
    WebPAnimEncoderOptions options;
    if (!WebPAnimEncoderOptionsInit(&options))
        return false;
    options.anim_params.loop_count = 0;
    options.minimize_size = 0;
    options.kmin = 3;
    options.kmax = 30;
    m_impl->encoder = WebPAnimEncoderNew(size.width(), size.height(), &options);
    if (!m_impl->encoder)
        return false;
    if (!WebPConfigInit(&m_impl->config))
        return false;
    m_impl->config.quality = static_cast<float>(quality);
    m_impl->config.lossless = lossless ? 1 : 0;
    m_impl->config.method = 4;
    return WebPValidateConfig(&m_impl->config) != 0;
}

bool WebpAnimEncoder::addFrame(const QImage& frameIn, int timestampMs)
{
    if (!m_impl->encoder)
        return false;
    QImage frame = frameIn.convertToFormat(QImage::Format_RGBA8888);
    if (frame.size() != m_impl->size)
        frame = frame.scaled(m_impl->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    WebPPicture pic;
    if (!WebPPictureInit(&pic))
        return false;
    pic.use_argb = 1;
    pic.width = frame.width();
    pic.height = frame.height();
    if (!WebPPictureImportRGBA(&pic, frame.constBits(), static_cast<int>(frame.bytesPerLine())))
        return false;
    const bool ok = WebPAnimEncoderAdd(m_impl->encoder, &pic, timestampMs, &m_impl->config) != 0;
    WebPPictureFree(&pic);
    m_impl->lastTimestamp = timestampMs;
    return ok;
}

bool WebpAnimEncoder::finish()
{
    if (!m_impl->encoder)
        return false;
    // 结束标记：最后一帧的持续时间
    WebPAnimEncoderAdd(m_impl->encoder, nullptr, m_impl->lastTimestamp + 40, nullptr);
    WebPData data;
    WebPDataInit(&data);
    const bool assembled = WebPAnimEncoderAssemble(m_impl->encoder, &data) != 0;
    bool ok = false;
    if (assembled) {
        QFile f(m_impl->path);
        ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(reinterpret_cast<const char*>(data.bytes), static_cast<qint64>(data.size)) == static_cast<qint64>(data.size);
    }
    WebPDataClear(&data);
    WebPAnimEncoderDelete(m_impl->encoder);
    m_impl->encoder = nullptr;
    return ok;
}

} // namespace ss
