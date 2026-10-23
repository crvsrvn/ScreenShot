// REQ: GEN-13 NFR-13
// 翻译文件外置：从构建目录的 translations/ 加载，中文与英文都能翻译关键文案
#include <QCoreApplication>
#include <QDir>
#include <QTest>
#include <QTranslator>

class TestI18n : public QObject {
    Q_OBJECT
private slots:
    void chineseTranslationLoads();
    void englishTranslationLoads();
    void allSourceStringsTranslated();
};

static QString translationsDir()
{
    return QString::fromLocal8Bit(qgetenv("SS_TRANSLATIONS_DIR"));
}

void TestI18n::chineseTranslationLoads()
{
    QTranslator t;
    QVERIFY2(t.load(QStringLiteral("screenshot_zh_CN"), translationsDir()), qPrintable(translationsDir()));
    QCOMPARE(t.translate("ss::CaptureToolbar", "Copy"), QStringLiteral("复制"));
    QCOMPARE(t.translate("Settings", "Capture hotkey"), QStringLiteral("截图热键"));
    QCOMPARE(t.translate("ss::PreferencesDialog", "General"), QStringLiteral("常规"));
}

void TestI18n::englishTranslationLoads()
{
    QTranslator t;
    QVERIFY(t.load(QStringLiteral("screenshot_en"), translationsDir()));
    QCOMPARE(t.translate("ss::CaptureToolbar", "Copy"), QStringLiteral("Copy"));
}

void TestI18n::allSourceStringsTranslated()
{
    // .ts 里不允许留下未翻译条目
    QFile f(QString::fromLocal8Bit(qgetenv("SS_SOURCE_DIR")) + QStringLiteral("/translations/screenshot_zh_CN.ts"));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString xml = QString::fromUtf8(f.readAll());
    QVERIFY(!xml.contains(QStringLiteral("type=\"unfinished\"")));
}

QTEST_GUILESS_MAIN(TestI18n)
#include "tst_i18n.moc"
