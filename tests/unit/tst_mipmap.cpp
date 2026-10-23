// REQ: NFR-04
#include "core/util/Mipmap.h"

#include <QTest>

using namespace ss;

class TestMipmap : public QObject {
    Q_OBJECT
private slots:
    void levelsHalveUntilMin();
    void levelSelection();
    void emptyImage();
};

void TestMipmap::levelsHalveUntilMin()
{
    QImage img(1024, 512, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::red);
    Mipmap m(img, 64);
    // 1024×512 → 512×256 → 256×128 → 128×64 （下一级 64×32 高度不足 64）
    QCOMPARE(m.levelCount(), 4);
    QCOMPARE(m.levelForScale(1.0).size(), QSize(1024, 512));
}

void TestMipmap::levelSelection()
{
    QImage img(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::blue);
    Mipmap m(img, 32);
    QCOMPARE(m.levelIndexForScale(2.0), 0);
    QCOMPARE(m.levelIndexForScale(1.0), 0);
    QCOMPARE(m.levelIndexForScale(0.6), 0);  // 需要 614 px，512 不够 → 用原图
    QCOMPARE(m.levelIndexForScale(0.5), 1);  // 恰好 512
    QCOMPARE(m.levelIndexForScale(0.3), 1);
    QCOMPARE(m.levelIndexForScale(0.25), 2);
    QCOMPARE(m.levelIndexForScale(0.01), m.levelCount() - 1);
}

void TestMipmap::emptyImage()
{
    Mipmap m;
    QVERIFY(m.isEmpty());
    QVERIFY(m.levelForScale(1.0).isNull());
}

QTEST_GUILESS_MAIN(TestMipmap)
#include "tst_mipmap.moc"
