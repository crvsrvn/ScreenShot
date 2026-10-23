#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QWidget;

namespace ss {

class Settings;

// 输出管线（OUT-01 复制、OUT-03 保存、OUT-08 快速保存）。每个动作成功后发 done 信号，写入历史由会话负责。
class OutputActions : public QObject {
    Q_OBJECT
public:
    enum class Kind { Copy, Save, QuickSave, Pin };
    Q_ENUM(Kind)

    explicit OutputActions(Settings& settings, QObject* parent = nullptr);

    // 复制到剪贴板：Qt 在 Windows 上同时写入 PNG（保留透明）与 DIB（通用位图）
    bool copy(const QImage& image);
    // 保存对话框：记住上次目录与格式（OUT-06 的基础）；用户取消返回空
    QString saveWithDialog(const QImage& image, QWidget* parent);
    // 快速保存到指定目录，不弹对话框
    QString quickSave(const QImage& image);
    // 按设置的格式与质量写文件；扩展名由 format 决定
    bool writeImage(const QImage& image, const QString& path, const QString& format) const;

    QString defaultSaveDir() const;
    QString quickSaveDir() const;
    QString makeFileName(const QImage& image) const;

    // 测试：不弹对话框，直接用这个路径
    void setDialogOverride(const QString& path) { m_dialogOverride = path; }

signals:
    void done(ss::OutputActions::Kind kind, const QString& path);
    void failed(ss::OutputActions::Kind kind, const QString& error);

private:
    Settings& m_settings;
    QString m_dialogOverride;
    int m_sequence = 0;
};

} // namespace ss
