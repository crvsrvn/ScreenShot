// REQ: CAP-67 CAP-68
// 滚动拼接 P2：中间悬浮元素不重复、右侧滚动条自动排除
#include "core/stitch/Stitcher.h"

#include <QPainter>
#include <QTest>

using namespace ss::stitch;

namespace {

// 伪随机文档：每行不同
QImage makeDocument(int width, int height, quint32 seed)
{
    QImage doc(width, height, QImage::Format_RGB32);
    quint32 s = seed;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            s = s * 1664525u + 1013904223u;
            doc.setPixel(x, y, qRgb((s >> 8) & 255, (s >> 16) & 255, (y * 3) & 255));
        }
    }
    return doc;
}

// 视口帧：文档偏移 offset；可叠加中间悬浮条（屏幕固定位置）和右侧滚动条（轨道 + 随 offset 移动的滑块）
QImage frameAt(const QImage& doc, int offset, int viewRows, int bannerTop, int bannerRows, int scrollbar)
{
    QImage f = doc.copy(0, offset, doc.width(), viewRows);
    QPainter p(&f);
    if (bannerRows > 0) {
        p.fillRect(QRect(0, bannerTop, f.width(), bannerRows), QColor(255, 200, 0));
        p.setPen(Qt::black);
        p.drawText(QRect(0, bannerTop, f.width(), bannerRows), Qt::AlignCenter, QStringLiteral("FLOATING"));
    }
    if (scrollbar > 0) {
        const QRect track(f.width() - scrollbar, 0, scrollbar, viewRows);
        p.fillRect(track, QColor(230, 230, 230));
        const int thumbH = 40;
        const int thumbY = static_cast<int>(static_cast<double>(offset) / (doc.height() - viewRows) * (viewRows - thumbH));
        p.fillRect(QRect(track.left() + 2, thumbY, scrollbar - 4, thumbH), QColor(120, 120, 120));
    }
    return f;
}

bool rowIsBanner(const QImage& img, int y)
{
    int yellow = 0;
    for (int x = 0; x < img.width(); x += 4)
        if (img.pixel(x, y) == qRgb(255, 200, 0))
            ++yellow;
    return yellow > img.width() / 8;
}

} // namespace

class TestStitchP2 : public QObject {
    Q_OBJECT
private slots:
    void floatingRunsDetected();
    void floatingBannerAppearsOnce();
    void scrollbarWidthDetected();
    void scrollbarExcludedFromOutput();
};

void TestStitchP2::floatingRunsDetected()
{
    const QImage doc = makeDocument(300, 1200, 7);
    const QImage a = frameAt(doc, 0, 400, 150, 30, 0);
    const QImage b = frameAt(doc, 120, 400, 150, 30, 0);
    const auto runs = detectFloatingRuns(rowHashes(a), rowHashes(b), 120, 0, 0);
    QCOMPARE(runs.size(), 1);
    QCOMPARE(runs[0].top, 150);
    QCOMPARE(runs[0].rows, 30);
    // 没有位移时不判断
    QVERIFY(detectFloatingRuns(rowHashes(a), rowHashes(a), 0, 0, 0).isEmpty());
}

void TestStitchP2::floatingBannerAppearsOnce()
{
    const QImage doc = makeDocument(300, 1500, 11);
    Stitcher st;
    st.begin(frameAt(doc, 0, 400, 150, 30, 0));
    int offset = 0;
    for (int step : {120, 110, 130, 100, 120, 110, 100}) {
        offset += step;
        const auto r = st.addFrame(frameAt(doc, offset, 400, 150, 30, 0));
        QVERIFY2(r.shift.ok, qPrintable(r.shift.reason));
        QCOMPARE(r.shift.shift, step);
    }
    QCOMPARE(st.floatingRuns().size(), 1);
    const QImage out = st.finish();
    // 悬浮条最多出现一次（首帧里的那次），不随每帧重复
    int bannerRows = 0;
    for (int y = 0; y < out.height(); ++y)
        if (rowIsBanner(out, y))
            ++bannerRows;
    QVERIFY2(bannerRows <= 30, qPrintable(QString::number(bannerRows)));
    // 其余内容仍对得上文档
    QCOMPARE(out.pixel(10, out.height() - 1), doc.pixel(10, offset + 399));
}

void TestStitchP2::scrollbarWidthDetected()
{
    const QImage doc = makeDocument(320, 1500, 5);
    const QImage a = frameAt(doc, 0, 400, 0, 0, 16);
    const QImage b = frameAt(doc, 100, 400, 0, 0, 16);
    QCOMPARE(detectScrollbarWidth(a, b, 100), 16);
    // 没有滚动条的内容：0
    const QImage c = frameAt(doc, 0, 400, 0, 0, 0);
    const QImage d = frameAt(doc, 100, 400, 0, 0, 0);
    QCOMPARE(detectScrollbarWidth(c, d, 100), 0);
}

void TestStitchP2::scrollbarExcludedFromOutput()
{
    const QImage doc = makeDocument(320, 1500, 9);
    Stitcher st;
    st.begin(frameAt(doc, 0, 400, 0, 0, 16));
    int offset = 0;
    for (int step : {100, 120, 110, 100, 90, 130}) {
        offset += step;
        const auto r = st.addFrame(frameAt(doc, offset, 400, 0, 0, 16));
        QVERIFY2(r.shift.ok, qPrintable(r.shift.reason));
        QCOMPARE(r.shift.shift, step);
    }
    QCOMPARE(st.excludedRightColumns(), 16);
    const QImage out = st.finish();
    QCOMPARE(out.width(), 320 - 16);
    QCOMPARE(out.height(), offset + 400);
    for (int y = 0; y < out.height(); y += 37)
        QCOMPARE(out.pixel(300, y), doc.pixel(300, y));
}

QTEST_MAIN(TestStitchP2) // drawText 需要 QGuiApplication
#include "tst_stitch_p2.moc"
