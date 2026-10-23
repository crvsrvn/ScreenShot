// REQ: CFG-01 CFG-04 CFG-07
#include "app/PreferencesDialog.h"
#include "core/config/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QTest>

using namespace ss;

class TestPreferences : public QObject {
    Q_OBJECT
private slots:
    void pagesMatchSettingsTable();
    void everyEditorHasTooltip();
    void editsApplyImmediately();
};

void TestPreferences::pagesMatchSettingsTable()
{
    Settings s;
    PreferencesDialog d(s);
    auto* tabs = d.findChild<QTabWidget*>();
    QVERIFY(tabs);
    QStringList pages;
    for (const auto& spec : settingSpecs())
        if (!pages.contains(spec.page) && spec.page != QLatin1String("hidden")) // CFG-06：高级配置不进设置窗口
            pages.append(spec.page);
    QCOMPARE(tabs->count(), pages.size());
    for (int i = 0; i < pages.size(); ++i)
        QCOMPARE(tabs->tabText(i), PreferencesDialog::pageTitle(pages[i]));
}

void TestPreferences::everyEditorHasTooltip()
{
    Settings s;
    PreferencesDialog d(s);
    for (const auto& spec : settingSpecs()) {
        if (spec.page == QLatin1String("hidden"))
            continue;
        QWidget* editor = d.findChild<QWidget*>(spec.key);
        QVERIFY2(editor, qPrintable(spec.key));
        QVERIFY2(!editor->toolTip().isEmpty(), qPrintable(spec.key));
    }
}

void TestPreferences::editsApplyImmediately()
{
    Settings s;
    PreferencesDialog d(s);
    auto* autostart = d.findChild<QCheckBox*>(QStringLiteral("general.autostart"));
    QVERIFY(autostart);
    autostart->setChecked(true);
    QCOMPARE(s.boolValue(QStringLiteral("general.autostart")), true);
    auto* zoom = d.findChild<QSpinBox*>(QStringLiteral("capture.magnifierZoom"));
    zoom->setValue(12);
    QCOMPARE(s.intValue(QStringLiteral("capture.magnifierZoom")), 12);
    auto* mode = d.findChild<QComboBox*>(QStringLiteral("capture.detectMode"));
    mode->setCurrentIndex(1);
    QCOMPARE(s.stringValue(QStringLiteral("capture.detectMode")), QStringLiteral("window"));
}

QTEST_MAIN(TestPreferences)
#include "tst_preferences.moc"
