// REQ: CFG-01 CFG-04 CFG-07 NFR-14
#include "app/PreferencesDialog.h"
#include "core/config/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QTest>

using namespace ss;

class TestPreferences : public QObject {
    Q_OBJECT
private slots:
    void pagesMatchSettingsTable();
    void everyEditorHasTooltip();
    void editsApplyImmediately();
    void editorsAccessible();
    void spinBoxesShowValue();
    void searchFiltersRows();
};

void TestPreferences::pagesMatchSettingsTable()
{
    Settings s;
    PreferencesDialog d(s);
    auto* nav = d.findChild<QListWidget*>(QStringLiteral("nav"));
    QVERIFY(nav);
    QStringList pages;
    for (const auto& spec : settingSpecs())
        if (!pages.contains(spec.page) && spec.page != QLatin1String("hidden")) // CFG-06：高级配置不进设置窗口
            pages.append(spec.page);
    QCOMPARE(nav->count(), pages.size());
    for (int i = 0; i < pages.size(); ++i)
        QCOMPARE(nav->item(i)->text(), PreferencesDialog::pageTitle(pages[i]));
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

void TestPreferences::editorsAccessible()
{
    // NFR-14：每个选项编辑器都有可访问名称与说明，标签与编辑器绑定（屏幕阅读器可读）
    Settings s;
    PreferencesDialog d(s);
    for (const auto& spec : settingSpecs()) {
        if (spec.page == QLatin1String("hidden"))
            continue;
        QWidget* editor = d.findChild<QWidget*>(spec.key);
        QVERIFY2(editor, qPrintable(spec.key));
        QVERIFY2(!editor->accessibleName().isEmpty(), qPrintable(spec.key));
        QVERIFY2(!editor->accessibleDescription().isEmpty(), qPrintable(spec.key));
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

void TestPreferences::spinBoxesShowValue()
{
    // 样式表隐藏上下按钮后，数字框内的编辑框必须仍有显示数值的宽度
    Settings s;
    PreferencesDialog d(s);
    d.show();
    QVERIFY(QTest::qWaitForWindowExposed(&d));
    auto* zoom = d.findChild<QSpinBox*>(QStringLiteral("capture.magnifierZoom"));
    QVERIFY(zoom);
    auto* edit = zoom->findChild<QLineEdit*>();
    QVERIFY(edit);
    QVERIFY2(edit->width() > zoom->width() / 2, qPrintable(QString::number(edit->width())));
}

void TestPreferences::searchFiltersRows()
{
    Settings s;
    PreferencesDialog d(s);
    auto* search = d.findChild<QLineEdit*>(QStringLiteral("search"));
    auto* nav = d.findChild<QListWidget*>(QStringLiteral("nav"));
    QVERIFY(search && nav);
    auto* zoomRow = d.findChild<QSpinBox*>(QStringLiteral("capture.magnifierZoom"))->parentWidget();
    auto* autostartRow = d.findChild<QCheckBox*>(QStringLiteral("general.autostart"))->parentWidget();
    search->setText(QCoreApplication::translate("Settings", "Magnifier zoom"));
    QVERIFY(!zoomRow->isHidden());
    QVERIFY(autostartRow->isHidden());
    QVERIFY(nav->item(0)->isHidden()); // 常规页没有匹配项
    QVERIFY(!nav->item(nav->currentRow())->isHidden());
    search->clear();
    QVERIFY(!autostartRow->isHidden());
    QVERIFY(!nav->item(0)->isHidden());
}

QTEST_MAIN(TestPreferences)
#include "tst_preferences.moc"
