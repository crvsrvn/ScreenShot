#pragma once

#include <QDialog>
#include <QJsonArray>
#include <QRect>

class QSpinBox;
class QComboBox;
class QLineEdit;

namespace ss {

class Settings;

// 自定义截图对话框（CAP-05）：输入位置、尺寸和延时；可保存为带名字的预设（存在设置 capture.presets 里）。
class CustomCaptureDialog : public QDialog {
    Q_OBJECT
public:
    struct Request {
        QRect region;
        int delayMs = 0;
    };

    explicit CustomCaptureDialog(Settings& settings, QWidget* parent = nullptr);

    Request request() const;
    void setRegion(const QRect& r);
    void setDelayMs(int ms);
    // 预设
    static QJsonArray presets(const Settings& settings);
    static void savePreset(Settings& settings, const QString& name, const QRect& region, int delayMs);
    static bool removePreset(Settings& settings, const QString& name);
    void applyPreset(int index);
    void saveCurrentAsPreset(const QString& name);

private:
    void reloadPresets();

    Settings& m_settings;
    QSpinBox* m_x;
    QSpinBox* m_y;
    QSpinBox* m_w;
    QSpinBox* m_h;
    QSpinBox* m_delay;
    QComboBox* m_presets;
    QLineEdit* m_presetName;
};

} // namespace ss
