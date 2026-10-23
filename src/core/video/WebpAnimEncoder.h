#pragma once

#include <QImage>
#include <QString>
#include <memory>

namespace ss {

// 动画 WebP 写出（VID-03）：libwebp 的 WebPAnimEncoder
class WebpAnimEncoder {
public:
    WebpAnimEncoder();
    ~WebpAnimEncoder();
    bool open(const QString& path, QSize size, int quality = 80, bool lossless = false);
    bool addFrame(const QImage& frame, int timestampMs);
    bool finish(); // 写出文件

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace ss
