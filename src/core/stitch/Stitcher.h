#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QVector>
#include <optional>

namespace ss::stitch {

// 滚动截图拼接（CAP-61 ~ CAP-66、NFR-06）。全部为平台无关的纯算法，可单元测试。
// 约定：纵向拼接；横向拼接由调用方把帧转置后使用同一套算法（CAP-63）。

// 一帧的逐行 64 位哈希（只哈希 [x0, x1) 列，便于排除滚动条）
QVector<quint64> rowHashes(const QImage& frame, int x0 = 0, int x1 = -1);

// 固定区识别（CAP-65）：两帧顶部 / 底部完全相同的连续行数
struct FixedRegions {
    int top = 0;
    int bottom = 0;
};
FixedRegions detectFixedRegions(const QVector<quint64>& previous, const QVector<quint64>& current, int maxFraction = 2);

// 位移估计结果
struct ShiftResult {
    bool ok = false;
    int shift = 0;          // 新帧相对上一帧向上滚动的行数（内容上移为正）；0 表示没有滚动
    double confidence = 0;  // 0~1
    int anchors = 0;        // 参与投票的锚点行数
    QString reason;         // 不可信时的原因（给预览提示）
};

struct ShiftOptions {
    int minOverlapRows = 32;        // 重叠区至少这么多行
    int minAnchors = 8;             // 至少这么多锚点行投票
    double minVoteRatio = 0.6;      // 得票最高的位移占锚点的比例
    int pixelTolerance = 8;         // 校验时每通道允许的差
    double maxMismatchRatio = 0.02; // 校验时允许不一致的像素比例
};

// 估计 current 相对 previous 的滚动位移（两帧已去掉固定区）。
ShiftResult estimateShift(const QImage& previous, const QImage& current, const QVector<quint64>& prevHashes, const QVector<quint64>& curHashes,
                          const ShiftOptions& options = {});

// 低纹理兜底：缩小后灰度行投影的相关性匹配
ShiftResult estimateShiftByProjection(const QImage& previous, const QImage& current, const ShiftOptions& options = {});

// 分块长图（CAP-74）：按固定行数分块存放，避免单张图像上限
class LongImage {
public:
    explicit LongImage(int width = 0, int chunkRows = 4096);
    int width() const { return m_width; }
    int height() const { return m_height; }
    bool isEmpty() const { return m_height == 0; }
    void appendRows(const QImage& source, int y0, int rows); // 从 source 的 [y0, y0+rows) 追加
    void truncate(int height);                              // 裁掉尾部（CAP-70）
    void removeTop(int rows);                               // 裁掉头部
    QRgb pixel(int x, int y) const;
    QImage rows(int y0, int rows) const;                    // 取一段
    QImage toImage() const;                                 // 整张（受 QImage 尺寸限制，超长时用 rows）
    QImage thumbnail(int maxWidth, int maxHeight) const;    // 预览缩略图
    qint64 byteCount() const;

private:
    int m_width;
    int m_chunkRows;
    int m_height = 0;
    QList<QImage> m_chunks;
};

// 拼接器：把稳定帧依次合成为长图；固定区自动识别；不可信的帧不追加
class Stitcher {
public:
    struct FrameResult {
        bool appended = false;  // 本帧是否追加了内容
        bool reachedEnd = false;// 连续若干帧位移为 0（到底）
        ShiftResult shift;
        FixedRegions fixed;
        int appendedRows = 0;
    };

    explicit Stitcher(ShiftOptions options = {});

    // 第一帧：作为长图开头
    void begin(const QImage& firstFrame);
    bool started() const { return m_started; }
    FrameResult addFrame(const QImage& frame);
    const LongImage& image() const { return m_image; }
    FixedRegions fixedRegions() const { return m_fixed; }
    int frameCount() const { return m_frames; }
    // 连续零位移帧数达到这个值视为到底（CAP-62）
    void setEndThreshold(int frames) { m_endThreshold = frames; }
    // 最终输出：底部固定区补回长图末尾
    QImage finish() const;
    // 滚动条排除（CAP-68）：自动识别右侧滚动条列，不参与匹配、不进入输出；0 表示没有
    int excludedRightColumns() const { return m_scrollbarWidth; }
    void setScrollbarDetection(bool on) { m_detectScrollbar = on; }
    // 悬浮元素（CAP-67）：中间区域跟随屏幕不动的行段，追加时跳过，避免重复出现
    struct FloatingRun {
        int top = 0;
        int rows = 0;
    };
    QList<FloatingRun> floatingRuns() const { return m_floating; }
    void setFloatingDetection(bool on) { m_detectFloating = on; }

private:
    ShiftOptions m_options;
    LongImage m_image;
    QImage m_previous;           // 上一帧（完整）
    QVector<quint64> m_prevHashes;
    FixedRegions m_fixed;
    QImage m_lastFooter;
    bool m_started = false;
    bool m_fixedLocked = false;
    int m_frames = 0;
    int m_zeroShiftRun = 0;
    int m_endThreshold = 3;
    bool m_detectScrollbar = true;
    int m_scrollbarWidth = 0;
    bool m_detectFloating = true;
    QList<FloatingRun> m_floating;
    QList<FloatingRun> m_floatingCandidates;
    void appendRowsSkippingFloating(const QImage& frame, int y0, int rows);
};

// 右侧滚动条宽度识别（CAP-68）：两帧内容按 shift 滚动时，右缘跟随屏幕不动（轨道）或独立移动（滑块）的列
int detectScrollbarWidth(const QImage& previous, const QImage& current, int shift, int maxWidth = 24);
// 悬浮元素识别（CAP-67）：两帧内容按 shift 滚动时，中间区域仍逐行相同的行段
QList<Stitcher::FloatingRun> detectFloatingRuns(const QVector<quint64>& previous, const QVector<quint64>& current, int shift, int top, int bottom, int minRows = 6);

// 转置（横向拼接用）：宽高互换，像素 (x, y) → (y, x)
QImage transposed(const QImage& image);

} // namespace ss::stitch
