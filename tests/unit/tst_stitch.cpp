// REQ: CAP-61 CAP-63 CAP-65 CAP-66 CAP-70 CAP-74 NFR-06 TST-07 AC-10
// 滚动拼接算法：合成长内容逐帧滚动 → 拼接结果与原内容逐像素一致；固定栏只出现一次；
// 纯色 / 重复内容 / 滚动过快时拒绝；反向滚动裁剪；横向拼接；随机化长跑；分块长图
#include "core/stitch/Stitcher.h"
#include "core/util/Clock.h"
#include "support/TestSupport.h"

#include <QPainter>
#include <QRandomGenerator>
#include <QTest>

using namespace ss;
using namespace ss::stitch;
using namespace ss::testing;

namespace {

// 伪随机“文档”：每行有不同的随机像素块（类似文字），行间有空白
QImage makeDocument(int width, int height, quint32 seed)
{
    QImage doc(width, height, QImage::Format_RGB32);
    doc.fill(Qt::white);
    QRandomGenerator rng(seed);
    for (int y = 0; y < height; ++y) {
        if (y % 7 == 0)
            continue; // 空行
        auto* line = reinterpret_cast<QRgb*>(doc.scanLine(y));
        int x = static_cast<int>(rng.bounded(10));
        while (x < width) {
            const int len = static_cast<int>(rng.bounded(3, 40));
            const QRgb c = qRgb(static_cast<int>(rng.bounded(200)), static_cast<int>(rng.bounded(200)), static_cast<int>(rng.bounded(200)));
            for (int i = 0; i < len && x < width; ++i, ++x)
                line[x] = c;
            x += static_cast<int>(rng.bounded(2, 20));
        }
    }
    return doc;
}

// 模拟带固定头尾栏的滚动窗口：视口显示 doc 的 [offset, offset+viewRows)，顶部 header 行、底部 footer 行是固定内容
QImage frameAt(const QImage& doc, int offset, int viewRows, int header, int footer)
{
    QImage f(doc.width(), viewRows + header + footer, QImage::Format_RGB32);
    QPainter p(&f);
    p.fillRect(0, 0, f.width(), header, QColor(40, 40, 80));
    p.setPen(Qt::white);
    p.drawText(QRect(4, 0, f.width(), header), Qt::AlignVCenter, QStringLiteral("Fixed header"));
    p.drawImage(0, header, doc, 0, offset, doc.width(), viewRows);
    p.fillRect(0, header + viewRows, f.width(), footer, QColor(80, 40, 40));
    p.drawText(QRect(4, header + viewRows, f.width(), footer), Qt::AlignVCenter, QStringLiteral("Fixed footer"));
    return f;
}

} // namespace

class TestStitch : public QObject {
    Q_OBJECT
private slots:
    void rowHashesAndFixedRegions();
    void stitchesScrollingDocumentExactly();
    void fixedBarsAppearOnce();
    void rejectsUniformAndRepeatedContent();
    void rejectsScrollTooFast();
    void reverseScrollTruncates();
    void horizontalViaTranspose();
    void longImageChunks();
    void randomizedScrollRun();
    void perFrameBudget();
};

void TestStitch::rowHashesAndFixedRegions()
{
    const QImage doc = makeDocument(300, 400, 1);
    const QVector<quint64> h = rowHashes(doc);
    QCOMPARE(h.size(), 400);
    QVERIFY(h[1] != h[2]);
    const QImage a = frameAt(doc, 0, 200, 20, 15);
    const QImage b = frameAt(doc, 50, 200, 20, 15);
    const FixedRegions f = detectFixedRegions(rowHashes(a), rowHashes(b));
    QCOMPARE(f.top, 20);
    QCOMPARE(f.bottom, 15);
    // 排除最右侧 20 列（滚动条）后哈希仍然逐行不同
    const QVector<quint64> partial = rowHashes(doc, 0, 280);
    QVERIFY(partial[1] != h[1] || partial[2] != h[2]);
}

void TestStitch::stitchesScrollingDocumentExactly()
{
    const QImage doc = makeDocument(320, 3000, 7);
    const int view = 240, header = 24, footer = 18;
    Stitcher st;
    st.begin(frameAt(doc, 0, view, header, footer));
    int offset = 0;
    const int steps[] = {60, 130, 45, 200, 90, 15, 180};
    for (int s : steps) {
        offset += s;
        const auto r = st.addFrame(frameAt(doc, offset, view, header, footer));
        QVERIFY2(r.appended, qPrintable(r.shift.reason));
        QCOMPARE(r.shift.shift, s);
    }
    const QImage out = st.finish();
    QCOMPARE(out.height(), header + offset + view + footer);
    // 中间内容与文档逐像素一致
    SS_VERIFY_IMAGE(out.copy(0, header, 320, offset + view), doc.copy(0, 0, 320, offset + view), CompareOptions{}, "stitch_exact");
}

void TestStitch::fixedBarsAppearOnce()
{
    const QImage doc = makeDocument(300, 1500, 3);
    Stitcher st;
    st.begin(frameAt(doc, 0, 200, 30, 20));
    for (int off = 100; off <= 700; off += 100)
        QVERIFY(st.addFrame(frameAt(doc, off, 200, 30, 20)).appended);
    QCOMPARE(st.fixedRegions().top, 30);
    QCOMPARE(st.fixedRegions().bottom, 20);
    const QImage out = st.finish();
    // 头部只在最上面出现：第 30 行之后不再有头栏的颜色
    int headerRows = 0;
    for (int y = 0; y < out.height(); ++y)
        if (out.pixel(150, y) == qRgb(40, 40, 80))
            ++headerRows;
    QVERIFY(headerRows <= 30);
    QCOMPARE(out.pixel(150, out.height() - 1), qRgb(80, 40, 40)); // 尾栏在末尾出现一次
}

void TestStitch::rejectsUniformAndRepeatedContent()
{
    // 纯色
    QImage flat(300, 400, QImage::Format_RGB32);
    flat.fill(Qt::white);
    Stitcher st;
    st.begin(flat.copy(0, 0, 300, 200));
    QImage flat2 = flat.copy(0, 0, 300, 200);
    flat2.setPixel(5, 5, qRgb(0, 0, 0)); // 微小变化使两帧不同
    const auto r = st.addFrame(flat2);
    QVERIFY(!r.appended);
    QVERIFY(!r.shift.reason.isEmpty());
    // 完全重复的列表：每 20 行重复
    QImage rep(300, 400, QImage::Format_RGB32);
    const QImage unit = makeDocument(300, 20, 9);
    for (int y = 0; y < 400; ++y)
        std::memcpy(rep.scanLine(y), unit.constScanLine(y % 20), 300 * 4);
    Stitcher st2;
    st2.begin(rep.copy(0, 0, 300, 200));
    const auto r2 = st2.addFrame(rep.copy(0, 7, 300, 200));
    QVERIFY(!r2.appended); // 没有唯一锚点 → 拒绝而不是拼错
}

void TestStitch::rejectsScrollTooFast()
{
    const QImage doc = makeDocument(300, 2000, 11);
    Stitcher st;
    st.begin(frameAt(doc, 0, 200, 0, 0));
    const auto r = st.addFrame(frameAt(doc, 600, 200, 0, 0)); // 完全没有重叠
    QVERIFY(!r.appended);
    QVERIFY(!r.shift.reason.isEmpty());
    QCOMPARE(st.image().height(), 200); // 长图未被污染
}

void TestStitch::reverseScrollTruncates()
{
    const QImage doc = makeDocument(300, 2000, 5);
    Stitcher st;
    st.begin(frameAt(doc, 0, 200, 10, 10));
    QVERIFY(st.addFrame(frameAt(doc, 100, 200, 10, 10)).appended);
    QVERIFY(st.addFrame(frameAt(doc, 200, 200, 10, 10)).appended);
    QCOMPARE(st.image().height(), 10 + 400 + 10);
    const auto back = st.addFrame(frameAt(doc, 150, 200, 10, 10));
    QCOMPARE(back.shift.shift, -50);
    QCOMPARE(st.image().height(), 10 + 350 + 10);
    SS_VERIFY_IMAGE(st.finish().copy(0, 10, 300, 350), doc.copy(0, 0, 300, 350), CompareOptions{}, "stitch_reverse");
}

void TestStitch::horizontalViaTranspose()
{
    const QImage doc = transposed(makeDocument(200, 1200, 13)); // 1200 宽 × 200 高的横向内容
    Stitcher st;
    st.begin(transposed(doc.copy(0, 0, 300, 200)));
    int off = 0;
    for (int s : {80, 120, 60}) {
        off += s;
        const auto r = st.addFrame(transposed(doc.copy(off, 0, 300, 200)));
        QVERIFY2(r.appended, qPrintable(r.shift.reason));
        QCOMPARE(r.shift.shift, s);
    }
    const QImage out = transposed(st.finish());
    QCOMPARE(out.size(), QSize(off + 300, 200));
    SS_VERIFY_IMAGE(out, doc.copy(0, 0, off + 300, 200), CompareOptions{}, "stitch_horizontal");
}

void TestStitch::longImageChunks()
{
    LongImage li(50, 100);
    const QImage src = makeDocument(50, 250, 2);
    li.appendRows(src, 0, 250);
    QCOMPARE(li.height(), 250);
    QCOMPARE(li.pixel(10, 249), src.pixel(10, 249));
    SS_VERIFY_IMAGE(li.rows(90, 30), src.copy(0, 90, 50, 30), CompareOptions{}, "longimage_rows");
    li.truncate(120);
    QCOMPARE(li.height(), 120);
    li.removeTop(20);
    QCOMPARE(li.height(), 100);
    QCOMPARE(li.pixel(3, 0), src.pixel(3, 20));
    QVERIFY(li.byteCount() <= 50 * 4 * 100 + 50 * 4 * 100);
    const QImage thumb = li.thumbnail(25, 1000);
    QCOMPARE(thumb.width(), 25);
    // 超长：100 万行按块存放，内存与行数线性（CAP-74、NFR-06）
    LongImage huge(8, 4096);
    QImage strip(8, 4096, QImage::Format_RGB32);
    strip.fill(Qt::blue);
    for (int i = 0; i < 250; ++i)
        huge.appendRows(strip, 0, 4096);
    QCOMPARE(huge.height(), 1024000);
    QCOMPARE(huge.byteCount(), static_cast<qint64>(1024000) * 8 * 4);
}

void TestStitch::randomizedScrollRun()
{
    // TST-07：随机步长（含反向、零步、过快）长时间运行，结果始终与文档一致或被拒绝
    const QImage doc = makeDocument(260, 6000, 21);
    QRandomGenerator rng(99);
    Stitcher st;
    st.begin(frameAt(doc, 0, 220, 16, 12));
    int offset = 0;
    int accepted = 0;
    for (int i = 0; i < 150; ++i) {
        int step = static_cast<int>(rng.bounded(-80, 200));
        if (offset + step < 0)
            step = -offset;
        if (offset + step > 6000 - 220)
            step = 6000 - 220 - offset;
        const QImage f = frameAt(doc, offset + step, 220, 16, 12);
        const auto r = st.addFrame(f);
        if (r.shift.ok) {
            QCOMPARE(r.shift.shift, step);
            offset += step;
            ++accepted;
        } else {
            // 被拒绝：下一帧必须仍能从上一个被接受的位置继续
        }
    }
    QVERIFY(accepted > 100);
    const QImage out = st.finish();
    SS_VERIFY_IMAGE(out.copy(0, 16, 260, out.height() - 28), doc.copy(0, 0, 260, out.height() - 28), CompareOptions{}, "stitch_random");
}

void TestStitch::perFrameBudget()
{
    const QImage doc = makeDocument(1920, 4000, 31);
    Stitcher st;
    st.begin(frameAt(doc, 0, 1080, 0, 0));
    st.addFrame(frameAt(doc, 300, 1080, 0, 0));
    const qint64 t0 = monotonicNs();
    const auto r = st.addFrame(frameAt(doc, 700, 1080, 0, 0));
    const double ms = nsToMs(monotonicNs() - t0);
    QVERIFY(r.appended);
    qInfo("1920×1080 一帧匹配 %.1f ms", ms);
#ifdef NDEBUG
    QVERIFY2(ms <= 30.0, "NFR-06 每帧匹配 ≤ 30 ms");
#endif
}

QTEST_MAIN(TestStitch)
#include "tst_stitch.moc"
