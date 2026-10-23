// REQ: CFG-01 CFG-02 CFG-04 CFG-07
#include "core/config/Settings.h"

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ss;

class TestSettings : public QObject {
    Q_OBJECT
private slots:
    void everySpecHasTitleDescriptionAndValidDefault();
    void defaultsAndChangeSignal();
    void coercionClampsAndValidates();
    void saveLoadRoundTrip();
    void unknownKeysPreserved();
    void configPathResolution();
};

void TestSettings::everySpecHasTitleDescriptionAndValidDefault()
{
    // CFG-04：每个选项都要有说明文字；枚举默认值必须在选项里
    for (const auto& s : settingSpecs()) {
        QVERIFY2(!s.key.isEmpty() && s.title && *s.title && s.description && *s.description, qPrintable(s.key));
        QVERIFY2(!s.page.isEmpty(), qPrintable(s.key));
        if (s.type == SettingType::Enum) {
            QVERIFY2(s.choices.contains(s.defaultValue.toString()), qPrintable(s.key));
            QCOMPARE(s.choices.size(), s.choiceTitles.size());
        }
        QVERIFY2(settingSpec(s.key) == &s, qPrintable(s.key));
    }
    QVERIFY(settingSpecs().size() >= 20);
}

void TestSettings::defaultsAndChangeSignal()
{
    Settings s;
    QCOMPARE(s.intValue(QStringLiteral("capture.magnifierZoom")), 8);
    QSignalSpy spy(&s, &Settings::changed);
    s.setValue(QStringLiteral("capture.magnifierZoom"), 8); // 与默认值相同：不发信号
    QCOMPARE(spy.count(), 0);
    s.setValue(QStringLiteral("capture.magnifierZoom"), 12);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(s.intValue(QStringLiteral("capture.magnifierZoom")), 12);
    s.setValue(QStringLiteral("capture.magnifierZoom"), 12);
    QCOMPARE(spy.count(), 1);
    s.resetToDefault(QStringLiteral("capture.magnifierZoom"));
    QCOMPARE(s.intValue(QStringLiteral("capture.magnifierZoom")), 8);
}

void TestSettings::coercionClampsAndValidates()
{
    Settings s;
    s.setValue(QStringLiteral("capture.magnifierZoom"), 100);
    QCOMPARE(s.intValue(QStringLiteral("capture.magnifierZoom")), 16);
    s.setValue(QStringLiteral("capture.detectMode"), QStringLiteral("bogus"));
    QCOMPARE(s.stringValue(QStringLiteral("capture.detectMode")), QStringLiteral("element"));
    s.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("ctrl+shift+a"));
    QCOMPARE(s.stringValue(QStringLiteral("hotkey.capture")), QStringLiteral("Ctrl+Shift+A"));
    s.setValue(QStringLiteral("general.autostart"), QStringLiteral("true"));
    QCOMPARE(s.boolValue(QStringLiteral("general.autostart")), true);
}

void TestSettings::saveLoadRoundTrip()
{
    QTemporaryDir dir;
    const QString path = QDir(dir.path()).filePath(QStringLiteral("sub/settings.json"));
    Settings a;
    a.setValue(QStringLiteral("output.saveFormat"), QStringLiteral("webp"));
    a.setValue(QStringLiteral("capture.historyLimit"), 3);
    QVERIFY(a.save(path));
    Settings b;
    QVERIFY(b.load(path));
    QCOMPARE(b.stringValue(QStringLiteral("output.saveFormat")), QStringLiteral("webp"));
    QCOMPARE(b.intValue(QStringLiteral("capture.historyLimit")), 3);
    QCOMPARE(b.filePath(), path);
}

void TestSettings::unknownKeysPreserved()
{
    Settings s;
    s.fromMap({{QStringLiteral("future.option"), 42}});
    QCOMPARE(s.value(QStringLiteral("future.option")).toInt(), 42);
    QVERIFY(s.toMap().contains(QStringLiteral("future.option")));
}

void TestSettings::configPathResolution()
{
    QTemporaryDir app, user;
    QCOMPARE(resolveConfigPath(QStringLiteral("C:/x/custom.json"), app.path(), user.path()), QStringLiteral("C:/x/custom.json"));
    // 程序目录可写 → 便携模式
    QCOMPARE(resolveConfigPath(QString(), app.path(), user.path()), QDir(app.path()).filePath(QStringLiteral("config/settings.json")));
    // 程序目录不可写（不存在）→ 用户目录
    QCOMPARE(resolveConfigPath(QString(), QDir(app.path()).filePath(QStringLiteral("missing")), user.path()), QDir(user.path()).filePath(QStringLiteral("settings.json")));
}

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
