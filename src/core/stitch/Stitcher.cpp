#include "core/stitch/Stitcher.h"

#include <QHash>
#include <QPainter>
#include <cmath>

namespace ss::stitch {

namespace {

QImage toRgb32(const QImage& img)
{
    return img.format() == QImage::Format_RGB32 || img.format() == QImage::Format_ARGB32_Premultiplied ? img : img.convertToFormat(QImage::Format_RGB32);
}

// FNV-1a 变体，逐像素混合
inline quint64 hashRow(const quint32* row, int from, int to)
{
    quint64 h = 1469598103934665603ULL;
    for (int x = from; x < to; ++x) {
        h ^= row[x] & 0x00FFFFFFu;
        h *= 1099511628211ULL;
        h ^= h >> 29;
    }
    return h;
}

// 在整个重叠区逐像素校验位移；返回不一致像素比例（-1 表示重叠不足）
double verifyShift(const QImage& previous, const QImage& current, int shift, const ShiftOptions& o);

bool rowsEqual(const QImage& a, int ya, const QImage& b, int yb, int tolerance, int& mismatches)
{
    const auto* ra = reinterpret_cast<const quint32*>(a.constScanLine(ya));
    const auto* rb = reinterpret_cast<const quint32*>(b.constScanLine(yb));
    const int w = std::min(a.width(), b.width());
    mismatches = 0;
    for (int x = 0; x < w; ++x) {
        const quint32 p = ra[x], q = rb[x];
        const int dr = std::abs(static_cast<int>((p >> 16) & 0xFF) - static_cast<int>((q >> 16) & 0xFF));
        const int dg = std::abs(static_cast<int>((p >> 8) & 0xFF) - static_cast<int>((q >> 8) & 0xFF));
        const int db = std::abs(static_cast<int>(p & 0xFF) - static_cast<int>(q & 0xFF));
        if (dr > tolerance || dg > tolerance || db > tolerance)
            ++mismatches;
    }
    return mismatches == 0;
}

double verifyShift(const QImage& previous, const QImage& current, int shift, const ShiftOptions& o)
{
    const int h = std::min(previous.height(), current.height());
    const int overlap = h - std::abs(shift);
    if (overlap < o.minOverlapRows)
        return -1;
    qint64 mismatched = 0;
    int counted = 0;
    for (int y = 0; y < overlap; ++y) {
        const int py = shift >= 0 ? y + shift : y;
        const int cy = shift >= 0 ? y : y - shift;
        // 同一屏幕位置上两帧完全相同的行（悬浮元素、空白行）不参与校验，无论它在新帧还是旧帧的对应位置（CAP-67）
        int sameM = 0;
        if (rowsEqual(previous, cy, current, cy, o.pixelTolerance, sameM) || rowsEqual(previous, py, current, py, o.pixelTolerance, sameM))
            continue;
        int m = 0;
        rowsEqual(previous, py, current, cy, o.pixelTolerance, m);
        mismatched += m;
        ++counted;
    }
    if (counted < o.minOverlapRows / 2)
        return 0.0; // 几乎全是静止行：没有可校验的内容，交给投票结果
    return static_cast<double>(mismatched) / (static_cast<double>(counted) * previous.width());
}

} // namespace

QVector<quint64> rowHashes(const QImage& frame, int x0, int x1)
{
    const QImage img = toRgb32(frame);
    if (x1 < 0 || x1 > img.width())
        x1 = img.width();
    x0 = std::clamp(x0, 0, x1);
    QVector<quint64> hashes(img.height());
    for (int y = 0; y < img.height(); ++y)
        hashes[y] = hashRow(reinterpret_cast<const quint32*>(img.constScanLine(y)), x0, x1);
    return hashes;
}

FixedRegions detectFixedRegions(const QVector<quint64>& prev, const QVector<quint64>& cur, int maxFraction)
{
    FixedRegions f;
    const int n = std::min(prev.size(), cur.size());
    const int limit = n / std::max(1, maxFraction); // 固定区最多占一半，避免整帧相同时全算固定区
    while (f.top < limit && prev[f.top] == cur[f.top])
        ++f.top;
    while (f.bottom < limit && prev[n - 1 - f.bottom] == cur[n - 1 - f.bottom])
        ++f.bottom;
    return f;
}

ShiftResult estimateShift(const QImage& previousIn, const QImage& currentIn, const QVector<quint64>& prevHashes, const QVector<quint64>& curHashes, const ShiftOptions& o)
{
    ShiftResult r;
    const QImage previous = toRgb32(previousIn);
    const QImage current = toRgb32(currentIn);
    const int h = std::min(previous.height(), current.height());
    if (h < o.minOverlapRows || previous.width() != current.width()) {
        r.reason = QStringLiteral("frame too small");
        return r;
    }
    // 上一帧中只出现一次的行作为锚点
    QHash<quint64, int> prevIndex;
    QHash<quint64, int> prevCount;
    for (int y = 0; y < h; ++y) {
        ++prevCount[prevHashes[y]];
        prevIndex.insert(prevHashes[y], y);
    }
    QHash<quint64, int> curCount;
    for (int y = 0; y < h; ++y)
        ++curCount[curHashes[y]];
    // 投票：锚点在新帧中的位置 → 位移 = prevY - curY
    QHash<int, int> votes;
    int anchors = 0;
    for (int y = 0; y < h; ++y) {
        const quint64 hv = curHashes[y];
        if (curCount.value(hv) != 1)
            continue;
        auto it = prevIndex.find(hv);
        if (it == prevIndex.end() || prevCount.value(hv) != 1)
            continue;
        ++anchors;
        ++votes[it.value() - y];
    }
    r.anchors = anchors;
    if (anchors < o.minAnchors) {
        r.reason = QStringLiteral("too few anchors (uniform or repeated content)");
        return r;
    }
    int bestShift = 0, bestVotes = -1;
    for (auto it = votes.cbegin(); it != votes.cend(); ++it) {
        if (it.value() > bestVotes || (it.value() == bestVotes && std::abs(it.key()) < std::abs(bestShift))) {
            bestVotes = it.value();
            bestShift = it.key();
        }
    }
    const double ratio = static_cast<double>(bestVotes) / anchors;
    if (ratio < o.minVoteRatio) {
        r.reason = QStringLiteral("ambiguous match");
        r.shift = bestShift;
        r.confidence = ratio;
        return r;
    }
    // 校验：重叠区逐像素比对
    const double mismatchRatio = verifyShift(previous, current, bestShift, o);
    if (mismatchRatio < 0) {
        r.reason = QStringLiteral("overlap too small (scrolled too fast)");
        r.shift = bestShift;
        return r;
    }
    if (mismatchRatio > o.maxMismatchRatio) {
        r.reason = QStringLiteral("overlap verification failed (dynamic content)");
        r.shift = bestShift;
        r.confidence = 1.0 - mismatchRatio;
        return r;
    }
    r.ok = true;
    r.shift = bestShift;
    r.confidence = ratio * (1.0 - mismatchRatio);
    return r;
}

ShiftResult estimateShiftByProjection(const QImage& previousIn, const QImage& currentIn, const ShiftOptions& o)
{
    ShiftResult r;
    const QImage a = toRgb32(previousIn).scaledToWidth(std::max(8, previousIn.width() / 4), Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    const QImage b = toRgb32(currentIn).scaledToWidth(std::max(8, currentIn.width() / 4), Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    const int h = std::min(a.height(), b.height());
    if (h < 8) {
        r.reason = QStringLiteral("frame too small");
        return r;
    }
    auto projection = [](const QImage& img, int rows) {
        QVector<double> p(rows);
        for (int y = 0; y < rows; ++y) {
            const uchar* line = img.constScanLine(y);
            double sum = 0;
            for (int x = 0; x < img.width(); ++x)
                sum += line[x];
            p[y] = sum / img.width();
        }
        // 去均值，便于相关性
        double mean = 0;
        for (double v : p)
            mean += v;
        mean /= rows;
        for (double& v : p)
            v -= mean;
        return p;
    };
    const QVector<double> pa = projection(a, h), pb = projection(b, h);
    double energy = 0;
    for (double v : pa)
        energy += v * v;
    if (energy < 1e-6 * h) {
        r.reason = QStringLiteral("uniform content");
        return r;
    }
    double best = -1e300, second = -1e300;
    int bestShift = 0;
    const int maxShift = h - o.minOverlapRows / 4;
    for (int s = -maxShift; s <= maxShift; ++s) {
        double corr = 0, na = 0, nb = 0;
        int count = 0;
        for (int y = 0; y < h; ++y) {
            const int py = s >= 0 ? y + s : y;
            const int cy = s >= 0 ? y : y - s;
            if (py >= h || cy >= h)
                break;
            corr += pa[py] * pb[cy];
            na += pa[py] * pa[py];
            nb += pb[cy] * pb[cy];
            ++count;
        }
        if (count < o.minOverlapRows / 4 || na < 1e-9 || nb < 1e-9)
            continue;
        const double normalized = corr / std::sqrt(na * nb);
        if (normalized > best) {
            if (std::abs(s - bestShift) > 2)
                second = best; // 远离最佳峰的次优峰（周期性内容会出现多个峰）
            best = normalized;
            bestShift = s;
        } else if (std::abs(s - bestShift) > 2 && normalized > second) {
            second = normalized;
        }
    }
    if (best < 0.9) {
        r.reason = QStringLiteral("low-texture match not confident");
        r.confidence = std::max(0.0, best);
        return r;
    }
    if (second > best - 0.05) {
        r.reason = QStringLiteral("ambiguous match (repeated content)");
        r.confidence = best;
        return r;
    }
    // 还原到原分辨率并在附近用像素校验精调
    const int factor = std::max(1, previousIn.height() / std::max(1, h));
    const int approx = bestShift * factor;
    int refined = approx;
    double bestMismatch = 2.0;
    for (int s = approx - factor - 2; s <= approx + factor + 2; ++s) {
        const double m = verifyShift(toRgb32(previousIn), toRgb32(currentIn), s, o);
        if (m >= 0 && m < bestMismatch) {
            bestMismatch = m;
            refined = s;
        }
    }
    if (bestMismatch > o.maxMismatchRatio) {
        r.reason = QStringLiteral("low-texture verification failed");
        r.confidence = best;
        r.shift = refined;
        return r;
    }
    r.ok = true;
    r.shift = refined;
    r.confidence = best * (1.0 - bestMismatch);
    return r;
}

// ---- LongImage ----

LongImage::LongImage(int width, int chunkRows)
    : m_width(width)
    , m_chunkRows(std::max(1, chunkRows))
{
}

void LongImage::appendRows(const QImage& sourceIn, int y0, int rows)
{
    const QImage source = toRgb32(sourceIn);
    if (m_width == 0)
        m_width = source.width();
    const size_t rowBytes = static_cast<size_t>(m_width) * 4;
    for (int i = 0; i < rows; ++i) {
        const int y = y0 + i;
        if (y < 0 || y >= source.height())
            break;
        const int local = m_height % m_chunkRows;
        if (local == 0)
            m_chunks.append(QImage(m_width, m_chunkRows, QImage::Format_RGB32));
        std::memcpy(m_chunks.last().scanLine(local), source.constScanLine(y), std::min(rowBytes, static_cast<size_t>(source.width()) * 4));
        ++m_height;
    }
}

void LongImage::truncate(int height)
{
    height = std::clamp(height, 0, m_height);
    m_height = height;
    const int needed = (m_height + m_chunkRows - 1) / m_chunkRows;
    while (m_chunks.size() > needed)
        m_chunks.removeLast();
}

void LongImage::removeTop(int rows)
{
    rows = std::clamp(rows, 0, m_height);
    if (rows == 0)
        return;
    LongImage rebuilt(m_width, m_chunkRows);
    for (int y = rows; y < m_height; y += m_chunkRows) {
        const int n = std::min(m_chunkRows, m_height - y);
        rebuilt.appendRows(this->rows(y, n), 0, n);
    }
    *this = rebuilt;
}

QRgb LongImage::pixel(int x, int y) const
{
    if (x < 0 || y < 0 || x >= m_width || y >= m_height)
        return 0;
    return m_chunks[y / m_chunkRows].pixel(x, y % m_chunkRows);
}

QImage LongImage::rows(int y0, int count) const
{
    y0 = std::clamp(y0, 0, m_height);
    count = std::clamp(count, 0, m_height - y0);
    QImage out(m_width, count, QImage::Format_RGB32);
    const size_t rowBytes = static_cast<size_t>(m_width) * 4;
    for (int i = 0; i < count; ++i) {
        const int y = y0 + i;
        std::memcpy(out.scanLine(i), m_chunks[y / m_chunkRows].constScanLine(y % m_chunkRows), rowBytes);
    }
    return out;
}

QImage LongImage::toImage() const
{
    return rows(0, m_height);
}

QImage LongImage::thumbnail(int maxWidth, int maxHeight) const
{
    if (isEmpty())
        return {};
    // 分块缩放后拼接，避免先生成整图
    const double factor = std::min(static_cast<double>(maxWidth) / m_width, static_cast<double>(maxHeight) / m_height);
    const int w = std::max(1, static_cast<int>(m_width * factor));
    const int h = std::max(1, static_cast<int>(m_height * factor));
    QImage thumb(w, h, QImage::Format_RGB32);
    thumb.fill(Qt::black);
    QPainter p(&thumb);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int y = 0; y < m_height; y += m_chunkRows) {
        const int n = std::min(m_chunkRows, m_height - y);
        const QImage piece = rows(y, n);
        p.drawImage(QRectF(0, y * factor, w, n * factor), piece);
    }
    return thumb;
}

qint64 LongImage::byteCount() const
{
    qint64 total = 0;
    for (const auto& c : m_chunks)
        total += c.sizeInBytes();
    return total;
}

// ---- 滚动条与悬浮元素 ----

int detectScrollbarWidth(const QImage& prevIn, const QImage& curIn, int shift, int maxWidth)
{
    const QImage prev = toRgb32(prevIn), cur = toRgb32(curIn);
    if (shift == 0 || prev.size() != cur.size() || prev.width() < 8)
        return 0;
    const int h = prev.height();
    const int absShift = std::abs(shift);
    if (absShift >= h - 8)
        return 0;
    // 从右缘向左逐列分类：滑块列（不随内容滚动、同位置大体相同）、单色列（轨道边缘 / 空白）、内容列（随内容滚动）。
    // 滚动条宽度 = 最左的滑块列到右缘，其间只能是滑块列或单色列
    int leftmostThumb = -1, leftmostBar = -1;
    for (int x = prev.width() - 1; x >= prev.width() - maxWidth && x >= 0; --x) {
        int same = 0, shifted = 0, n = 0;
        for (int y = 0; y + absShift < h; ++y) {
            const QRgb p = prev.pixel(x, shift > 0 ? y + absShift : y);
            const QRgb c = cur.pixel(x, shift > 0 ? y : y + absShift);
            if (prev.pixel(x, y) == cur.pixel(x, y))
                ++same;
            if (p == c)
                ++shifted;
            ++n;
        }
        const double sameRatio = static_cast<double>(same) / n;
        const double shiftedRatio = static_cast<double>(shifted) / n;
        const bool thumbLike = shiftedRatio < 0.97 && sameRatio >= 0.5;
        const bool uniform = sameRatio >= 0.97 && shiftedRatio >= 0.97;
        if (thumbLike)
            leftmostThumb = x;
        else if (!uniform)
            break; // 内容列：滚动条到此为止
        leftmostBar = x; // 滑块列或紧邻的单色轨道边缘都算滚动条
    }
    if (leftmostThumb < 0)
        return 0;
    const int width = prev.width() - leftmostBar;
    return width >= 4 ? width : 0;
}

QList<Stitcher::FloatingRun> detectFloatingRuns(const QVector<quint64>& prev, const QVector<quint64>& cur, int shift, int top, int bottom, int minRows)
{
    QList<Stitcher::FloatingRun> runs;
    const int n = std::min(prev.size(), cur.size());
    if (shift == 0)
        return runs;
    int runStart = -1;
    for (int y = top; y <= n - bottom; ++y) {
        // 同位置相同、但不是因为内容恰好滚到这里（与按位移对应的行不同）
        bool floating = false;
        if (y < n - bottom && prev[y] == cur[y]) {
            const int src = y + shift;
            floating = src < 0 || src >= n || prev[src] != cur[y];
        }
        if (floating && runStart < 0)
            runStart = y;
        if (!floating && runStart >= 0) {
            if (y - runStart >= minRows)
                runs.append({runStart, y - runStart});
            runStart = -1;
        }
    }
    return runs;
}

// ---- Stitcher ----

Stitcher::Stitcher(ShiftOptions options)
    : m_options(options)
{
}

void Stitcher::begin(const QImage& firstFrame)
{
    m_previous = toRgb32(firstFrame);
    m_prevHashes = rowHashes(m_previous);
    m_image = LongImage(m_previous.width());
    m_image.appendRows(m_previous, 0, m_previous.height());
    m_fixed = FixedRegions{};
    m_fixedLocked = false;
    m_started = true;
    m_frames = 1;
    m_zeroShiftRun = 0;
    m_scrollbarWidth = 0;
    m_floating.clear();
    m_floatingCandidates.clear();
}

void Stitcher::appendRowsSkippingFloating(const QImage& frame, int y0, int rows)
{
    // 按悬浮行段切开，只追加不在悬浮段里的行
    int y = y0;
    const int end = y0 + rows;
    while (y < end) {
        int next = end;
        bool inside = false;
        for (const FloatingRun& r : m_floating) {
            if (y >= r.top && y < r.top + r.rows) {
                inside = true;
                next = std::min(end, r.top + r.rows);
                break;
            }
            if (r.top > y)
                next = std::min(next, r.top);
        }
        if (!inside)
            m_image.appendRows(frame, y, next - y);
        y = next;
    }
}

Stitcher::FrameResult Stitcher::addFrame(const QImage& frameIn)
{
    FrameResult result;
    if (!m_started)
        return result;
    const QImage frame = toRgb32(frameIn);
    if (frame.size() != m_previous.size()) {
        result.shift.reason = QStringLiteral("frame size changed");
        return result;
    }
    QVector<quint64> hashes = rowHashes(frame, 0, m_scrollbarWidth > 0 ? frame.width() - m_scrollbarWidth : -1);
    ++m_frames;

    // 固定区：前几帧按两帧相同行数识别，之后锁定（取稳定值）
    const FixedRegions detected = detectFixedRegions(m_prevHashes, hashes);
    const int h = frame.height();
    if (h - detected.top - detected.bottom < m_options.minOverlapRows) {
        // 两帧几乎相同（悬停高亮、光标闪烁、时钟跳动）：没有滚动，不算错误，也不能把整帧当成固定区
        ++m_zeroShiftRun;
        result.fixed = m_fixed;
        result.shift.ok = true;
        result.shift.shift = 0;
        result.shift.confidence = 1.0;
        result.reachedEnd = m_zeroShiftRun >= m_endThreshold;
        return result;
    }
    if (!m_fixedLocked) {
        if (detected.top == m_fixed.top && detected.bottom == m_fixed.bottom && m_frames > 2)
            m_fixedLocked = true;
        else
            m_fixed = detected;
    }
    result.fixed = m_fixed;

    // 整帧相同：没有滚动
    if (hashes == m_prevHashes) {
        ++m_zeroShiftRun;
        result.shift.ok = true;
        result.shift.shift = 0;
        result.shift.confidence = 1.0;
        result.reachedEnd = m_zeroShiftRun >= m_endThreshold;
        return result;
    }
    m_zeroShiftRun = 0;

    const int top = m_fixed.top, bottom = m_fixed.bottom;
    const int scrollRows = h - top - bottom;
    if (scrollRows < m_options.minOverlapRows) {
        result.shift.reason = QStringLiteral("scroll area too small");
        return result;
    }
    const int bodyWidth = frame.width() - m_scrollbarWidth; // 已识别的滚动条列不参与匹配（CAP-68）
    const QImage prevScroll = m_previous.copy(0, top, bodyWidth, scrollRows);
    const QImage curScroll = frame.copy(0, top, bodyWidth, scrollRows);
    const QVector<quint64> prevH = m_prevHashes.mid(top, scrollRows);
    const QVector<quint64> curH = hashes.mid(top, scrollRows);
    ShiftResult shift = estimateShift(prevScroll, curScroll, prevH, curH, m_options);
    if (!shift.ok && shift.anchors < m_options.minAnchors)
        shift = estimateShiftByProjection(prevScroll, curScroll, m_options); // 低纹理兜底
    result.shift = shift;
    if (!shift.ok)
        return result;

    // CAP-68：第一次得到可信位移时识别右侧滚动条，之后哈希与输出都排除这些列
    if (m_detectScrollbar && m_scrollbarWidth == 0 && m_frames == 2) {
        m_scrollbarWidth = detectScrollbarWidth(m_previous, frame, shift.shift);
        if (m_scrollbarWidth > 0) {
            hashes = rowHashes(frame, 0, frame.width() - m_scrollbarWidth); // 之后的匹配都按去掉滚动条的哈希
            // 重建已输出的长图（去掉滚动条列）
            LongImage rebuilt(m_previous.width() - m_scrollbarWidth);
            const QImage first = m_image.toImage();
            rebuilt.appendRows(first.copy(0, 0, first.width() - m_scrollbarWidth, first.height()), 0, first.height());
            m_image = rebuilt;
        }
    }
    // CAP-67：中间区域跟随屏幕不动的行段，连续两帧一致后确认
    if (m_detectFloating) {
        const QList<FloatingRun> runs = detectFloatingRuns(m_prevHashes, hashes, shift.shift, top, bottom);
        QList<FloatingRun> confirmed;
        for (const FloatingRun& r : runs)
            for (const FloatingRun& c : m_floatingCandidates)
                if (c.top == r.top && c.rows == r.rows)
                    confirmed.append(r);
        m_floatingCandidates = runs;
        if (!confirmed.isEmpty())
            m_floating = confirmed;
    }

    if (shift.shift > 0) {
        // 内容上移 shift 行：新帧滚动区底部的 shift 行是新内容
        const int newRows = std::min(shift.shift, scrollRows);
        // 长图末尾当前包含上一帧的底部固定区：先去掉，再追加新内容
        m_image.truncate(m_image.height() - bottom);
        const QImage body = m_scrollbarWidth > 0 ? frame.copy(0, 0, frame.width() - m_scrollbarWidth, frame.height()) : frame;
        appendRowsSkippingFloating(body, top + scrollRows - newRows, newRows);
        m_image.appendRows(body, h - bottom, bottom);
        result.appended = true;
        result.appendedRows = newRows;
    } else if (shift.shift < 0) {
        // 反向滚动：裁掉长图尾部对应的行（CAP-70）
        const int back = std::min(-shift.shift, m_image.height() - h);
        if (back > 0) {
            m_image.truncate(m_image.height() - bottom - back);
            const QImage body = m_scrollbarWidth > 0 ? frame.copy(0, 0, frame.width() - m_scrollbarWidth, frame.height()) : frame;
            m_image.appendRows(body, h - bottom, bottom);
            result.appended = true;
            result.appendedRows = -back;
        }
    }
    m_previous = frame;
    m_prevHashes = hashes;
    return result;
}

QImage Stitcher::finish() const
{
    return m_image.toImage();
}

QImage transposed(const QImage& imageIn)
{
    const QImage image = toRgb32(imageIn);
    QImage out(image.height(), image.width(), QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const quint32*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            reinterpret_cast<quint32*>(out.scanLine(x))[y] = line[x];
    }
    return out;
}

} // namespace ss::stitch
