#pragma once

#include "core/ports/IVideoEncoder.h"
#include "core/video/InputVisualizer.h"
#include "core/video/Playback.h"
#include "core/video/Recording.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>

class QLabel;
class QSlider;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QPushButton;

namespace ss {

class Settings;
class OutputActions;

// 录制回放与导出（VID-03 / VID-06 / VID-08）：播放、暂停、跳转、倍速；裁剪开头结尾；进度条 / 输入可视化叠加；
// 导出 GIF / WebP / MP4：保存对话框、快速保存、复制为文件
class RecordPlaybackDialog : public QDialog {
    Q_OBJECT
public:
    RecordPlaybackDialog(std::shared_ptr<FrameStore> store, Settings& settings, OutputActions& output, VideoEncoderFactory encoder, QWidget* parent = nullptr);
    ~RecordPlaybackDialog() override;

    // 播放
    void play();
    void pause();
    bool playing() const { return m_timer.isActive(); }
    void seek(qint64 ms);
    qint64 position() const { return m_position; }
    void setSpeed(double speed) { m_speed = speed; }
    double speed() const { return m_speed; }
    // 裁剪
    void trimStartHere();
    void trimEndHere();
    void resetTrim();
    void setTrim(qint64 startMs, qint64 endMs);
    TrimRange trim() const { return m_trim; }
    // 叠加
    void setProgressStyle(ProgressStyle style) { m_progress = style; }
    void setShowInputs(bool on) { m_showInputs = on; }
    void setClickAnimMs(int ms) { m_inputOptions.clickDurationMs = ms; }
    // 当前帧（含叠加）；absoluteMs 为录制时间
    QImage renderAt(qint64 absoluteMs) const;

    // 导出：format = gif | webp | mp4
    bool exportTo(const QString& path, const QString& format);
    QString save();            // 对话框
    QString copyToClipboard(); // 复制为文件
    void discard();            // 删除帧目录
    QString lastError() const { return m_error; }
    QString format() const;
    void setFormat(const QString& format);
    void setDialogOverride(const QString& path) { m_dialogOverride = path; }
    std::shared_ptr<FrameStore> store() const { return m_store; }

signals:
    void exported(const QString& path);

private:
    void buildUi();
    void advance();
    void refreshPreview();
    QString exportToDir(const QString& dir);
    bool exportMp4(const QString& path, const QVector<ExportFrame>& plan, qint64 totalMs);

    std::shared_ptr<FrameStore> m_store;
    Settings& m_settings;
    OutputActions& m_output;
    VideoEncoderFactory m_encoder;
    QVector<qint64> m_timestamps;
    qint64 m_duration = 0;
    qint64 m_position = 0;
    double m_speed = 1.0;
    TrimRange m_trim;
    ProgressStyle m_progress = ProgressStyle::None;
    bool m_showInputs = true;
    InputVisualOptions m_inputOptions;
    QTimer m_timer;
    QElapsedTimer m_wall;
    QString m_error;
    QString m_dialogOverride;
    // 界面
    QLabel* m_preview = nullptr;
    QLabel* m_timeLabel = nullptr;
    QLabel* m_trimLabel = nullptr;
    QSlider* m_slider = nullptr;
    QPushButton* m_playButton = nullptr;
    QComboBox* m_formatBox = nullptr;
};

} // namespace ss
