// REQ: VID-02 VID-03 VID-06 VID-08 VID-09
// M7 录屏核心：帧仓库（画质、索引、崩溃恢复）、GIF / WebP 动画编码、输入可视化、回放逻辑（裁剪、重采样、进度、混音、画中画）
#include "core/video/GifEncoder.h"
#include "core/video/InputVisualizer.h"
#include "core/video/Playback.h"
#include "core/video/Recording.h"
#include "core/video/WebpAnimEncoder.h"
#include "support/TestSupport.h"

#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace ss;

namespace {

QImage frameImage(int n, QSize size = QSize(64, 48))
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor(n * 20 % 256, 80, 200));
    QPainter p(&img);
    p.fillRect(QRect(n * 4 % size.width(), 8, 8, 8), Qt::white);
    return img;
}

} // namespace

class TestVideo : public QObject {
    Q_OBJECT
private slots:
    void frameStoreRoundTrip();
    void frameStoreQuality();
    void frameStoreRecovery();
    void gifEncode();
    void webpEncode();
    void inputVisualizer();
    void playbackLogic();
    void progressAndPip();
    void mixAudioClamp();
};

void TestVideo::frameStoreRoundTrip()
{
    QTemporaryDir tmp;
    FrameStore store;
    QVERIFY(store.create(tmp.filePath(QStringLiteral("rec")), QRect(10, 20, 64, 48), 30, FrameStore::Quality::Standard));
    for (int i = 0; i < 5; ++i)
        QVERIFY(store.appendFrame(frameImage(i), i * 33));
    InputEvent click;
    click.kind = InputEvent::Kind::Click;
    click.timeMs = 40;
    click.pos = QPoint(5, 6);
    click.button = 3;
    store.addEvent(click);
    InputEvent key;
    key.kind = InputEvent::Kind::Key;
    key.timeMs = 70;
    key.keyText = QStringLiteral("Ctrl+C");
    store.addEvent(key);
    QVector<qint16> pcm(4800, 1000);
    QVERIFY(store.appendAudio(pcm, 48000));
    QVERIFY(store.writeIndex(true));

    FrameStore again;
    QVERIFY(again.open(store.dir()));
    QVERIFY(again.finished());
    QCOMPARE(again.frameCount(), 5);
    QCOMPARE(again.fps(), 30);
    QCOMPARE(again.region(), QRect(10, 20, 64, 48));
    QCOMPARE(again.frameSize(), QSize(64, 48));
    QCOMPARE(again.timestampMs(4), 132);
    QCOMPARE(again.durationMs(), 132 + 33);
    QCOMPARE(again.events().size(), 2);
    QCOMPARE(again.events()[0].button, 3);
    QCOMPARE(again.events()[1].keyText, QStringLiteral("Ctrl+C"));
    QCOMPARE(again.audioSampleRate(), 48000);
    QCOMPARE(again.audio().size(), 4800);
    QCOMPARE(again.audio()[10], 1000);
    QVERIFY(!again.frame(2).isNull());
    QCOMPARE(again.frame(2).size(), QSize(64, 48));
    again.remove();
    QVERIFY(!QDir(store.dir()).exists());
}

void TestVideo::frameStoreQuality()
{
    QTemporaryDir tmp;
    FrameStore light;
    QVERIFY(light.create(tmp.filePath(QStringLiteral("light")), QRect(0, 0, 64, 48), 16, FrameStore::Quality::Light));
    QVERIFY(light.appendFrame(frameImage(0), 0));
    QCOMPARE(light.frameSize(), QSize(32, 24)); // VID-02：轻量缩小一半
    QVERIFY(QFile::exists(QDir(light.dir()).filePath(QStringLiteral("f000000.jpg"))));
    FrameStore raw;
    QVERIFY(raw.create(tmp.filePath(QStringLiteral("raw")), QRect(0, 0, 64, 48), 60, FrameStore::Quality::Raw));
    QVERIFY(raw.appendFrame(frameImage(0), 0));
    QVERIFY(QFile::exists(QDir(raw.dir()).filePath(QStringLiteral("f000000.png"))));
    QCOMPARE(raw.frame(0).pixel(1, 1), frameImage(0).pixel(1, 1)); // 原始无损
    QCOMPARE(FrameStore::qualityFromName(FrameStore::qualityName(FrameStore::Quality::Light)), FrameStore::Quality::Light);
}

void TestVideo::frameStoreRecovery()
{
    // VID-09：索引只写到第 2 帧就“崩溃”，磁盘上有 4 帧 → 打开时补齐，且标记为未完成
    QTemporaryDir tmp;
    FrameStore store;
    QVERIFY(store.create(tmp.filePath(QStringLiteral("rec")), QRect(0, 0, 64, 48), 30, FrameStore::Quality::Standard));
    QVERIFY(store.appendFrame(frameImage(0), 0));
    QVERIFY(store.appendFrame(frameImage(1), 33));
    QVERIFY(store.writeIndex(false));
    QVERIFY(store.appendFrame(frameImage(2), 66));
    QVERIFY(store.appendFrame(frameImage(3), 99));
    QVERIFY(FrameStore::hasUnfinished(store.dir()));
    FrameStore recovered;
    QVERIFY(recovered.open(store.dir()));
    QVERIFY(!recovered.finished());
    QCOMPARE(recovered.frameCount(), 4);
    QCOMPARE(recovered.timestampMs(3), 33 + 33 + 33);
    QVERIFY(recovered.writeIndex(true));
    QVERIFY(!FrameStore::hasUnfinished(store.dir()));
    QVERIFY(!FrameStore::hasUnfinished(tmp.filePath(QStringLiteral("missing"))));
}

void TestVideo::gifEncode()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("a.gif"));
    GifEncoder gif;
    QVERIFY(gif.open(path, QSize(64, 48)));
    for (int i = 0; i < 4; ++i)
        QVERIFY(gif.addFrame(frameImage(i), 100));
    QVERIFY(gif.finish());
    QVERIFY(QFile(path).size() > 100);
    QImageReader reader(path);
    QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
    if (!reader.supportsAnimation())
        QSKIP("Qt GIF plugin missing");
    QCOMPARE(reader.imageCount(), 4);
    const QImage first = reader.read();
    QCOMPARE(first.size(), QSize(64, 48));
    // 颜色经 256 色量化后接近原图
    const QRgb want = frameImage(0).pixel(30, 30);
    const QRgb got = first.pixel(30, 30);
    QVERIFY(qAbs(qRed(want) - qRed(got)) <= 8 && qAbs(qGreen(want) - qGreen(got)) <= 8 && qAbs(qBlue(want) - qBlue(got)) <= 8);
    QCOMPARE(reader.nextImageDelay(), 100);
    // 量化：多于 256 色的渐变图压到 256 色
    QImage gradient(256, 16, QImage::Format_RGB32);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 256; ++x)
            gradient.setPixel(x, y, qRgb(x, 255 - x, y * 16));
    const QImage indexed = GifEncoder::quantize(gradient);
    QCOMPARE(indexed.format(), QImage::Format_Indexed8);
    QVERIFY(indexed.colorCount() <= 256);
}

void TestVideo::webpEncode()
{
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("a.webp"));
    WebpAnimEncoder webp;
    QVERIFY(webp.open(path, QSize(64, 48), 80));
    for (int i = 0; i < 3; ++i)
        QVERIFY(webp.addFrame(frameImage(i), i * 100));
    QVERIFY(webp.finish());
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray head = f.read(16);
    QCOMPARE(head.left(4), QByteArray("RIFF"));
    QCOMPARE(head.mid(8, 4), QByteArray("WEBP"));
    QCOMPARE(head.mid(12, 4), QByteArray("VP8X")); // 动画扩展块
    QImageReader reader(path);
    if (reader.canRead() && reader.supportsAnimation())
        QCOMPARE(reader.imageCount(), 3);
}

void TestVideo::inputVisualizer()
{
    QList<InputEvent> events;
    InputEvent click;
    click.kind = InputEvent::Kind::Click;
    click.timeMs = 100;
    click.pos = QPoint(32, 24);
    events.append(click);
    InputEvent key;
    key.kind = InputEvent::Kind::Key;
    key.timeMs = 100;
    key.keyText = QStringLiteral("Ctrl+S");
    events.append(key);
    QImage base(128, 96, QImage::Format_RGB32);
    base.fill(Qt::black);
    InputVisualOptions o;
    o.clickDurationMs = 400;
    // 事件之前：不画
    QCOMPARE(renderWithInputEvents(base, events, 50, o).pixel(32, 24), qRgb(0, 0, 0));
    // 事件刚发生：涟漪在点击处
    const QImage at = renderWithInputEvents(base, events, 120, o);
    QVERIFY(at.pixel(32, 24) != qRgb(0, 0, 0));
    // 按键标签在底部中间
    bool labelDrawn = false;
    for (int x = 40; x < 90 && !labelDrawn; ++x)
        for (int y = 60; y < 90; ++y)
            if (at.pixel(x, y) != qRgb(0, 0, 0)) {
                labelDrawn = true;
                break;
            }
    QVERIFY(labelDrawn);
    // 动画时长过后消失（VID-06 速度可调）
    QCOMPARE(renderWithInputEvents(base, events, 100 + 401, o).pixel(32, 24), qRgb(0, 0, 0));
    o.clickDurationMs = 1000;
    QVERIFY(renderWithInputEvents(base, events, 100 + 401, o).pixel(32, 24) != qRgb(0, 0, 0));
    o.showKeys = false;
    const QImage noKeys = renderWithInputEvents(base, events, 120, o);
    bool any = false;
    for (int x = 40; x < 90; ++x)
        for (int y = 60; y < 90; ++y)
            any = any || noKeys.pixel(x, y) != qRgb(0, 0, 0);
    QVERIFY(!any);
}

void TestVideo::playbackLogic()
{
    const QVector<qint64> ts{0, 33, 66, 99, 132};
    QCOMPARE(frameIndexAt(ts, -5), 0);
    QCOMPARE(frameIndexAt(ts, 0), 0);
    QCOMPARE(frameIndexAt(ts, 40), 1);
    QCOMPARE(frameIndexAt(ts, 99), 3);
    QCOMPARE(frameIndexAt(ts, 500), 4);
    QCOMPARE(frameIndexAt({}, 5), -1);
    TrimRange trim;
    QCOMPARE(trim.clamp(165), 165);
    trim.startMs = 50;
    trim.endMs = 120;
    QVERIFY(trim.contains(50, 165));
    QVERIFY(!trim.contains(120, 165));
    const QVector<ExportFrame> plan = planExport(ts, 165, trim, 30);
    QCOMPARE(plan.size(), 3); // 50、83、116
    QCOMPARE(plan[0].index, 1);
    QCOMPARE(plan[0].timeMs, 0);
    QCOMPARE(plan[2].index, 3);
    QCOMPARE(plan[2].timeMs, 66);
    // 降帧率导出
    QCOMPARE(planExport(ts, 165, TrimRange{}, 10).size(), 2);
    QCOMPARE(progressStyleFromName(QStringLiteral("countdown")), ProgressStyle::Countdown);
    QCOMPARE(progressStyleFromName(QStringLiteral("x")), ProgressStyle::None);
}

void TestVideo::progressAndPip()
{
    QImage img(200, 100, QImage::Format_RGB32);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        paintProgress(p, img.size(), 50, 100, ProgressStyle::Bar);
    }
    QVERIFY(img.pixel(50, 97) != qRgb(0, 0, 0));     // 已播部分
    QVERIFY(img.pixel(150, 97) != img.pixel(50, 97)); // 未播部分颜色不同
    QImage text(200, 100, QImage::Format_RGB32);
    text.fill(Qt::white);
    {
        QPainter p(&text);
        paintProgress(p, text.size(), 1000, 5000, ProgressStyle::Countdown);
    }
    QVERIFY(text.pixel(150, 80) != qRgb(255, 255, 255)); // 右下角的文字底框
    QCOMPARE(text.pixel(20, 20), qRgb(255, 255, 255));
    QImage none(200, 100, QImage::Format_RGB32);
    none.fill(Qt::black);
    {
        QPainter p(&none);
        paintProgress(p, none.size(), 10, 100, ProgressStyle::None);
    }
    QCOMPARE(none.pixel(50, 97), qRgb(0, 0, 0));
    // 画中画：右下角出现摄像头颜色
    QImage cam(40, 30, QImage::Format_RGB32);
    cam.fill(Qt::green);
    QImage pip(200, 100, QImage::Format_RGB32);
    pip.fill(Qt::black);
    {
        QPainter p(&pip);
        paintPictureInPicture(p, pip.size(), cam, PipCorner::BottomRight, 25, 10);
    }
    QCOMPARE(pip.pixel(180, 80), qRgb(0, 255, 0));
    QCOMPARE(pip.pixel(20, 20), qRgb(0, 0, 0));
    QImage pip2 = pip;
    pip2.fill(Qt::black);
    {
        QPainter p(&pip2);
        paintPictureInPicture(p, pip2.size(), cam, PipCorner::TopLeft, 25, 10);
    }
    QCOMPARE(pip2.pixel(20, 20), qRgb(0, 255, 0));
}

void TestVideo::mixAudioClamp()
{
    const QVector<qint16> a{10000, -10000, 30000};
    const QVector<qint16> b{10000, 10000, 30000, 5};
    const QVector<qint16> out = mixAudio(a, 100, b, 50);
    QCOMPARE(out.size(), 4);
    QCOMPARE(out[0], 15000);
    QCOMPARE(out[1], -5000);
    QCOMPARE(out[2], 32767); // 饱和
    QCOMPARE(out[3], 2);
    QCOMPARE(mixAudio(a, 0, b, 200)[0], 20000);
}

QTEST_MAIN(TestVideo)
#include "tst_video.moc"
