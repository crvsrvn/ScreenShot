#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QWidget;

namespace ss {

class Settings;

// 输出管线（OUT-01 复制、OUT-02 复制为文件、OUT-03 保存、OUT-05 质量、OUT-08 快速保存、OUT-09 自动保存、OUT-11 打印）。
// 每个动作成功后发 done 信号，写入历史由会话负责。
class OutputActions : public QObject {
    Q_OBJECT
public:
    enum class Kind { Copy, Save, QuickSave, Pin, AutoSave, Print };
    Q_ENUM(Kind)

    explicit OutputActions(Settings& settings, QObject* parent = nullptr);

    // 来源窗口信息（OUT-10 的 {title} {process}）
    void setSource(const QString& windowTitle, const QString& processName);

    // 复制到剪贴板：图像（PNG + DIB）；按设置附带文件（OUT-02）
    bool copy(const QImage& image);
    // 保存对话框：记住上次目录与格式，同名覆盖由对话框确认（OUT-06）；用户取消返回空
    QString saveWithDialog(const QImage& image, QWidget* parent);
    // 快速保存到指定目录，不弹对话框
    QString quickSave(const QImage& image);
    // 自动保存（OUT-09）：开启时额外存一份；返回路径或空
    QString autoSave(const QImage& image);
    // 清理自动保存目录里 N 天前的文件（OUT-09）；返回删除数
    int cleanupAutoSave();
    // 打印（OUT-11）：pdfPath 非空时打印到 PDF 文件（测试 / 无打印机），否则弹打印对话框
    bool print(const QImage& image, QWidget* parent, const QString& pdfPath = QString());
    // 按设置的格式与质量写文件；无半透明像素时去掉透明通道（OUT-05）
    bool writeImage(const QImage& image, const QString& path, const QString& format) const;
    static bool hasTranslucentPixels(const QImage& image);

    QString defaultSaveDir() const;
    QString quickSaveDir() const;
    QString autoSaveDir() const;
    QString makeFileName(const QImage& image) const;
    // 复制为文件时使用的临时目录
    QString clipboardFileDir() const;

    // 测试：不弹对话框，直接用这个路径
    void setDialogOverride(const QString& path) { m_dialogOverride = path; }

signals:
    void done(ss::OutputActions::Kind kind, const QString& path);
    void failed(ss::OutputActions::Kind kind, const QString& error);

private:
    Settings& m_settings;
    QString m_dialogOverride;
    QString m_windowTitle;
    QString m_processName;
    int m_sequence = 0;
};

} // namespace ss
