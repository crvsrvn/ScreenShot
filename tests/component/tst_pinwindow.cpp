// REQ: NFR-04 PIN-20 PIN-22 PIN-42
// 贴图窗口原型：缩放保持锚点、拖动不重绘、超出桌面时裁剪窗口
#include "app/PinWindow.h"

#include <QApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace ss;

class TestPinWindow : public QObject {
    Q_OBJECT
private slots:
    void initialGeometryIncludesShadow();
    void zoomKeepsAnchor();
    void moveDoesNotRepaint();
    void clippedToDesktop();
    void closeSignal();
};

static QImage testImage(int w = 400, int h = 300)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::darkCyan);
    return img;
}

void TestPinWindow::initialGeometryIncludesShadow()
{
    PinWindow pin(testImage());
    pin.moveContentTo(QPointF(100, 100)); // 远离桌面边缘，阴影不被裁剪
    QCOMPARE(pin.scale(), 1.0);
    QVERIFY(!pin.isClipped());
    QCOMPARE(pin.size(), QSize(400 + 48, 300 + 48));
    QCOMPARE(pin.pos(), QPoint(100 - 24, 100 - 24));
}

void TestPinWindow::zoomKeepsAnchor()
{
    PinWindow pin(testImage());
    pin.moveContentTo(QPointF(100, 100));
    // 以内容中心为锚点放大 1.5 倍：内容中心的桌面位置不变（offscreen 平台的屏幕为 800×600，窗口会被裁剪）
    const QPointF anchorDesktop(300, 250);
    pin.zoomAt(anchorDesktop - pin.pos(), 1.5, false);
    QCOMPARE(pin.scale(), 1.5);
    QCOMPARE(pin.contentRect().size(), QSizeF(600, 450));
    QCOMPARE(pin.contentRect().center(), anchorDesktop);
}

void TestPinWindow::moveDoesNotRepaint()
{
    PinWindow pin(testImage(200, 150));
    pin.moveContentTo(QPointF(100, 100));
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    QApplication::processEvents();
    QVERIFY(!pin.isClipped());
    const int before = pin.paintCount();
    for (int i = 0; i < 20; ++i) {
        pin.moveContentTo(pin.contentTopLeft() + QPointF(3, 2));
        QApplication::processEvents();
    }
    QCOMPARE(pin.paintCount(), before);
}

void TestPinWindow::clippedToDesktop()
{
    // 内容远大于桌面：窗口只覆盖可见部分
    QRect desktop;
    for (QScreen* s : QGuiApplication::screens())
        desktop = desktop.united(s->geometry());
    qInfo() << "offscreen 桌面" << desktop;
    PinWindow pin(testImage(4000, 3000));
    pin.moveContentTo(QPointF(-100, -100));
    QVERIFY(pin.isClipped());
    QVERIFY2(pin.width() <= desktop.width(), qPrintable(QString::number(pin.width())));
    QVERIFY2(pin.height() <= desktop.height(), qPrintable(QString::number(pin.height())));
    pin.fitToScreen();
    QVERIFY(pin.scale() < 1.0);
    QVERIFY(pin.contentRect().width() <= QGuiApplication::primaryScreen()->availableGeometry().width() * 0.9 + 1);
}

void TestPinWindow::closeSignal()
{
    PinWindow pin(testImage());
    QSignalSpy closed(&pin, &PinWindow::closed);
    QTest::keyClick(&pin, Qt::Key_Escape);
    QCOMPARE(closed.count(), 1);
}

QTEST_MAIN(TestPinWindow)
#include "tst_pinwindow.moc"
