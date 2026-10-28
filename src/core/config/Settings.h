#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariant>

namespace ss {

// 设置项的类型，决定设置窗口里用什么控件（CFG-01 自动生成）
enum class SettingType { Bool, Int, Double, String, Directory, Enum, KeySequence, Color };

// 一条设置项的声明：键、所在页、标题、说明（CFG-04）、默认值、取值范围
struct SettingSpec {
    QString key;           // 形如 "capture.detectMode"
    QString page;          // 设置窗口的页（未翻译的英文标识）
    const char* title;     // 翻译源文本（QT_TRANSLATE_NOOP）
    const char* description;
    SettingType type = SettingType::Bool;
    QVariant defaultValue;
    QVariant minimum;      // Int / Double
    QVariant maximum;
    QStringList choices;   // Enum：存储值
    QList<const char*> choiceTitles; // Enum：显示文本（翻译源）
};

// 全部设置项的声明表（只写一处，CFG-04）
const QList<SettingSpec>& settingSpecs();
const SettingSpec* settingSpec(const QString& key);

// 设置值：内存里的键值表 + JSON 文件读写（CFG-02）。修改立即生效：发 changed 信号（CFG-07）。
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    QVariant value(const QString& key) const;
    bool boolValue(const QString& key) const { return value(key).toBool(); }
    int intValue(const QString& key) const { return value(key).toInt(); }
    QString stringValue(const QString& key) const { return value(key).toString(); }
    void setValue(const QString& key, const QVariant& value);
    void resetToDefault(const QString& key);

    // 文件读写；load 时未知键保留，缺失键用默认值
    bool load(const QString& path);
    bool save(const QString& path) const;
    QString filePath() const { return m_filePath; }
    void setFilePath(const QString& path) { m_filePath = path; }
    bool saveToFile() const { return !m_filePath.isEmpty() && save(m_filePath); }

    // 导出 / 导入全部（CFG-03）
    QVariantMap toMap() const;
    void fromMap(const QVariantMap& map);

signals:
    void changed(const QString& key, const QVariant& value);

private:
    QVariant coerce(const SettingSpec& spec, const QVariant& value) const;

    QHash<QString, QVariant> m_values;
    QString m_filePath;
};

// 决定配置文件位置（CFG-02）：命令行指定 > 程序目录可写（便携模式）> 用户配置目录
QString resolveConfigPath(const QString& commandLineConfig, const QString& appDir, const QString& userConfigDir);

} // namespace ss
