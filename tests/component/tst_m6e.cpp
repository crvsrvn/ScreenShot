// REQ: REC-02 REC-03 REC-04 REC-07 REC-08 ACT-07 ACT-08 ACT-09 ACT-14 ACT-16 CFG-03 CFG-06 CFG-08
// M6 识别 / 动作 / 配置 P2：OCR 语言与模式、结果排版、图像贴图可选文字、工业条码、表格导出、热键忽略前台程序、单键热键、
// 应用内快捷键、切换窗口置顶、粘贴为图像 / 纯文本、配置导入导出、高级配置、打开配置目录
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/Notification.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/PreferencesDialog.h"
#include "app/ResultDialogs.h"
#include "core/code/CodeDecoder.h"
#include "core/config/Settings.h"
#include "core/config/Shortcuts.h"
#include "core/ocr/OcrFormat.h"
#include "core/ocr/TableExtract.h"
#include "core/output/XlsxWriter.h"
#include "support/TestSupport.h"
#include "testing/FakeHotkey.h"
#include "testing/FakeInputSimulator.h"
#include "testing/FakeOcr.h"
#include "testing/FakePlatform.h"
#include "testing/FakeWindowDetector.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_WIN)
#include "platform/windows/WinWindowTools.h"
#include <windows.h>
#endif

using namespace ss;
using namespace ss::testing;

namespace {

QImage testImage(int w = 200, int h = 150)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgb(10, 20, 30));
    return img;
}

void key(CaptureSession& s, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    s.handleKeyPress(&e);
}

void sendKey(QWidget* w, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    QApplication::sendEvent(w, &e);
}

// 记录 configure 调用的 OCR 替身
class RecordingOcr : public FakeOcr {
public:
    void configure(const QString& k, const QString& v) override { calls.append({k, v}); }
    QList<QPair<QString, QString>> calls;
};

} // namespace

class TestM6E : public QObject {
    Q_OBJECT
private slots:
    void ocrOptionsApplied();
    void ocrLayoutAndPunctuation();
    void imagePinSelectableText();
    void industrialBarcodes();
    void tableExtractAndXlsx();
    void hotkeyIgnoredForForegroundProcess();
    void singleKeyHotkeyPolicy();
    void inAppShortcutsRemap();
    void toggleWindowTopmost();
    void pasteAsImageAndText();
    void settingsImportExport();
    void hiddenAdvancedSettings();
    void openSettingsFolder();
};

void TestM6E::ocrOptionsApplied()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("ocr.language"), QStringLiteral("en-US"));
    s.setValue(QStringLiteral("ocr.mode"), QStringLiteral("accurate"));
    Platform p = makeFakePlatform(&h);
    auto* ocr = new RecordingOcr;
    p.ocr.reset(ocr);
    App app(std::move(p), s);
    QVERIFY(ocr->calls.contains(qMakePair(QStringLiteral("language"), QStringLiteral("en-US"))));
    QVERIFY(ocr->calls.contains(qMakePair(QStringLiteral("mode"), QStringLiteral("accurate"))));
    ocr->calls.clear();
    s.setValue(QStringLiteral("ocr.mode"), QStringLiteral("fast"));
    QVERIFY(ocr->calls.contains(qMakePair(QStringLiteral("mode"), QStringLiteral("fast"))));
}

void TestM6E::ocrLayoutAndPunctuation()
{
    QList<OcrLine> lines{{QStringLiteral("Hello world,"), QRect(10, 10, 200, 20)}, {QStringLiteral("second line."), QRect(10, 34, 180, 20)},
                         {QStringLiteral("New paragraph"), QRect(10, 90, 150, 20)}, {QStringLiteral("中文第一行，"), QRect(10, 114, 150, 20)},
                         {QStringLiteral("中文第二行。"), QRect(10, 138, 150, 20)}};
    QCOMPARE(formatOcrText(lines, OcrLayout::Multiline, OcrPunctuation::Keep), QStringLiteral("Hello world,\nsecond line.\nNew paragraph\n中文第一行，\n中文第二行。"));
    QCOMPARE(formatOcrText(lines, OcrLayout::NoNewlines, OcrPunctuation::Keep), QStringLiteral("Hello world, second line. New paragraph 中文第一行，中文第二行。"));
    const QString autoText = formatOcrText(lines, OcrLayout::Auto, OcrPunctuation::Keep);
    QCOMPARE(autoText, QStringLiteral("Hello world, second line.\n\nNew paragraph 中文第一行，中文第二行。"));
    QCOMPARE(convertPunctuation(QStringLiteral("a,b.c?"), OcrPunctuation::FullWidth), QStringLiteral("a，b。c？"));
    QCOMPARE(convertPunctuation(QStringLiteral("a，b。c？“x”"), OcrPunctuation::HalfWidth), QStringLiteral("a,b.c?\"x\""));
    QCOMPARE(formatOcrText(lines, OcrLayout::Multiline, OcrPunctuation::HalfWidth).split(QLatin1Char('\n')).last(), QStringLiteral("中文第二行."));
    // App 走设置
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("ocr.showResult"), false);
    s.setValue(QStringLiteral("ocr.layout"), QStringLiteral("noNewlines"));
    App app(makeFakePlatform(&h), s);
    OcrResult r;
    r.ok = true;
    r.lines = lines.mid(0, 2);
    h.ocr->setResult(r);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(0, 0, 50, 50));
    app.session().ocrCopy();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Hello world, second line."));
}

void TestM6E::imagePinSelectableText()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    OcrResult r;
    r.ok = true;
    r.lines = {{QStringLiteral("alpha"), QRect(10, 10, 60, 16)}, {QStringLiteral("beta"), QRect(10, 40, 60, 16)}, {QStringLiteral("gamma"), QRect(10, 70, 60, 16)}};
    h.ocr->setResult(r);
    PinWindow* pin = app.pinImage(testImage(), QPoint(0, 0));
    pin->setTextSelectionMode(true); // 触发识别
    QCOMPARE(pin->ocrLines().size(), 3);
    pin->selectOcrLinesIn(QRectF(0, 0, 100, 50));
    QCOMPARE(pin->selectedOcrLines(), (QList<int>{0, 1}));
    QCOMPARE(pin->selectedText(), QStringLiteral("alpha\nbeta"));
    sendKey(pin, QStringLiteral("Ctrl+C"));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("alpha\nbeta"));
    std::unique_ptr<QMimeData> mime(pin->makeDragMimeData());
    QCOMPARE(mime->text(), QStringLiteral("alpha\nbeta"));
    // 标注时右键框出文字行
    pin->setTextSelectionMode(false);
    pin->setTool(AnnotationEditor::Tool::Rect);
    QMouseEvent press(QEvent::MouseButtonPress, pin->contentRect().translated(-pin->pos()).topLeft() + QPointF(20, 45), QPointF(), Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(pin, &press);
    QCOMPARE(pin->editor().document().count(), 1);
    QCOMPARE(pin->editor().document().at(0)->bounds(), QRectF(8, 38, 64, 20));
    // 关闭后不识别
    s.setValue(QStringLiteral("ocr.pinSelectText"), false);
    PinWindow* other = app.pinImage(testImage(), QPoint(0, 0));
    other->setTextSelectionMode(true);
    QVERIFY(!other->textSelectionMode());
    QVERIFY(other->ocrLines().isEmpty());
}

void TestM6E::industrialBarcodes()
{
    struct Case {
        QString format;   // 生成用名称
        QString shown;    // 识别结果里的显示名
        QString text;
    };
    const QList<Case> cases{{QStringLiteral("Code128"), QStringLiteral("Code128"), QStringLiteral("ABC-123")}, {QStringLiteral("EAN13"), QStringLiteral("EAN-13"), QStringLiteral("4006381333931")},
                            {QStringLiteral("UPCA"), QStringLiteral("UPC-A"), QStringLiteral("012345678905")}, {QStringLiteral("Code39"), QStringLiteral("Code39"), QStringLiteral("HELLO39")},
                            {QStringLiteral("DataMatrix"), QStringLiteral("DataMatrix"), QStringLiteral("matrix data")}, {QStringLiteral("PDF417"), QStringLiteral("PDF417"), QStringLiteral("pdf417 payload")}};
    for (const Case& c : cases) {
        const QImage img = encodeBarcode(c.text, c.format);
        QVERIFY2(!img.isNull(), qPrintable(c.format));
        const QList<CodeResult> codes = decodeCodes(img);
        QVERIFY2(codes.size() == 1, qPrintable(c.format));
        QCOMPARE(codes[0].text, c.text);
        QCOMPARE(codes[0].format, c.shown);
    }
}

void TestM6E::tableExtractAndXlsx()
{
    QList<OcrLine> lines{{QStringLiteral("Name"), QRect(10, 10, 40, 16)}, {QStringLiteral("Qty"), QRect(120, 11, 30, 16)}, {QStringLiteral("Price"), QRect(220, 9, 40, 16)},
                         {QStringLiteral("Apple"), QRect(10, 40, 45, 16)}, {QStringLiteral("3"), QRect(122, 41, 10, 16)}, {QStringLiteral("1.50"), QRect(221, 40, 30, 16)},
                         {QStringLiteral("Pear"), QRect(11, 70, 40, 16)}, {QStringLiteral("2.00"), QRect(220, 70, 30, 16)}};
    const QList<QStringList> table = extractTable(lines);
    QCOMPARE(table.size(), 3);
    QCOMPARE(table[0], (QStringList{QStringLiteral("Name"), QStringLiteral("Qty"), QStringLiteral("Price")}));
    QCOMPARE(table[1], (QStringList{QStringLiteral("Apple"), QStringLiteral("3"), QStringLiteral("1.50")}));
    QCOMPARE(table[2], (QStringList{QStringLiteral("Pear"), QString(), QStringLiteral("2.00")}));
    // xlsx：合法的存储式 zip，含工作表与单元格文本
    const QByteArray xlsx = buildXlsx(table);
    QVERIFY(xlsx.startsWith("PK\x03\x04"));
    QVERIFY(xlsx.contains("xl/worksheets/sheet1.xml"));
    QVERIFY(xlsx.contains("<t xml:space=\"preserve\">Apple</t>"));
    QVERIFY(xlsx.contains("r=\"C3\""));
    QVERIFY(xlsx.contains("[Content_Types].xml"));
    QVERIFY(xlsx.endsWith(QByteArray::fromHex("0000"))); // 结尾记录的注释长度
    QTemporaryDir dir;
    OcrResultDialog dialog(QStringLiteral("x"));
    dialog.setLines(lines);
    const QString path = dir.filePath(QStringLiteral("t.xlsx"));
    QVERIFY(dialog.exportTable(path));
    QVERIFY(QFileInfo(path).size() > 500);
}

void TestM6E::hotkeyIgnoredForForegroundProcess()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("hotkey.ignoreProcesses"), QStringLiteral("game.exe, other.exe"));
    App app(makeFakePlatform(&h), s);
    h.windowDetector->setActiveWindow(WindowInfo{1, QStringLiteral("Game"), QStringLiteral("Game.exe"), QRect(0, 0, 100, 100), 0});
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(!app.session().isActive());
    h.windowDetector->setActiveWindow(WindowInfo{2, QStringLiteral("Editor"), QStringLiteral("editor.exe"), QRect(0, 0, 100, 100), 0});
    h.hotkey->trigger(App::HotkeyCapture);
    QVERIFY(app.session().isActive());
    app.session().cancel();
}

void TestM6E::singleKeyHotkeyPolicy()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    s.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("A")); // 单键：默认拒绝
    QVERIFY(!h.hotkey->isRegistered(App::HotkeyCapture));
    s.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("F9")); // 功能键总是允许
    QVERIFY(h.hotkey->isRegistered(App::HotkeyCapture));
    s.setValue(QStringLiteral("hotkey.capture"), QStringLiteral("A"));
    s.setValue(QStringLiteral("hotkey.allowSingleKey"), true);
    QVERIFY(h.hotkey->isRegistered(App::HotkeyCapture));
}

void TestM6E::inAppShortcutsRemap()
{
    ShortcutMap defaults;
    QCOMPARE(defaults.keyFor(QStringLiteral("capture"), QStringLiteral("copy")), QKeySequence(QStringLiteral("Ctrl+C")));
    QCOMPARE(defaults.actionFor(QStringLiteral("pin"), Qt::Key_W, Qt::ControlModifier), QStringLiteral("close"));
    QVERIFY(defaults.actionFor(QStringLiteral("pin"), Qt::Key_W, Qt::NoModifier).isEmpty());
    ShortcutMap custom;
    custom.set(QStringLiteral("capture"), QStringLiteral("quickSave"), QKeySequence(QStringLiteral("Ctrl+K")));
    const ShortcutMap reloaded(custom.toJson());
    QCOMPARE(reloaded.actionFor(QStringLiteral("capture"), Qt::Key_K, Qt::ControlModifier), QStringLiteral("quickSave"));
    // 截图：Ctrl+K 快速保存
    FakePlatformHandles h;
    Settings s;
    QTemporaryDir dir;
    s.setValue(QStringLiteral("output.quickSaveDir"), dir.path());
    s.setValue(QStringLiteral("shortcuts.map"), reloaded.toJson());
    App app(makeFakePlatform(&h), s);
    QSignalSpy done(&app.output(), &OutputActions::done);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(0, 0, 30, 30));
    key(app.session(), QStringLiteral("Ctrl+K"));
    QCOMPARE(done.count(), 1);
    QCOMPARE(done.at(0).at(0).value<OutputActions::Kind>(), OutputActions::Kind::QuickSave);
    // 贴图：把“锁定”改到 Ctrl+Shift+L；图像贴图的灰度改到 G
    ShortcutMap pinMap;
    pinMap.set(QStringLiteral("pin"), QStringLiteral("lock"), QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    pinMap.set(QStringLiteral("pinImage"), QStringLiteral("grayscale"), QKeySequence(QStringLiteral("G")));
    s.setValue(QStringLiteral("shortcuts.map"), pinMap.toJson());
    PinWindow* pin = app.pinImage(testImage(), QPoint(0, 0));
    sendKey(pin, QStringLiteral("L"));
    QVERIFY(!pin->locked()); // 旧键不再生效
    sendKey(pin, QStringLiteral("Ctrl+Shift+L"));
    QVERIFY(pin->locked());
    sendKey(pin, QStringLiteral("G"));
    QVERIFY(pin->grayscale());
}

void TestM6E::toggleWindowTopmost()
{
#if defined(Q_OS_WIN)
    if (QGuiApplication::platformName() == QLatin1String("offscreen"))
        QSKIP("needs real windows");
    QWidget w;
    w.resize(200, 200);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    void* hwnd = reinterpret_cast<void*>(w.winId());
    QVERIFY(!win::isWindowTopmost(hwnd));
    // offscreen 平台没有真实窗口位置：直接对句柄切换两次
    SetWindowPos(static_cast<HWND>(hwnd), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    QVERIFY(win::isWindowTopmost(hwnd));
    SetWindowPos(static_cast<HWND>(hwnd), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    QVERIFY(!win::isWindowTopmost(hwnd));
    win::toggleWindowTopmostAt(QPoint(-100000, -100000)); // 没有窗口：不崩溃
#else
    QSKIP("Windows only");
#endif
}

void TestM6E::pasteAsImageAndText()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("plain words"));
    mime->setHtml(QStringLiteral("<b>plain</b> words"));
    QApplication::clipboard()->setMimeData(mime);
    app.pasteAs(QStringLiteral("text"));
    QVERIFY(!QApplication::clipboard()->mimeData()->hasHtml());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("plain words"));
    QCOMPARE(h.input->keyCombos().size(), 1);
    QCOMPARE(h.input->keyCombos().first().second, Qt::Key_V);
    app.pasteAs(QStringLiteral("image"));
    QVERIFY(!QApplication::clipboard()->image().isNull()); // 文本渲染成图像
    QCOMPARE(h.input->keyCombos().size(), 2);
    app.runCommand({QStringLiteral("paste-as"), QStringLiteral("text")});
    QCOMPARE(h.input->keyCombos().size(), 2); // 剪贴板是图像、没有文本：不粘贴
}

void TestM6E::settingsImportExport()
{
    Settings a;
    a.setValue(QStringLiteral("pin.zoomStep"), 33);
    a.setValue(QStringLiteral("general.language"), QStringLiteral("en"));
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("export.json"));
    QVERIFY(a.exportTo(path));
    Settings b;
    QCOMPARE(b.intValue(QStringLiteral("pin.zoomStep")), 10);
    QVERIFY(b.importFrom(path));
    QCOMPARE(b.intValue(QStringLiteral("pin.zoomStep")), 33);
    QCOMPARE(b.stringValue(QStringLiteral("general.language")), QStringLiteral("en"));
    QVERIFY(!b.importFrom(dir.filePath(QStringLiteral("missing.json"))));
}

void TestM6E::hiddenAdvancedSettings()
{
    Settings s;
    QVERIFY(s.boolValue(QStringLiteral("advanced.animations")));
    QCOMPARE(s.intValue(QStringLiteral("advanced.notificationMs")), 5000);
    QVERIFY(settingSpec(QStringLiteral("advanced.elementTimeoutMs")));
    PreferencesDialog dialog(s);
    auto* tabs = dialog.findChild<QTabWidget*>();
    QVERIFY(tabs);
    for (int i = 0; i < tabs->count(); ++i)
        QVERIFY(tabs->tabText(i) != QLatin1String("hidden") && tabs->tabText(i) != PreferencesDialog::pageTitle(QStringLiteral("hidden")));
    // 配置文件里仍可改
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("s.json"));
    s.setValue(QStringLiteral("advanced.notificationMs"), 900);
    QVERIFY(s.save(path));
    Settings t;
    QVERIFY(t.load(path));
    QCOMPARE(t.intValue(QStringLiteral("advanced.notificationMs")), 900);
}

void TestM6E::openSettingsFolder()
{
    QString revealed;
    NotificationWidget::revealHandler = [&revealed](const QString& p) { revealed = p; };
    FakePlatformHandles h;
    Settings s;
    QTemporaryDir dir;
    s.setFilePath(dir.filePath(QStringLiteral("settings.json")));
    App app(makeFakePlatform(&h), s);
    app.openSettingsFolder();
    QCOMPARE(revealed, dir.filePath(QStringLiteral("settings.json")));
    app.runCommand({QStringLiteral("prefs-dir")});
    QCOMPARE(revealed, dir.filePath(QStringLiteral("settings.json")));
}

QTEST_MAIN(TestM6E)
#include "tst_m6e.moc"
