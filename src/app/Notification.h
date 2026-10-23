#pragma once

#include <QString>
#include <QTimer>
#include <QWidget>

class QLabel;

namespace ss {

// 系统通知（GEN-07）：右上角浮出的小窗口，带“定位文件 / 复制文件 / 复制路径”按钮，几秒后自动消失。
// 不用系统通知中心：那里放不下自定义按钮，且各平台行为不一。
class NotificationWidget : public QWidget {
    Q_OBJECT
public:
    explicit NotificationWidget(const QString& title, const QString& message, const QString& filePath, int timeoutMs = 5000);

    QString filePath() const { return m_path; }
    void revealFile();  // 在资源管理器 / 访达中定位
    void copyFile();    // 复制文件（可粘贴到文件管理器）
    void copyPath();

    // 定位文件的实现（便于测试替换）
    static std::function<void(const QString&)> revealHandler;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    QString m_path;
    QTimer m_timer;
};

} // namespace ss
