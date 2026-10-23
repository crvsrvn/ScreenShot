#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace ss {

// 动画 GIF 写出（VID-03）：GIF89a，每帧局部调色板（中位切分量化到 256 色）+ LZW；无第三方依赖
class GifEncoder {
public:
    bool open(const QString& path, QSize size, int loopCount = 0); // loopCount 0 = 无限循环
    bool addFrame(const QImage& frame, int delayMs);
    bool finish();
    QByteArray bytes() const { return m_data; } // 未指定路径时的内存输出

    // 单帧量化：返回 Indexed8 图（最多 256 色）
    static QImage quantize(const QImage& frame, int maxColors = 256);

private:
    void writeFrame(const QImage& indexed, int delayMs);
    QString m_path;
    QSize m_size;
    QByteArray m_data;
    bool m_open = false;
};

} // namespace ss
