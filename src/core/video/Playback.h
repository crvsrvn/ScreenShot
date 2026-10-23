#pragma once

#include "core/video/Recording.h"

#include <QImage>
#include <QVector>

namespace ss {

// 回放与导出的纯逻辑（VID-08）：裁剪开头结尾、按时间找帧、进度条叠加、音频混合、画中画合成
struct TrimRange {
    qint64 startMs = 0;
    qint64 endMs = -1; // -1 = 到末尾
    qint64 clamp(qint64 durationMs) const;
    bool contains(qint64 t, qint64 durationMs) const;
};

// 时间 → 帧序号（最后一个时间戳 ≤ t 的帧）
int frameIndexAt(const QVector<qint64>& timestamps, qint64 t);

// 导出帧序列：按裁剪范围与目标帧率重采样，返回 (帧序号, 相对裁剪起点的时间)
struct ExportFrame {
    int index;
    qint64 timeMs;
};
QVector<ExportFrame> planExport(const QVector<qint64>& timestamps, qint64 durationMs, const TrimRange& trim, int targetFps);

// 进度条叠加（VID-08）：bar / text / countdown
enum class ProgressStyle { None, Bar, Text, Countdown };
ProgressStyle progressStyleFromName(const QString& name);
void paintProgress(QPainter& painter, const QSize& frameSize, qint64 elapsedMs, qint64 totalMs, ProgressStyle style);

// 音频混合（VID-04）：两路 16 位单声道按各自音量（0~200%）相加，饱和
QVector<qint16> mixAudio(const QVector<qint16>& a, int volumeA, const QVector<qint16>& b, int volumeB);

// 画中画（VID-07）：把摄像头画面按比例放到某个角
enum class PipCorner { TopLeft, TopRight, BottomLeft, BottomRight };
void paintPictureInPicture(QPainter& painter, const QSize& frameSize, const QImage& camera, PipCorner corner, int widthPercent = 25, int margin = 12);

} // namespace ss
