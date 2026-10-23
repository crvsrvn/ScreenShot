#include "core/video/GifEncoder.h"

#include <QFile>
#include <QHash>
#include <algorithm>
#include <vector>

namespace ss {

namespace {

// ---- 中位切分量化 ----
struct Box {
    std::vector<QRgb> colors;
};

int channelRange(const Box& b, int channel, int& lo, int& hi)
{
    lo = 255;
    hi = 0;
    for (QRgb c : b.colors) {
        const int v = channel == 0 ? qRed(c) : channel == 1 ? qGreen(c) : qBlue(c);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return hi - lo;
}

QRgb average(const Box& b)
{
    quint64 r = 0, g = 0, bl = 0;
    for (QRgb c : b.colors) {
        r += qRed(c);
        g += qGreen(c);
        bl += qBlue(c);
    }
    const quint64 n = std::max<quint64>(1, b.colors.size());
    return qRgb(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(bl / n));
}

// ---- LZW ----
class LzwWriter {
public:
    explicit LzwWriter(int minCodeSize)
        : m_minCodeSize(minCodeSize)
    {
    }
    QByteArray encode(const uchar* pixels, int count)
    {
        m_out.clear();
        m_bits = 0;
        m_nbits = 0;
        const int clearCode = 1 << m_minCodeSize;
        const int endCode = clearCode + 1;
        int codeSize = m_minCodeSize + 1;
        int next = endCode + 1;
        QHash<quint32, int> dict; // (prefix << 8 | byte) → code
        putCode(clearCode, codeSize);
        if (count == 0) {
            putCode(endCode, codeSize);
            flush();
            return m_out;
        }
        int prefix = pixels[0];
        for (int i = 1; i < count; ++i) {
            const uchar c = pixels[i];
            const quint32 key = (static_cast<quint32>(prefix) << 8) | c;
            auto it = dict.find(key);
            if (it != dict.end()) {
                prefix = it.value();
                continue;
            }
            putCode(prefix, codeSize);
            if (next < 4096) {
                dict.insert(key, next++);
                if (next > (1 << codeSize) && codeSize < 12)
                    ++codeSize;
            } else {
                putCode(clearCode, codeSize);
                dict.clear();
                codeSize = m_minCodeSize + 1;
                next = endCode + 1;
            }
            prefix = c;
        }
        putCode(prefix, codeSize);
        putCode(endCode, codeSize);
        flush();
        return m_out;
    }

private:
    void putCode(int code, int size)
    {
        m_bits |= static_cast<quint32>(code) << m_nbits;
        m_nbits += size;
        while (m_nbits >= 8) {
            m_out.append(static_cast<char>(m_bits & 0xFF));
            m_bits >>= 8;
            m_nbits -= 8;
        }
    }
    void flush()
    {
        if (m_nbits > 0)
            m_out.append(static_cast<char>(m_bits & 0xFF));
    }
    int m_minCodeSize;
    QByteArray m_out;
    quint32 m_bits = 0;
    int m_nbits = 0;
};

void appendLe16(QByteArray& d, int v)
{
    d.append(static_cast<char>(v & 0xFF));
    d.append(static_cast<char>((v >> 8) & 0xFF));
}

} // namespace

QImage GifEncoder::quantize(const QImage& frameIn, int maxColors)
{
    const QImage frame = frameIn.convertToFormat(QImage::Format_RGB32);
    // 收集不重复颜色（按 5 位每通道先合并，控制数量）
    QHash<QRgb, int> histogram;
    for (int y = 0; y < frame.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        for (int x = 0; x < frame.width(); ++x)
            ++histogram[line[x] & 0xFFF8F8F8];
    }
    std::vector<Box> boxes(1);
    boxes[0].colors.reserve(histogram.size());
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it)
        boxes[0].colors.push_back(it.key());
    while (static_cast<int>(boxes.size()) < maxColors) {
        // 选跨度最大的盒子切开
        int best = -1, bestRange = -1, bestChannel = 0;
        for (size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].colors.size() < 2)
                continue;
            for (int ch = 0; ch < 3; ++ch) {
                int lo, hi;
                const int range = channelRange(boxes[i], ch, lo, hi);
                if (range > bestRange) {
                    bestRange = range;
                    best = static_cast<int>(i);
                    bestChannel = ch;
                }
            }
        }
        if (best < 0 || bestRange == 0)
            break;
        Box& b = boxes[best];
        std::sort(b.colors.begin(), b.colors.end(), [bestChannel](QRgb a, QRgb c) {
            const int va = bestChannel == 0 ? qRed(a) : bestChannel == 1 ? qGreen(a) : qBlue(a);
            const int vc = bestChannel == 0 ? qRed(c) : bestChannel == 1 ? qGreen(c) : qBlue(c);
            return va < vc;
        });
        Box right;
        const size_t mid = b.colors.size() / 2;
        right.colors.assign(b.colors.begin() + static_cast<long long>(mid), b.colors.end());
        b.colors.resize(mid);
        boxes.push_back(std::move(right));
    }
    QList<QRgb> palette;
    for (const Box& b : boxes)
        palette.append(average(b));
    // 映射：每个量化色 → 最近的调色板项（缓存）
    QHash<QRgb, uchar> lookup;
    auto nearest = [&](QRgb c) -> uchar {
        auto it = lookup.find(c);
        if (it != lookup.end())
            return it.value();
        int bestI = 0, bestD = 1 << 30;
        for (int i = 0; i < palette.size(); ++i) {
            const int dr = qRed(c) - qRed(palette[i]), dg = qGreen(c) - qGreen(palette[i]), db = qBlue(c) - qBlue(palette[i]);
            const int d = dr * dr + dg * dg + db * db;
            if (d < bestD) {
                bestD = d;
                bestI = i;
            }
        }
        lookup.insert(c, static_cast<uchar>(bestI));
        return static_cast<uchar>(bestI);
    };
    QImage indexed(frame.size(), QImage::Format_Indexed8);
    indexed.setColorTable(palette);
    for (int y = 0; y < frame.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(frame.constScanLine(y));
        uchar* out = indexed.scanLine(y);
        for (int x = 0; x < frame.width(); ++x)
            out[x] = nearest(line[x] & 0xFFF8F8F8);
    }
    return indexed;
}

bool GifEncoder::open(const QString& path, QSize size, int loopCount)
{
    m_path = path;
    m_size = size;
    m_data.clear();
    m_data.append("GIF89a");
    appendLe16(m_data, size.width());
    appendLe16(m_data, size.height());
    m_data.append(static_cast<char>(0x70)); // 无全局调色板，8 位色深
    m_data.append('\0');
    m_data.append('\0');
    // Netscape 循环扩展
    m_data.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16);
    appendLe16(m_data, loopCount);
    m_data.append('\0');
    m_open = true;
    return true;
}

void GifEncoder::writeFrame(const QImage& indexed, int delayMs)
{
    // 图形控制扩展
    m_data.append("\x21\xF9\x04", 3);
    m_data.append(static_cast<char>(0x04)); // 处置方式：不处置
    appendLe16(m_data, std::max(1, delayMs / 10));
    m_data.append('\0');
    m_data.append('\0');
    // 图像描述符 + 局部调色板
    m_data.append(',');
    appendLe16(m_data, 0);
    appendLe16(m_data, 0);
    appendLe16(m_data, indexed.width());
    appendLe16(m_data, indexed.height());
    int tableBits = 1;
    while ((1 << tableBits) < indexed.colorCount())
        ++tableBits;
    tableBits = std::max(tableBits, 1);
    m_data.append(static_cast<char>(0x80 | (tableBits - 1)));
    const QList<QRgb> table = indexed.colorTable();
    for (int i = 0; i < (1 << tableBits); ++i) {
        const QRgb c = i < table.size() ? table[i] : 0;
        m_data.append(static_cast<char>(qRed(c)));
        m_data.append(static_cast<char>(qGreen(c)));
        m_data.append(static_cast<char>(qBlue(c)));
    }
    const int minCodeSize = std::max(2, tableBits);
    m_data.append(static_cast<char>(minCodeSize));
    QByteArray pixels;
    pixels.reserve(indexed.width() * indexed.height());
    for (int y = 0; y < indexed.height(); ++y)
        pixels.append(reinterpret_cast<const char*>(indexed.constScanLine(y)), indexed.width());
    const QByteArray lzw = LzwWriter(minCodeSize).encode(reinterpret_cast<const uchar*>(pixels.constData()), static_cast<int>(pixels.size()));
    for (int i = 0; i < lzw.size(); i += 255) {
        const int n = std::min<int>(255, static_cast<int>(lzw.size()) - i);
        m_data.append(static_cast<char>(n));
        m_data.append(lzw.constData() + i, n);
    }
    m_data.append('\0');
}

bool GifEncoder::addFrame(const QImage& frame, int delayMs)
{
    if (!m_open)
        return false;
    QImage f = frame;
    if (f.size() != m_size)
        f = f.scaled(m_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    writeFrame(quantize(f), delayMs);
    return true;
}

bool GifEncoder::finish()
{
    if (!m_open)
        return false;
    m_data.append(';');
    m_open = false;
    if (m_path.isEmpty())
        return true;
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(m_data) == m_data.size();
}

} // namespace ss
