// REQ: ANN-02 ANN-07 ANN-20 ANN-23 ANN-27 ANN-28 ANN-29 ANN-32 ANN-36 NFR-05 NFR-08
// 标注数据模型：各类标注的绘制与命中、撤销 / 重做（AC-04 的 30 步往返）、橡皮擦只擦标注、序列化往返、渲染一致性
#include "core/annotation/AnnotationDocument.h"
#include "core/annotation/Shapes.h"
#include "core/util/Clock.h"
#include "support/TestSupport.h"

#include <QJsonDocument>
#include <QTest>

using namespace ss;
using namespace ss::testing;

namespace {

QImage baseImage(int w = 200, int h = 150)
{
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgb((x * 31 + y * 17) & 0xFF, (x ^ y) & 0xFF, (x * y) & 0xFF); // 非线性图案，模糊后像素必变
    }
    return img;
}

std::unique_ptr<RectAnnotation> rect(const QRectF& r, QColor color = Qt::red, bool filled = true)
{
    auto a = std::make_unique<RectAnnotation>();
    a->setBounds(r);
    AnnotationStyle s;
    s.stroke = color;
    s.fill = color;
    s.filled = filled;
    a->setStyle(s);
    return a;
}

} // namespace

class TestAnnotation : public QObject {
    Q_OBJECT
private slots:
    void rectFillPaintsAndHits();
    void lineArrowHitTest();
    void pencilAndHighlighter();
    void mosaicAndBlurAffectOnlyShape();
    void textBoundsAndScale();
    void eraserRemovesAnnotationsNotBase();
    void undoRedoThirtyOperations();
    void clearAllIsUndoable();
    void modifyCommand();
    void jsonRoundTrip();
    void renderIsDeterministicAndCached();
    void fiveHundredItemsRenderBudget();
};

void TestAnnotation::rectFillPaintsAndHits()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    const int id = doc.add(rect(QRectF(10, 10, 50, 30)));
    QCOMPARE(doc.count(), 1);
    const QImage out = doc.render();
    QCOMPARE(out.pixel(20, 20), qRgb(255, 0, 0));
    QCOMPARE(out.pixel(100, 100), baseImage().pixel(100, 100));
    QVERIFY(doc.hitTest(QPointF(20, 20), 2) == doc.find(id));
    QVERIFY(doc.hitTest(QPointF(150, 100), 2) == nullptr);
    // 描边矩形：内部不命中
    const int id2 = doc.add(rect(QRectF(100, 50, 60, 40), Qt::blue, false));
    QVERIFY(doc.hitTest(QPointF(130, 70), 2) == nullptr);
    QVERIFY(doc.hitTest(QPointF(100, 70), 2) == doc.find(id2));
    // 旋转 90° 后，原来包围盒外的点命中
    doc.find(id2)->setRotation(90);
    QVERIFY(doc.hitTest(QPointF(130, 40), 2) != nullptr);
}

void TestAnnotation::lineArrowHitTest()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    auto line = std::make_unique<LineAnnotation>();
    line->points() = {QPointF(10, 10), QPointF(100, 10), QPointF(100, 80)};
    line->setArrow(false, true);
    QVERIFY(!line->rotatable());
    const int id = doc.add(std::move(line));
    QVERIFY(doc.hitTest(QPointF(50, 11), 2) == doc.find(id));
    QVERIFY(doc.hitTest(QPointF(50, 40), 2) == nullptr);
    const QImage out = doc.render();
    QCOMPARE(out.pixel(50, 10), qRgb(230, 60, 60));
    QCOMPARE(out.pixel(98, 70), qRgb(230, 60, 60)); // 箭头头部附近
    QCOMPARE(doc.find(id)->bounds(), QRectF(10, 10, 90, 70));
}

void TestAnnotation::pencilAndHighlighter()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    auto pencil = std::make_unique<PencilAnnotation>();
    pencil->points() = {QPointF(10, 100), QPointF(60, 100), QPointF(120, 100)};
    AnnotationStyle s;
    s.stroke = Qt::black;
    s.width = 6;
    pencil->setStyle(s);
    const int pid = doc.add(std::move(pencil));
    QVERIFY(doc.hitTest(QPointF(60, 101), 1) == doc.find(pid));
    auto hl = std::make_unique<HighlighterAnnotation>();
    hl->setShape(HighlighterAnnotation::Shape::Rect);
    hl->setRect(QRectF(0, 0, 50, 50));
    AnnotationStyle hs;
    hs.stroke = QColor(255, 255, 0);
    hl->setStyle(hs);
    hl->setBlend(HighlighterAnnotation::Blend::Multiply);
    doc.add(std::move(hl));
    const QImage out = doc.render();
    // 正片叠底：黄色 × 底色 → 蓝通道变 0，红绿保持
    const QRgb base = baseImage().pixel(20, 20);
    const QRgb px = out.pixel(20, 20);
    QCOMPARE(qBlue(px), 0);
    QCOMPARE(qRed(px), qRed(base));
    QCOMPARE(out.pixel(60, 100), qRgb(0, 0, 0));
}

void TestAnnotation::mosaicAndBlurAffectOnlyShape()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    auto m = std::make_unique<EffectAnnotation>();
    m->setEffect(EffectAnnotation::Effect::Mosaic);
    m->setShape(EffectAnnotation::Shape::Rect);
    m->setRect(QRectF(20, 20, 60, 60));
    m->setStrength(40);
    doc.add(std::move(m));
    auto b = std::make_unique<EffectAnnotation>();
    b->setEffect(EffectAnnotation::Effect::Blur);
    b->setShape(EffectAnnotation::Shape::Ellipse);
    b->setRect(QRectF(100, 20, 60, 60));
    b->setStrength(20);
    doc.add(std::move(b));
    const QImage out = doc.render();
    const QImage base = baseImage();
    // 马赛克块内颜色一致，与原图不同；形状外保持原样
    QCOMPARE(out.pixel(21, 21), out.pixel(30, 30));
    QVERIFY(out.pixel(21, 21) != base.pixel(21, 21));
    QCOMPARE(out.pixel(10, 10), base.pixel(10, 10));
    QCOMPARE(out.pixel(90, 90), base.pixel(90, 90));
    // 模糊：椭圆中心像素变了，椭圆外角落没变
    QVERIFY(out.pixel(130, 50) != base.pixel(130, 50));
    QCOMPARE(out.pixel(101, 21), base.pixel(101, 21));
    QVERIFY(doc.hitTest(QPointF(50, 50), 0) != nullptr);
    QVERIFY(doc.hitTest(QPointF(101, 21), 0) == nullptr);
}

void TestAnnotation::textBoundsAndScale()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage(400, 300));
    auto t = std::make_unique<TextAnnotation>();
    t->setText(QStringLiteral("Hello 你好"));
    t->setPosition(QPointF(10, 10));
    AnnotationStyle s;
    s.stroke = Qt::blue;
    t->setStyle(s);
    const double h0 = t->bounds().height();
    QVERIFY(h0 > 10);
    const int id = doc.add(std::move(t));
    const QImage out = doc.render();
    bool anyBlue = false;
    for (int y = 10; y < 10 + static_cast<int>(h0) && !anyBlue; ++y)
        for (int x = 10; x < 200; ++x)
            if (qBlue(out.pixel(x, y)) > 200 && qRed(out.pixel(x, y)) < 100) {
                anyBlue = true;
                break;
            }
    QVERIFY(anyBlue);
    // 拖角放大一倍：字号随之变大
    auto* text = static_cast<TextAnnotation*>(doc.find(id));
    const double size0 = text->font().pointSizeF();
    text->setBounds(QRectF(10, 10, text->bounds().width() * 2, h0 * 2));
    QVERIFY(text->font().pointSizeF() > size0 * 1.8);
    text->setRotation(45);
    QVERIFY(text->rotatedBounds().height() > text->bounds().height());
}

void TestAnnotation::eraserRemovesAnnotationsNotBase()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    doc.add(rect(QRectF(0, 0, 100, 100)));
    auto e = std::make_unique<EraserAnnotation>();
    e->setShape(EraserAnnotation::Shape::Rect);
    e->setRect(QRectF(20, 20, 30, 30));
    const int eid = doc.add(std::move(e));
    QImage out = doc.render();
    QCOMPARE(out.pixel(10, 10), qRgb(255, 0, 0));             // 未擦的部分
    QCOMPARE(out.pixel(30, 30), baseImage().pixel(30, 30));   // 擦掉标注，露出底图
    QVERIFY(doc.hitTest(QPointF(30, 30), 0) == doc.find(eid)); // 矩形擦除区可选中
    // 橡皮擦之后再画的标注不受影响
    doc.add(rect(QRectF(25, 25, 10, 10), Qt::green));
    out = doc.render();
    QCOMPARE(out.pixel(30, 30), qRgb(0, 255, 0));
}

void TestAnnotation::undoRedoThirtyOperations()
{
    // AC-04：连续 30 个操作后撤销到底再重做到底，每步状态正确
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    QList<QImage> snapshots;
    snapshots.append(doc.render());
    for (int i = 0; i < 30; ++i) {
        if (i % 5 == 4 && doc.count() > 0)
            doc.remove(doc.at(0)->id());
        else if (i % 7 == 6) {
            auto before = doc.at(doc.count() - 1)->clone();
            doc.at(doc.count() - 1)->translate(QPointF(3, 3));
            doc.commitModification(doc.at(doc.count() - 1)->id(), std::move(before));
        } else
            doc.add(rect(QRectF(5 + i * 4, 5 + i * 3, 30, 20), QColor(i * 8, 50, 200)));
        snapshots.append(doc.render());
    }
    QCOMPARE(doc.undoStack().count(), 30);
    for (int i = 30; i > 0; --i) {
        QVERIFY(doc.canUndo());
        doc.undo();
        SS_VERIFY_IMAGE(doc.render(), snapshots[i - 1], CompareOptions{}, "undo_step");
    }
    QVERIFY(!doc.canUndo());
    QCOMPARE(doc.count(), 0);
    for (int i = 1; i <= 30; ++i) {
        QVERIFY(doc.canRedo());
        doc.redo();
        SS_VERIFY_IMAGE(doc.render(), snapshots[i], CompareOptions{}, "redo_step");
    }
    QVERIFY(!doc.canRedo());
}

void TestAnnotation::clearAllIsUndoable()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    doc.add(rect(QRectF(0, 0, 10, 10)));
    doc.add(rect(QRectF(20, 20, 10, 10)));
    doc.clearAll();
    QCOMPARE(doc.count(), 0);
    doc.undo();
    QCOMPARE(doc.count(), 2);
    QCOMPARE(doc.at(1)->bounds(), QRectF(20, 20, 10, 10));
    doc.redo();
    QCOMPARE(doc.count(), 0);
}

void TestAnnotation::modifyCommand()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    const int id = doc.add(rect(QRectF(0, 0, 10, 10)));
    auto before = doc.find(id)->clone();
    doc.find(id)->setBounds(QRectF(50, 50, 20, 20));
    doc.find(id)->setRotation(30);
    doc.commitModification(id, std::move(before));
    QCOMPARE(doc.find(id)->bounds(), QRectF(50, 50, 20, 20));
    doc.undo();
    QCOMPARE(doc.find(id)->bounds(), QRectF(0, 0, 10, 10));
    QCOMPARE(doc.find(id)->rotation(), 0.0);
    doc.redo();
    QCOMPARE(doc.find(id)->rotation(), 30.0);
}

void TestAnnotation::jsonRoundTrip()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    doc.add(rect(QRectF(1, 2, 3, 4)));
    auto line = std::make_unique<LineAnnotation>();
    line->points() = {QPointF(0, 0), QPointF(50, 50)};
    line->setArrow(true, true);
    doc.add(std::move(line));
    auto t = std::make_unique<TextAnnotation>();
    t->setText(QStringLiteral("x"));
    t->setPosition(QPointF(7, 8));
    t->setRotation(15);
    doc.add(std::move(t));
    auto e = std::make_unique<EffectAnnotation>();
    e->setShape(EffectAnnotation::Shape::Brush);
    e->points() = {QPointF(1, 1), QPointF(9, 9)};
    doc.add(std::move(e));
    const QJsonObject json = doc.toJson();
    AnnotationDocument copy;
    copy.setBaseImage(baseImage());
    copy.fromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
    QCOMPARE(copy.count(), 4);
    QCOMPARE(copy.at(0)->type(), QStringLiteral("rect"));
    QCOMPARE(copy.at(1)->type(), QStringLiteral("line"));
    QVERIFY(static_cast<LineAnnotation*>(copy.at(1))->arrowStart());
    QCOMPARE(copy.at(2)->rotation(), 15.0);
    SS_VERIFY_IMAGE(copy.render(), doc.render(), CompareOptions{}, "json_roundtrip");
    QCOMPARE(copy.add(rect(QRectF())), 5); // id 接着编
}

void TestAnnotation::renderIsDeterministicAndCached()
{
    AnnotationDocument doc;
    doc.setBaseImage(baseImage());
    const int id = doc.add(rect(QRectF(10, 10, 20, 20)));
    const QImage a = doc.render();
    const QImage b = doc.cachedRender();
    SS_VERIFY_IMAGE(a, b, CompareOptions{}, "render_cached");
    const int rev = doc.revision();
    QCOMPARE(doc.cachedRender().cacheKey(), doc.cachedRender().cacheKey()); // 命中缓存
    doc.find(id)->translate(QPointF(1, 1));
    QCOMPARE(doc.revision(), rev); // 直接改对象不换版本；提交修改才换
    doc.commitModification(id, rect(QRectF(10, 10, 20, 20)));
    QVERIFY(doc.revision() > rev);
    const QImage excluded = doc.cachedRender(id);
    QCOMPARE(excluded.pixel(15, 15), baseImage().pixel(15, 15));
}

void TestAnnotation::fiveHundredItemsRenderBudget()
{
    // NFR-05：4K 底图 500 个标注，完整渲染一次的耗时（Release 下应远小于一帧的多倍；这里只记录并设宽松上限）
    AnnotationDocument doc;
    doc.setBaseImage(baseImage(3840, 2160));
    for (int i = 0; i < 500; ++i) {
        if (i % 3 == 0) {
            auto p = std::make_unique<PencilAnnotation>();
            p->points() = {QPointF(i * 7 % 3800, 100), QPointF(i * 7 % 3800 + 40, 160), QPointF(i * 7 % 3800 + 80, 120)};
            doc.add(std::move(p));
        } else {
            doc.add(rect(QRectF(i * 7 % 3800, (i * 13) % 2000, 60, 40), QColor(i % 255, 80, 80), i % 2 == 0));
        }
    }
    doc.render();
    const qint64 t0 = monotonicNs();
    doc.render();
    const double ms = nsToMs(monotonicNs() - t0);
    qInfo("4K 底图 500 个标注完整渲染 %.1f ms", ms);
#ifdef NDEBUG
    QVERIFY2(ms < 400, "500 items render too slow");
#endif
}

QTEST_MAIN(TestAnnotation) // 文字标注需要字体数据库
#include "tst_annotation.moc"
