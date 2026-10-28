// REQ: REC-01 REC-06 ACT-05 ACT-06 CFG-05
// M5 其他 P1：OCR 结果窗口与 Shift+C、二维码识别（截图 / 贴图 / 文件）、贴图鼠标动作、命令行接口、主题
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/CaptureToolbar.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "app/ResultDialogs.h"
#include "app/Theme.h"
#include "core/code/CodeDecoder.h"
#include "core/config/Settings.h"
#include "support/TestSupport.h"
#include "testing/FakeHotkey.h"
#include "testing/FakeOcr.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

using namespace ss;
using namespace ss::testing;

namespace {

void sendKey(QWidget* w, const QString& seq)
{
    const QKeyCombination c = QKeySequence(seq)[0];
    QKeyEvent e(QEvent::KeyPress, c.key(), c.keyboardModifiers());
    QApplication::sendEvent(w, &e);
}

QImage testImage(int w = 200, int h = 150)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(qRgb(30, 60, 90));
    return img;
}

// 把二维码画进第一台显示器的画面里（显示器内像素坐标 at；第一台显示器原点为 0,0、缩放 150%）
void paintQr(FakeScreenCapture* capture, const QString& text, QPoint at)
{
    const ScreenInfo screen = capture->screens().first();
    QImage img = FakeScreenCapture::patternImage(0, screen.physicalGeometry.size());
    QPainter p(&img);
    p.drawImage(at, encodeQrCode(text, 3));
    p.end();
    capture->setImage(0, img);
}

} // namespace

class TestMiscP1 : public QObject {
    Q_OBJECT
private slots:
    void qrEncodeDecodeRoundTrip();
    void qrDuringCaptureModes();
    void qrFromPinAndFile();
    void ocrShiftCAndResultDialog();
    void pinMouseActions();
    void commandLineInterface();
    void themeAndAccent();
};

void TestMiscP1::qrEncodeDecodeRoundTrip()
{
    const QImage qr = encodeQrCode(QStringLiteral("https://example.com/path?x=1"), 4);
    QVERIFY(qr.width() > 50);
    const QList<CodeResult> codes = decodeCodes(qr);
    QCOMPARE(codes.size(), 1);
    QCOMPARE(codes[0].text, QStringLiteral("https://example.com/path?x=1"));
    QCOMPARE(codes[0].format, QStringLiteral("QRCode"));
    QVERIFY(codes[0].isUrl());
    QVERIFY(codes[0].bounds.width() > 20);
    // 多个码
    QImage two(400, 150, QImage::Format_RGB32);
    two.fill(Qt::white);
    {
        QPainter p(&two);
        p.drawImage(10, 10, encodeQrCode(QStringLiteral("one"), 3));
        p.drawImage(220, 10, encodeQrCode(QStringLiteral("two"), 3));
    }
    const QList<CodeResult> both = decodeCodes(two);
    QCOMPARE(both.size(), 2);
    QStringList texts{both[0].text, both[1].text};
    texts.sort();
    QCOMPARE(texts, (QStringList{QStringLiteral("one"), QStringLiteral("two")}));
    QVERIFY(!both[0].isUrl());
    QVERIFY(decodeCodes(testImage()).isEmpty());
    // 结果窗口
    CodeResultDialog dialog(both);
    QCOMPARE(dialog.count(), 2);
    dialog.copySelected();
    QCOMPARE(QApplication::clipboard()->text(), both[0].text);
    QVERIFY(!dialog.selectedIsUrl());
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("open"))->isEnabled());
}

void TestMiscP1::qrDuringCaptureModes()
{
    FakePlatformHandles h;
    Settings s;
    App app(makeFakePlatform(&h), s);
    paintQr(h.screenCapture, QStringLiteral("hello-qr"), QPoint(120, 120));
    QSignalSpy codes(&app.session(), &CaptureSession::codesRequested);
    // auto：选区确定后检测到码，工具栏出现按钮，不自动弹窗
    s.setValue(QStringLiteral("capture.qrDetect"), QStringLiteral("auto"));
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(100, 100, 200, 200));
    QCOMPARE(app.session().state(), CaptureSession::State::Selected);
    QTRY_COMPARE(app.session().detectedCodes().size(), 1);
    QCOMPARE(app.session().detectedCodes()[0].text, QStringLiteral("hello-qr"));
    QCOMPARE(codes.count(), 0);
    QVERIFY(app.session().toolbar()->button(QStringLiteral("codes"))->isVisibleTo(app.session().toolbar()));
    app.session().toolbar()->button(QStringLiteral("codes"))->click();
    QCOMPARE(codes.count(), 1);
    app.session().cancel();
    // off：不检测，按钮隐藏
    s.setValue(QStringLiteral("capture.qrDetect"), QStringLiteral("off"));
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(100, 100, 200, 200));
    QTest::qWait(300);
    QVERIFY(app.session().detectedCodes().isEmpty());
    QVERIFY(!app.session().toolbar()->button(QStringLiteral("codes"))->isVisibleTo(app.session().toolbar()));
    app.session().cancel();
    // manual：按钮总是可见，点击才识别
    s.setValue(QStringLiteral("capture.qrDetect"), QStringLiteral("manual"));
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(100, 100, 200, 200));
    QTest::qWait(300);
    QVERIFY(app.session().detectedCodes().isEmpty());
    QVERIFY(app.session().toolbar()->button(QStringLiteral("codes"))->isVisibleTo(app.session().toolbar()));
    app.session().detectCodes();
    QCOMPARE(codes.count(), 2);
    app.session().cancel();
    // show：直接发出
    s.setValue(QStringLiteral("capture.qrDetect"), QStringLiteral("show"));
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(100, 100, 200, 200));
    QTRY_COMPARE(codes.count(), 3);
    // 选区里没有码：不发，已有结果清空
    app.session().setSelection(QRect(400, 400, 50, 50));
    QTest::qWait(300);
    QCOMPARE(codes.count(), 3);
    QVERIFY(app.session().detectedCodes().isEmpty());
    app.session().cancel();
}

void TestMiscP1::qrFromPinAndFile()
{
    Settings s;
    PinManager m(s, nullptr);
    QSignalSpy codes(&m, &PinManager::codesRequested);
    PinWindow* pin = m.pinImage(encodeQrCode(QStringLiteral("pinned-code"), 3), QPoint(0, 0));
    QVERIFY(pin);
    emit pin->codesRequested(pin->displayedImage()); // 右键菜单项触发的信号
    QCOMPARE(codes.count(), 1);
    QCOMPARE(decodeCodes(codes.at(0).at(0).value<QImage>())[0].text, QStringLiteral("pinned-code"));
    // 图片文件
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("qr.png"));
    QVERIFY(encodeQrCode(QStringLiteral("from-file"), 4).save(path));
    QCOMPARE(decodeCodes(QImage(path))[0].text, QStringLiteral("from-file"));
}

void TestMiscP1::ocrShiftCAndResultDialog()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("ocr.showResult"), false);
    App app(makeFakePlatform(&h), s);
    OcrResult r;
    r.ok = true;
    r.lines = {{QStringLiteral("hello"), QRect()}, {QStringLiteral("world"), QRect()}};
    h.ocr->setResult(r);
    QSignalSpy ocr(&app.session(), &CaptureSession::ocrCopyRequested);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(100, 100, 50, 50));
    QKeyEvent e(QEvent::KeyPress, Qt::Key_C, Qt::ShiftModifier);
    QVERIFY(app.session().handleKeyPress(&e)); // Shift+C：识别并复制，结束
    QCOMPARE(ocr.count(), 1);
    QVERIFY(!app.session().isActive());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hello\nworld"));
    // 结果窗口可编辑、复制
    OcrResultDialog dialog(QStringLiteral("abc"));
    dialog.findChild<QPlainTextEdit*>()->setPlainText(QStringLiteral("edited"));
    QCOMPARE(dialog.text(), QStringLiteral("edited"));
    dialog.copyAll();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("edited"));
    // 贴图右键识别 → 同一路径
    PinWindow* pin = app.pinImage(testImage(), QPoint(0, 0));
    QApplication::clipboard()->clear();
    emit pin->ocrRequested(pin->displayedImage());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("hello\nworld"));
}

void TestMiscP1::pinMouseActions()
{
    Settings s;
    PinManager m(s, nullptr);
    PinWindow* pin = m.pinImage(testImage(), QPoint(0, 0));
    QVERIFY(pin);
    // 双击默认关闭；改成缩略图
    s.setValue(QStringLiteral("pin.mouseDoubleClick"), QStringLiteral("thumbnail"));
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(50, 50), QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(pin, &dbl);
    QVERIFY(pin->thumbnail());
    QCOMPARE(m.pins().size(), 1);
    // 中键改成复制
    s.setValue(QStringLiteral("pin.mouseMiddle"), QStringLiteral("copy"));
    QApplication::clipboard()->clear();
    QMouseEvent mid(QEvent::MouseButtonPress, QPointF(50, 50), QPointF(50, 50), Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(pin, &mid);
    QVERIFY(!QApplication::clipboard()->image().isNull());
    QCOMPARE(pin->scale(), pin->scale()); // 未重置
    // Shift+双击改成穿透；Shift+右键改成置顶
    s.setValue(QStringLiteral("pin.mouseShiftDoubleClick"), QStringLiteral("passthrough"));
    QMouseEvent sdbl(QEvent::MouseButtonDblClick, QPointF(50, 50), QPointF(50, 50), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    QApplication::sendEvent(pin, &sdbl);
    QVERIFY(pin->passthrough());
    s.setValue(QStringLiteral("pin.mouseShiftRight"), QStringLiteral("topmost"));
    pin->runMouseAction(QStringLiteral("topmost"), QPoint());
    QVERIFY(!pin->topmost());
    // none：什么都不做；close：关闭
    pin->runMouseAction(QStringLiteral("none"), QPoint());
    QCOMPARE(m.pins().size(), 1);
    pin->runMouseAction(QStringLiteral("close"), QPoint());
    QApplication::processEvents();
    QCOMPARE(m.pins().size(), 0);
}

void TestMiscP1::commandLineInterface()
{
    FakePlatformHandles h;
    Settings s;
    QTemporaryDir dir;
    App app(makeFakePlatform(&h), s);
    // 贴图：文本 / HTML / 文件 / 位置
    app.runCommand({QStringLiteral("pin"), QStringLiteral("--text"), QStringLiteral("hello"), QStringLiteral("there"), QStringLiteral("--pos"), QStringLiteral("5,6")});
    QCOMPARE(app.pins().size(), 1);
    QCOMPARE(app.pins().last()->textPlain(), QStringLiteral("hello there"));
    QCOMPARE(app.pins().last()->physicalTopLeft(), QPoint(5, 6));
    app.runCommand({QStringLiteral("pin"), QStringLiteral("--html"), QStringLiteral("<b>bold</b>"), QStringLiteral("--pos"), QStringLiteral("0,0")});
    QCOMPARE(app.pins().last()->textPlain(), QStringLiteral("bold"));
    const QString file = dir.filePath(QStringLiteral("a.png"));
    QVERIFY(testImage(40, 30).save(file));
    app.runCommand({QStringLiteral("pin"), QStringLiteral("--file"), file, QStringLiteral("--pos"), QStringLiteral("0,0")});
    QCOMPARE(app.pins().last()->originalImage().size(), QSize(40, 30));
    QCOMPARE(app.pins().size(), 3);
    // 贴图管理
    app.runCommand({QStringLiteral("pins"), QStringLiteral("hide")});
    QVERIFY(app.pinsHidden());
    app.runCommand({QStringLiteral("pins"), QStringLiteral("show")});
    QVERIFY(!app.pinsHidden());
    s.setValue(QStringLiteral("pin.confirmBatch"), false);
    app.runCommand({QStringLiteral("pins"), QStringLiteral("close-all")});
    QApplication::processEvents();
    QCOMPARE(app.pins().size(), 0);
    app.runCommand({QStringLiteral("pins"), QStringLiteral("restore")});
    QCOMPARE(app.pins().size(), 1);
    // 截图：区域 + 延时 0 + 输出组合 + 阴影 + 保持编辑
    QSignalSpy captured(&app.session(), &CaptureSession::captured);
    QApplication::clipboard()->clear();
    app.runCommand({QStringLiteral("snip"), QStringLiteral("10,10,50,40"), QStringLiteral("--output"), QStringLiteral("copy")});
    QTRY_COMPARE(captured.count(), 1);
    QCOMPARE(captured.at(0).at(1).toRect(), QRect(10, 10, 50, 40));
    QCOMPARE(QApplication::clipboard()->image().size(), QSize(50, 40));
    QVERIFY(!app.session().isActive());
    app.runCommand({QStringLiteral("snip"), QStringLiteral("--size"), QStringLiteral("30x20"), QStringLiteral("--pos"), QStringLiteral("5,5"), QStringLiteral("--edit"), QStringLiteral("--shadow")});
    QTRY_VERIFY(app.session().isActive());
    QCOMPARE(app.session().selection(), QRect(5, 5, 30, 20));
    QVERIFY(app.session().shadowEnabled());
    app.session().cancel();
    // 全屏保持编辑
    app.runCommand({QStringLiteral("snip"), QStringLiteral("full"), QStringLiteral("--edit")});
    QTRY_VERIFY(app.session().isActive());
    QVERIFY(app.session().selection().width() > 100);
    app.session().cancel();
    // 未知命令不崩溃
    app.runCommand({QStringLiteral("bogus")});
}

void TestMiscP1::themeAndAccent()
{
    Settings s;
    s.setValue(QStringLiteral("ui.theme"), QStringLiteral("dark"));
    QCOMPARE(theme::resolve(s), QStringLiteral("dark"));
    const QPalette dark = theme::palette(QStringLiteral("dark"));
    QVERIFY(dark.color(QPalette::Window).lightness() < 100);
    QVERIFY(dark.color(QPalette::WindowText).lightness() > 150);
    s.setValue(QStringLiteral("ui.theme"), QStringLiteral("light"));
    QCOMPARE(theme::resolve(s), QStringLiteral("light"));
    QVERIFY(theme::palette(QStringLiteral("light")).color(QPalette::Window).lightness() > 150);
    s.setValue(QStringLiteral("ui.theme"), QStringLiteral("system"));
    QVERIFY(theme::resolve(s) == QLatin1String("light") || theme::resolve(s) == QLatin1String("dark"));
    // 主题色影响激活贴图的阴影；字体
    s.setValue(QStringLiteral("ui.accentColor"), QStringLiteral("#ff00aa00"));
    QCOMPARE(theme::accent(s), QColor(0, 170, 0));
    PinWindow pin(testImage(), s);
    pin.show();
    pin.activateWindow();
    const QColor shadow = pin.shadowColor();
    if (pin.isActiveWindow())
        QCOMPARE(shadow.rgb(), QColor(0, 170, 0).rgb());
    s.setValue(QStringLiteral("ui.font"), QStringLiteral("Arial"));
    theme::apply(s);
    QCOMPARE(QApplication::font().family(), QStringLiteral("Arial"));
}

QTEST_MAIN(TestMiscP1)
#include "tst_misc_p1.moc"
