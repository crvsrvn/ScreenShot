// REQ: CAP-61 CAP-62 AC-11
// 端到端滚动场景的离线复现：用测试驱动的可滚动窗口所画的同一份文档，按真实场景的步长（3 格 × 60 px）喂给拼接器，
// 长图必须与文档逐像素一致。真实桌面上失败而这里通过，说明问题在输入传递 / 画面稳定性，而不是拼接算法。
#include "ScrollTestWindow.h"
#include "core/stitch/Stitcher.h"
#include "support/TestSupport.h"
#include "testing/ImageCompare.h"

#include <QTest>

using namespace ss;
using namespace ss::stitch;

class TestStitchWindow : public QObject {
    Q_OBJECT
private slots:
    void stitchesDriverDocument();
    void stitchesDriverDocumentOneNotch();
};

static void run(int stepPx)
{
    driver::ScrollTestWindow window(480, 360, 360 * 12);
    const QImage doc = window.renderDocument();
    QCOMPARE(doc.height(), 4320);
    Stitcher s;
    s.begin(doc.copy(0, 0, 480, 360));
    int rejected = 0;
    QString lastReason;
    for (int offset = stepPx; offset <= doc.height() - 360; offset += stepPx) {
        const auto r = s.addFrame(doc.copy(0, offset, 480, 360));
        if (!r.shift.ok) {
            ++rejected;
            lastReason = r.shift.reason;
        }
    }
    // 最后一屏（到底）：再喂两次相同帧
    s.addFrame(doc.copy(0, doc.height() - 360, 480, 360));
    const auto end = s.addFrame(doc.copy(0, doc.height() - 360, 480, 360));
    QVERIFY2(rejected == 0, qPrintable(QStringLiteral("rejected %1, last reason: %2").arg(rejected).arg(lastReason)));
    const QImage out = s.finish();
    const auto cmp = testing::compareImages(out, doc);
    QVERIFY2(cmp.passed, qPrintable(cmp.summary()));
    Q_UNUSED(end);
}

void TestStitchWindow::stitchesDriverDocument()
{
    run(180); // scroll.speed = 3 格，每格 60 px
}

void TestStitchWindow::stitchesDriverDocumentOneNotch()
{
    run(60);
}

QTEST_MAIN(TestStitchWindow)
#include "tst_stitch_window.moc"
