// REQ: GEN-07 CAP-41 PIN-08 PIN-13 PIN-54 AC-07
// M6 PixPin P1：保存通知、区域历史、文件卡片贴图、新贴图位置策略、重启恢复贴图
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/Notification.h"
#include "app/OutputActions.h"
#include "app/PinManager.h"
#include "app/PinWindow.h"
#include "core/config/Settings.h"
#include "core/pin/FilePin.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

} // namespace

class TestPixPinP1 : public QObject {
    Q_OBJECT
private slots:
    void notificationButtons();
    void saveNotifiesAndCanBeDisabled();
    void regionHistoryKeys();
    void fileCardPin();
    void newPinPositionPolicy();
    void savesAndRestoresPins();
};

void TestPixPinP1::notificationButtons()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("shot.png"));
    QVERIFY(testImage().save(path));
    QString revealed;
    NotificationWidget::revealHandler = [&revealed](const QString& p) { revealed = p; };
    auto* n = new NotificationWidget(QStringLiteral("Saved"), path, path, 60000);
    n->show();
    QVERIFY(n->findChild<QLabel*>(QStringLiteral("message")));
    n->findChild<QPushButton*>(QStringLiteral("copyPath"))->click();
    QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
    auto* n2 = new NotificationWidget(QStringLiteral("Saved"), path, path, 60000);
    n2->show();
    n2->findChild<QPushButton*>(QStringLiteral("copyFile"))->click();
    QVERIFY(QApplication::clipboard()->mimeData()->hasUrls());
    QCOMPARE(QApplication::clipboard()->mimeData()->urls().first().toLocalFile(), path);
    auto* n3 = new NotificationWidget(QStringLiteral("Saved"), path, path, 60000);
    n3->show();
    n3->findChild<QPushButton*>(QStringLiteral("reveal"))->click();
    QCOMPARE(revealed, path);
    // 自动消失
    QPointer<NotificationWidget> n4 = new NotificationWidget(QStringLiteral("x"), QStringLiteral("y"), QString(), 50);
    n4->show();
    QTRY_VERIFY(n4.isNull());
}

void TestPixPinP1::saveNotifiesAndCanBeDisabled()
{
    FakePlatformHandles h;
    Settings s;
    QTemporaryDir dir;
    s.setValue(QStringLiteral("output.quickSaveDir"), dir.path());
    App app(makeFakePlatform(&h), s);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(0, 0, 20, 20));
    app.session().quickSave();
    QTRY_VERIFY(!QApplication::topLevelWidgets().isEmpty());
    NotificationWidget* shown = nullptr;
    for (QWidget* w : QApplication::topLevelWidgets())
        if (auto* n = qobject_cast<NotificationWidget*>(w))
            shown = n;
    QVERIFY(shown);
    QVERIFY(shown->filePath().endsWith(QStringLiteral(".png")));
    shown->close();
    s.setValue(QStringLiteral("general.notifications"), false);
    QVERIFY(!app.notify(QStringLiteral("a"), QStringLiteral("b")));
}

void TestPixPinP1::regionHistoryKeys()
{
    FakePlatformHandles h;
    Settings s;
    s.setValue(QStringLiteral("capture.regionHistoryLimit"), 3);
    App app(makeFakePlatform(&h), s);
    CaptureSession& session = app.session();
    for (const QRect& r : {QRect(10, 10, 50, 40), QRect(100, 100, 60, 60), QRect(200, 200, 30, 30), QRect(300, 300, 20, 20)}) {
        QVERIFY(session.start(1, false));
        session.setSelection(r);
        session.accept();
    }
    QCOMPARE(session.regionHistory().size(), 3); // 上限
    QCOMPARE(session.regionHistory().first(), QRect(300, 300, 20, 20));
    QVERIFY(session.start(1, false));
    key(session, QStringLiteral("R"));
    QCOMPARE(session.selection(), QRect(300, 300, 20, 20));
    key(session, QStringLiteral("Shift+R"));
    QCOMPARE(session.selection(), QRect(200, 200, 30, 30));
    key(session, QStringLiteral("Shift+R"));
    QCOMPARE(session.selection(), QRect(100, 100, 60, 60));
    key(session, QStringLiteral("Shift+R")); // 到头回绕
    QCOMPARE(session.selection(), QRect(300, 300, 20, 20));
    key(session, QStringLiteral("R"));
    QCOMPARE(session.selection(), QRect(300, 300, 20, 20));
    // 重复区域不重复记录，最近的在前
    session.setSelection(QRect(100, 100, 60, 60));
    session.accept();
    QCOMPARE(session.regionHistory().size(), 3);
    QCOMPARE(session.regionHistory().first(), QRect(100, 100, 60, 60));
}

void TestPixPinP1::fileCardPin()
{
    QTemporaryDir dir;
    const QString doc = dir.filePath(QStringLiteral("notes.txt"));
    {
        QFile f(doc);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(2048, 'x'));
    }
    const QString img = dir.filePath(QStringLiteral("pic.png"));
    QVERIFY(testImage(40, 30).save(img));
    QCOMPARE(humanFileSize(512), QStringLiteral("512 B"));
    QCOMPARE(humanFileSize(2048), QStringLiteral("2.0 KB"));
    QCOMPARE(humanFileSize(15 * 1024 * 1024), QStringLiteral("15 MB"));
    Settings s;
    PinManager m(s, nullptr);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(doc), QUrl::fromLocalFile(img)});
    PinWindow* last = m.pinMime(&mime, QPoint(0, 0));
    QVERIFY(last);
    QCOMPARE(m.pins().size(), 2);
    PinWindow* card = m.pins().first();
    QCOMPARE(card->contentKind(), PinWindow::ContentKind::File);
    QCOMPARE(card->filePath(), doc);
    QVERIFY(card->originalImage().width() > 200);
    QCOMPARE(m.pins().last()->contentKind(), PinWindow::ContentKind::Image);
    QCOMPARE(m.pins().last()->originalImage().size(), QSize(40, 30));
    card->copyFilePath();
    QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(doc));
    card->copyFile();
    QCOMPARE(QApplication::clipboard()->mimeData()->urls().first().toLocalFile(), doc);
    QVERIFY(card->stateJson().value(QStringLiteral("file")).toString() == doc);
    // 从不贴文件路径
    s.setValue(QStringLiteral("pin.neverPinFilePaths"), true);
    QMimeData only;
    only.setUrls({QUrl::fromLocalFile(doc)});
    QVERIFY(!m.pinMime(&only, QPoint(0, 0)));
    QCOMPARE(m.pins().size(), 2);
}

void TestPixPinP1::newPinPositionPolicy()
{
    Settings s;
    PinManager m(s, nullptr);
    const QSize size(100, 80);
    QCOMPARE(m.resolvePosition(QPoint(40, 50), size, true), QPoint(40, 50)); // capture
    s.setValue(QStringLiteral("pin.newPosition"), QStringLiteral("primaryCenter"));
    const QRect g = QGuiApplication::primaryScreen()->geometry();
    QCOMPARE(m.resolvePosition(QPoint(40, 50), size, true), g.center() - QPoint(50, 40));
    s.setValue(QStringLiteral("pin.newPosition"), QStringLiteral("cursorScreenCenter"));
    QCOMPARE(m.resolvePosition(QPoint(40, 50), size, true), g.center() - QPoint(50, 40)); // offscreen 只有一个屏
    s.setValue(QStringLiteral("pin.newPosition"), QStringLiteral("cursor"));
    const QPoint cursor = QCursor::pos();
    QCOMPARE(m.resolvePosition(QPoint(40, 50), size, true), cursor); // dpr=1
    QCOMPARE(m.resolvePosition(QPoint(40, 50), size, false), QPoint(40, 50)); // 剪贴板贴图本来就是鼠标位置
    // 截图 → 贴图走策略
    FakePlatformHandles h;
    s.setValue(QStringLiteral("pin.newPosition"), QStringLiteral("primaryCenter"));
    App app(makeFakePlatform(&h), s);
    QVERIFY(app.session().start(1, false));
    app.session().setSelection(QRect(10, 10, 100, 80));
    app.session().pin();
    QCOMPARE(app.pins().size(), 1);
    QCOMPARE(app.pins().first()->physicalTopLeft(), g.center() - QPoint(50, 40));
}

void TestPixPinP1::savesAndRestoresPins()
{
    QTemporaryDir dir;
    Settings s;
    {
        PinManager m(s, nullptr);
        PinWindow* a = m.pinImage(testImage(60, 40), QPoint(100, 120));
        a->setScale(2.0, false);
        a->setOpacity(0.7);
        a->rotateQuarter(1);
        // 标注烘焙进显示图
        a->toggleToolbar();
        a->setTool(AnnotationEditor::Tool::Rect);
        a->editor().setFilled(true);
        a->editor().setColor(Qt::red);
        a->editor().pressAt(QPointF(5, 5), Qt::LeftButton, Qt::NoModifier);
        a->editor().moveTo(QPointF(20, 20));
        a->editor().releaseAt(QPointF(20, 20), Qt::LeftButton);
        a->toggleToolbar();
        m.pinText(QStringLiteral("hello"), QPoint(300, 300));
        m.newGroup(QStringLiteral("Work"));
        m.pinColor(QColor(1, 2, 3), QPoint(50, 50));
        m.switchToGroup(0);
        QVERIFY(m.saveState(dir.path()));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("pins.json"))));
    }
    PinManager r(s, nullptr);
    QCOMPARE(r.restoreState(dir.path()), 3);
    QCOMPARE(r.groupNames(), (QStringList{r.groupNames().first(), QStringLiteral("Work")}));
    QCOMPARE(r.currentGroupIndex(), 0);
    QCOMPARE(r.pins().size(), 2);
    PinWindow* a = r.pins().first();
    QCOMPARE(a->physicalTopLeft(), QPoint(120, 100)); // 旋转围绕中心，保存的是旋转后的左上角
    QCOMPARE(a->scale(), 2.0);
    QCOMPARE(a->opacity(), 0.7);
    QCOMPARE(a->originalImage().size(), QSize(40, 60)); // 旋转后的显示图作为底图
    QCOMPARE(a->rotation(), 0);
    QCOMPARE(a->displayedImage().pixel(10, 10), qRgb(255, 0, 0)); // 标注保留
    QCOMPARE(r.pins().last()->contentKind(), PinWindow::ContentKind::Text);
    QCOMPARE(r.pins().last()->textPlain(), QStringLiteral("hello"));
    QVERIFY(r.pins().last()->isVisible());
    r.switchToGroup(1);
    QCOMPARE(r.pins().size(), 1);
    QCOMPARE(r.pins().first()->contentKind(), PinWindow::ContentKind::Color);
    QCOMPARE(r.pins().first()->color(), QColor(1, 2, 3));
    // App 层：退出时保存，启动时恢复（可关闭）
    FakePlatformHandles h;
    QTemporaryDir data;
    {
        App app(makeFakePlatform(&h), s);
        app.setDataDir(data.path());
        app.pinImage(testImage(), QPoint(0, 0));
        QVERIFY(app.pinManager().saveState(data.path() + QStringLiteral("/pins")));
    }
    App app2(makeFakePlatform(&h), s);
    app2.setDataDir(data.path());
    QCOMPARE(app2.restorePins(), 1);
    s.setValue(QStringLiteral("pin.restoreOnStart"), false);
    App app3(makeFakePlatform(&h), s);
    app3.setDataDir(data.path());
    QCOMPARE(app3.restorePins(), 0);
}

QTEST_MAIN(TestPixPinP1)
#include "tst_pixpin_p1.moc"
