#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

namespace ss {

// 录制期间记录的输入事件（VID-06）
struct InputEvent {
    enum class Kind { Click, Wheel, Key };
    Kind kind = Kind::Click;
    qint64 timeMs = 0;
    QPoint pos;        // 相对录制区域的像素位置（点击 / 滚轮）
    int button = 1;    // 1 左 2 中 3 右
    int wheel = 0;     // 滚轮方向：正为上
    QString keyText;   // 按键文字（如 "Ctrl+C"）
    QJsonObject toJson() const;
    static InputEvent fromJson(const QJsonObject& o);
};

// 一段录制（普通模式）：帧存放在磁盘目录，索引文件 recording.json 记录元数据；异常退出后可从目录恢复（VID-09）
class FrameStore {
public:
    enum class Quality { Light, Standard, Raw }; // VID-02：轻量（缩小一半 + JPEG）/ 标准（JPEG）/ 原始（PNG）

    FrameStore() = default;
    // 新建：dir 不存在则创建；清空旧内容
    bool create(const QString& dir, const QRect& region, int fps, Quality quality);
    // 打开已有目录（恢复）
    bool open(const QString& dir);
    bool isOpen() const { return !m_dir.isEmpty(); }
    QString dir() const { return m_dir; }

    // 追加一帧；timestampMs 为相对开始的时间
    bool appendFrame(const QImage& frame, qint64 timestampMs);
    int frameCount() const { return static_cast<int>(m_timestamps.size()); }
    qint64 timestampMs(int index) const { return m_timestamps.value(index); }
    QImage frame(int index) const;
    QSize frameSize() const { return m_frameSize; }
    qint64 durationMs() const;
    int fps() const { return m_fps; }
    QRect region() const { return m_region; }
    Quality quality() const { return m_quality; }

    void addEvent(const InputEvent& e) { m_events.append(e); }
    const QList<InputEvent>& events() const { return m_events; }
    // 音频：16 位单声道 PCM，采样率
    bool appendAudio(const QVector<qint16>& samples, int sampleRate);
    QVector<qint16> audio() const;
    int audioSampleRate() const { return m_audioRate; }

    // 索引写盘：录制中每隔一段时间写一次，结束时 finished=true
    bool writeIndex(bool finished);
    bool finished() const { return m_finished; }
    // 删除整个目录
    void remove();

    // 目录里有未完成的录制（崩溃恢复，VID-09）
    static bool hasUnfinished(const QString& dir);
    static QString qualityName(Quality q);
    static Quality qualityFromName(const QString& name);

private:
    QString framePath(int index) const;
    QString m_dir;
    QRect m_region;
    int m_fps = 30;
    Quality m_quality = Quality::Standard;
    QSize m_frameSize;
    QVector<qint64> m_timestamps;
    QList<InputEvent> m_events;
    int m_audioRate = 0;
    bool m_finished = false;
    QString m_frameExt = QStringLiteral("jpg");
};

} // namespace ss
