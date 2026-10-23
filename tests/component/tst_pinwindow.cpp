// REQ: NFR-04 PIN-20 PIN-22
// 贴图窗口原型：缩放保持锚点、拖动不重绘
#include "app/PinWindow.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTest>

using namespace ss;

class TestPinWindow : public QObject {
    Q_OBJECT
private slots:
    void initialGeometryIncludesShadow();
    void zoomKeepsAnchor();
    void moveDoesNotRepaint();
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
    QCOMPARE(pin.scale(), 1.0);
    QCOMPARE(pin.size(), QSize(400 + 48, 300 + 48));
}

void TestPinWindow::zoomKeepsAnchor()
{
    PinWindow pin(testImage());
    pin.move(100, 100);
    // 以内容中心为锚点放大 2 倍：内容中心的屏幕位置不变
    const QPointF anchor(24 + 200, 24 + 150);
    const QPoint anchorGlobal = pin.pos() + anchor.toPoint();
    pin.zoomAt(anchor, 2.0, false);
    QCOMPARE(pin.scale(), 2.0);
    QCOMPARE(pin.size(), QSize(800 + 48, 600 + 48));
    const QPoint newAnchorGlobal = pin.pos() + QPoint(24 + 400, 24 + 300);
    QCOMPARE(newAnchorGlobal, anchorGlobal);
}

void TestPinWindow::moveDoesNotRepaint()
{
    PinWindow pin(testImage());
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    QApplication::processEvents();
    const int before = pin.paintCount();
    for (int i = 0; i < 20; ++i) {
        pin.move(pin.pos() + QPoint(3, 2));
        QApplication::processEvents();
    }
    QCOMPARE(pin.paintCount(), before);
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
