// REQ: VID-01 VID-02 VID-03 VID-04 VID-05 VID-06 VID-07 VID-08 VID-09
// M7 录屏会话与回放：开始 / 暂停 / 继续 / 停止 / 重录 / 延迟 / 计时，普通与快速模式，声音混合，录制中标注，输入事件，摄像头画中画，
// 回放裁剪 / 倍速 / 导出 / 保存 / 复制，崩溃恢复，命令行
#include "app/App.h"
#include "app/CaptureSession.h"
#include "app/OutputActions.h"
#include "app/RecordPlaybackDialog.h"
#include "app/RecordSession.h"
#include "core/annotation/Shapes.h"
#include "core/config/Settings.h"
#include "core/video/Recording.h"
#include "support/TestSupport.h"
#include "testing/FakePlatform.h"
#include "testing/FakeScreenCapture.h"
#include "testing/FakeVideo.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#if defined(Q_OS_WIN)
#include "platform/windows/MfVideoWriter.h"
#endif

using namespace ss;
using namespace ss::testing;

namespace {

struct Fixture {
    QTemporaryDir tmp;
    FakePlatformHandles h;
    Settings s;
    std::unique_ptr<App> app;
    qint64 now = 1000;
    Fixture()
    {
        app = std::make_unique<App>(makeFakePlatform(&h), s);
        app->setDataDir(tmp.path());
        s.setValue(QStringLiteral("output.quickSaveDir"), tmp.filePath(QStringLiteral("out")));
        s.setValue(QStringLiteral("record.fps"), 30);
    }
    RecordSession* begin(const QRect& region = QRect(100, 100, 64, 48))
    {
        if (!app->startRecord(region, false))
            return nullptr;
        RecordSession* r = app->recordSession();
        r->setClock([this] { return now; });
        return r;
    }
    void advance(RecordSession* r, int frames, int stepMs = 33)
    {
        for (int i = 0; i < frames; ++i) {
            now += stepMs;
            r->tick();
        }
    }
    // JPEG 帧允许 ±3 的误差
    static bool near(QRgb a, QRgb b, int tol = 3)
    {
        return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol && qAbs(qBlue(a) - qBlue(b)) <= tol;
    }
    static QString fakeStats(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
};

} // namespace

class TestRecord : public QObject {
    Q_OBJECT
private slots:
    void normalModeLifecycle();
    void delayAndRestart();
    void quickModeWritesMp4();
    void audioMixAndCamera();
    void inputEventsAndKeyNames();
    void annotateWhileRecording();
    void playbackTrimSpeedExport();
    void playbackSaveCopyDiscard();
    void crashRecovery();
    void captureModeAndCommand();
    void realMp4Encoder();
};

void TestRecord::normalModeLifecycle()
{
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    QCOMPARE(r->state(), RecordSession::State::Idle);
    QCOMPARE(r->region(), QRect(100, 100, 64, 48));
    QSignalSpy finished(r, &RecordSession::finished);
    r->start();
    QCOMPARE(r->state(), RecordSession::State::Recording);
    QCOMPARE(r->frameCount(), 1); // 开始立即抓一帧
    f.advance(r, 5);
    QCOMPARE(r->frameCount(), 6);
    QCOMPARE(r->elapsedMs(), 5 * 33);
    r->pause();
    QCOMPARE(r->state(), RecordSession::State::Paused);
    f.advance(r, 3); // 暂停期间不抓帧、不计时
    QCOMPARE(r->frameCount(), 6);
    QCOMPARE(r->elapsedMs(), 5 * 33);
    r->resume();
    QCOMPARE(r->state(), RecordSession::State::Recording);
    f.advance(r, 2);
    QCOMPARE(r->frameCount(), 8);
    QCOMPARE(r->elapsedMs(), 7 * 33);
    auto store = r->store();
    QVERIFY(store);
    r->stop();
    QCOMPARE(r->state(), RecordSession::State::Stopped);
    QCOMPARE(finished.count(), 1);
    QVERIFY(store->finished());
    QCOMPARE(store->frameCount(), 8);
    QCOMPARE(store->timestampMs(7), 7 * 33);
    QCOMPARE(store->frame(0).size(), QSize(64, 48));
    QVERIFY(Fixture::near(store->frame(0).pixel(3, 4), FakeScreenCapture::patternPixel(0, 103, 104))); // 区域从显示器画面裁出
    QVERIFY(f.app->playbackDialog()); // 普通模式停止后打开回放窗口（VID-08）
    QCOMPARE(f.app->playbackDialog()->store()->frameCount(), 8);
    // 快速 / 普通模式之外的无效区域
    QVERIFY(!f.app->startRecord(QRect(0, 0, 1, 1), false));
    QVERIFY(!f.app->recordError().isEmpty());
}

void TestRecord::delayAndRestart()
{
    Fixture f;
    f.s.setValue(QStringLiteral("record.delaySec"), 2);
    RecordSession* r = f.begin();
    QVERIFY(r);
    QCOMPARE(r->delaySec(), 2);
    r->start();
    QCOMPARE(r->state(), RecordSession::State::Countdown);
    QCOMPARE(r->countdownRemainingMs(), 2000);
    f.advance(r, 1, 1500);
    QCOMPARE(r->state(), RecordSession::State::Countdown);
    QCOMPARE(r->countdownRemainingMs(), 500);
    f.advance(r, 1, 600);
    QCOMPARE(r->state(), RecordSession::State::Recording);
    f.advance(r, 4);
    const QString dir = r->store()->dir();
    QVERIFY(QDir(dir).exists());
    // 重录：丢弃已录内容回到准备状态（VID-01）
    r->setDelaySec(0);
    r->restart();
    QCOMPARE(r->state(), RecordSession::State::Idle);
    QVERIFY(!QDir(dir).exists());
    QCOMPARE(r->elapsedMs(), 0);
    r->start();
    QCOMPARE(r->state(), RecordSession::State::Recording);
    f.advance(r, 2);
    QSignalSpy cancelled(r, &RecordSession::cancelled);
    const QString dir2 = r->store()->dir();
    r->cancel();
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!QDir(dir2).exists());
    QVERIFY(!f.app->playbackDialog());
}

void TestRecord::quickModeWritesMp4()
{
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->setMode(RecordSession::Mode::Quick);
    r->setFps(16);
    r->start();
    f.advance(r, 9, 62);
    const QString tempPath = r->quickFilePath();
    QVERIFY(tempPath.endsWith(QStringLiteral(".mp4")));
    r->stop();
    QCoreApplication::processEvents();
    // 文件移到快速保存目录，并复制为文件（VID-03）
    const QString saved = f.app->recordLastFile();
    QVERIFY2(saved.startsWith(f.tmp.filePath(QStringLiteral("out"))), qPrintable(saved));
    QVERIFY(!QFile::exists(tempPath));
    const QString stats = Fixture::fakeStats(saved);
    QVERIFY2(stats.contains(QStringLiteral("frames=10")), qPrintable(stats));
    QVERIFY(stats.contains(QStringLiteral("fps=16")));
    QVERIFY(stats.contains(QStringLiteral("audio=0")));
    QVERIFY(QGuiApplication::clipboard()->mimeData()->hasUrls());
    QCOMPARE(QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile(), saved);
    QVERIFY(!f.app->playbackDialog());
}

void TestRecord::audioMixAndCamera()
{
    Fixture f;
    f.s.setValue(QStringLiteral("record.micVolume"), 100);
    f.s.setValue(QStringLiteral("record.systemVolume"), 50);
    f.s.setValue(QStringLiteral("record.cameraCorner"), QStringLiteral("topLeft"));
    f.s.setValue(QStringLiteral("record.cameraSize"), 50);
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->setMicEnabled(true);
    r->setSystemAudioEnabled(true);
    r->setCameraEnabled(true);
    r->start();
    QVERIFY(static_cast<FakeCamera*>(f.app->platform().camera.get())->running());
    f.advance(r, 10);
    QVERIFY(r->audioSampleCount() >= 10 * FakeAudioCapture::samplesPerTake);
    auto store = r->store();
    r->stop();
    QVERIFY(!static_cast<FakeCamera*>(f.app->platform().camera.get())->running());
    QCOMPARE(store->audioSampleRate(), 48000);
    const QVector<qint16> pcm = store->audio();
    QVERIFY(pcm.size() >= 10 * FakeAudioCapture::samplesPerTake);
    // 两路同相位正弦：混合 = 1.0 + 0.5 倍 → 峰值约 12000
    qint16 peak = 0;
    for (qint16 v : pcm)
        peak = std::max(peak, v);
    QVERIFY2(peak > 11000 && peak < 12500, qPrintable(QString::number(peak)));
    // 摄像头画中画在左上角（VID-07）
    const QImage frame = store->frame(5);
    QCOMPARE(frame.pixel(20, 20), qRgb(0, 200, 0));
    QVERIFY(frame.pixel(60, 44) != qRgb(0, 200, 0));
}

void TestRecord::inputEventsAndKeyNames()
{
    QCOMPARE(RecordSession::keyName(0x41), QStringLiteral("A"));
    QCOMPARE(RecordSession::keyName(0x74), QStringLiteral("F5"));
    QCOMPARE(RecordSession::keyName(0xA2), QStringLiteral("Ctrl"));
    QCOMPARE(RecordSession::keyName(0x20), QStringLiteral("Space"));
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->setMode(RecordSession::Mode::Quick);
    r->start();
    f.advance(r, 2);
    // 全局输入经 App 转发（VID-06）：区域外的点击忽略
    f.app->onGlobalMouse(1, true, QPoint(10, 10));
    f.app->onGlobalMouse(1, true, QPoint(110, 120));
    f.app->onGlobalMouse(1, false, QPoint(110, 120));
    r->noteWheel(120, QPoint(130, 130));
    f.app->onGlobalKeyDown(0xA2, false, 0); // Ctrl 按下
    f.app->onGlobalKeyDown(0x43, false, 0); // C
    r->noteKey(0xA2, false);
    f.app->onGlobalKeyDown(0x43, false, 0); // 再按 C：无修饰键
    QCOMPARE(r->events().size(), 4);
    QCOMPARE(r->events()[0].kind, InputEvent::Kind::Click);
    QCOMPARE(r->events()[0].pos, QPoint(10, 20));
    QCOMPARE(r->events()[0].timeMs, 66);
    QCOMPARE(r->events()[1].kind, InputEvent::Kind::Wheel);
    QCOMPARE(r->events()[1].wheel, 120);
    QCOMPARE(r->events()[2].keyText, QStringLiteral("Ctrl+C"));
    QCOMPARE(r->events()[3].keyText, QStringLiteral("C"));
    f.advance(r, 1);
    r->stop();
    QCoreApplication::processEvents();
    // 普通模式下事件写进索引
    RecordSession* r2 = f.begin();
    QVERIFY(r2);
    r2->start();
    r2->noteMouseButton(3, true, QPoint(120, 120));
    auto store = r2->store();
    r2->stop();
    QCOMPARE(store->events().size(), 1);
    QCOMPARE(store->events()[0].button, 3);
    FrameStore reopened;
    QVERIFY(reopened.open(store->dir()));
    QCOMPARE(reopened.events().size(), 1);
}

void TestRecord::annotateWhileRecording()
{
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->start();
    f.advance(r, 1);
    const QImage before = r->store()->frame(1);
    r->setAnnotating(true);
    QVERIFY(r->annotating());
    auto pencil = std::make_unique<PencilAnnotation>();
    AnnotationStyle style;
    style.stroke = QColor(255, 0, 0);
    style.width = 6;
    pencil->setStyle(style);
    pencil->points() = {QPointF(5, 24), QPointF(60, 24)};
    r->annotations().add(std::move(pencil));
    f.advance(r, 1);
    const QImage after = r->store()->frame(2);
    QVERIFY(!Fixture::near(before.pixel(30, 24), qRgb(255, 0, 0)));
    QVERIFY(Fixture::near(after.pixel(30, 24), qRgb(255, 0, 0))); // VID-05：标注合成进帧
    r->stop();
}

void TestRecord::playbackTrimSpeedExport()
{
    Fixture f;
    f.s.setValue(QStringLiteral("record.progress"), QStringLiteral("bar"));
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->start();
    f.advance(r, 29);
    r->noteMouseButton(1, true, QPoint(132, 124));
    r->stop();
    RecordPlaybackDialog* d = f.app->playbackDialog();
    QVERIFY(d);
    QCOMPARE(d->store()->frameCount(), 30);
    QCOMPARE(d->format(), QStringLiteral("mp4")); // 默认格式来自设置
    // 跳转 / 倍速 / 播放
    d->seek(500);
    QCOMPARE(d->position(), 500);
    d->setSpeed(2.0);
    QCOMPARE(d->speed(), 2.0);
    d->play();
    QVERIFY(d->playing());
    QTest::qWait(120);
    QVERIFY(d->position() > 500);
    d->pause();
    QVERIFY(!d->playing());
    // 裁剪：保留 300 ~ 700 ms
    d->seek(300);
    d->trimStartHere();
    d->seek(700);
    d->trimEndHere();
    QCOMPARE(d->trim().startMs, 300);
    QCOMPARE(d->trim().endMs, 700);
    // 导出 GIF：帧数按裁剪范围（300 ~ 700 ms，每 33 ms 一帧 = 13 帧）
    const QString gifPath = f.tmp.filePath(QStringLiteral("x.gif"));
    QVERIFY2(d->exportTo(gifPath, QStringLiteral("gif")), qPrintable(d->lastError()));
    QVERIFY(QFile(gifPath).size() > 100);
    const QString webpPath = f.tmp.filePath(QStringLiteral("x.webp"));
    QVERIFY2(d->exportTo(webpPath, QStringLiteral("webp")), qPrintable(d->lastError()));
    QVERIFY(QFile(webpPath).size() > 50);
    const QString mp4Path = f.tmp.filePath(QStringLiteral("x.mp4"));
    QVERIFY2(d->exportTo(mp4Path, QStringLiteral("mp4")), qPrintable(d->lastError()));
    const QString stats = Fixture::fakeStats(mp4Path);
    QVERIFY2(stats.contains(QStringLiteral("frames=13")), qPrintable(stats));
    // 进度条叠加出现在帧底部；点击涟漪出现在点击处（VID-06 / VID-08）
    const QImage frame = d->renderAt(29 * 33);
    QVERIFY(frame.pixel(10, 46) != d->store()->frame(29).pixel(10, 46));
    QVERIFY(frame.pixel(32, 24) != d->store()->frame(29).pixel(32, 24));
    d->setShowInputs(false);
    QCOMPARE(d->renderAt(29 * 33).pixel(32, 24), d->store()->frame(29).pixel(32, 24));
    d->resetTrim();
    QCOMPARE(d->trim().startMs, 0);
    QCOMPARE(d->trim().endMs, -1);
    d->close();
}

void TestRecord::playbackSaveCopyDiscard()
{
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->start();
    f.advance(r, 5);
    r->stop();
    RecordPlaybackDialog* d = f.app->playbackDialog();
    QVERIFY(d);
    d->setFormat(QStringLiteral("gif"));
    const QString quick = d->quickSave();
    QVERIFY2(!quick.isEmpty(), qPrintable(d->lastError()));
    QVERIFY2(quick.startsWith(f.tmp.filePath(QStringLiteral("out"))) && quick.endsWith(QStringLiteral(".gif")), qPrintable(quick));
    QVERIFY(QFile::exists(quick));
    QCOMPARE(f.app->recordLastFile(), quick);
    const QString copied = d->copyToClipboard();
    QVERIFY(QFile::exists(copied));
    QCOMPARE(QGuiApplication::clipboard()->mimeData()->urls().first().toLocalFile(), copied);
    const QString chosen = f.tmp.filePath(QStringLiteral("chosen.gif"));
    d->setDialogOverride(chosen);
    QCOMPARE(d->save(), chosen);
    QVERIFY(QFile::exists(chosen));
    const QString dir = d->store()->dir();
    d->discard();
    QVERIFY(!QDir(dir).exists());
}

void TestRecord::crashRecovery()
{
    Fixture f;
    RecordSession* r = f.begin();
    QVERIFY(r);
    r->start();
    f.advance(r, 70); // > 2 s：索引至少落盘一次
    const QString dir = r->store()->dir();
    QVERIFY(QFile::exists(QDir(dir).filePath(QStringLiteral("recording.json"))));
    // 模拟异常退出：不调用 stop，直接丢掉会话
    f.app.reset();
    QVERIFY(FrameStore::hasUnfinished(dir));
    auto app2 = std::make_unique<App>(makeFakePlatform(), f.s);
    app2->setDataDir(f.tmp.path());
    QCOMPARE(app2->unfinishedRecordings(), QStringList{dir});
    RecordPlaybackDialog* d = app2->openRecording(dir);
    QVERIFY(d);
    QCOMPARE(d->store()->frameCount(), 71);
    QVERIFY(!FrameStore::hasUnfinished(dir));
    QVERIFY(app2->unfinishedRecordings().isEmpty());
    d->close();
    QCoreApplication::processEvents();
}

void TestRecord::captureModeAndCommand()
{
    Fixture f;
    // 截图界面的录制模式确认后进入录制（CAP-07 / VID-01）
    CaptureSession& cs = f.app->session();
    QVERIFY(cs.start(1, false));
    cs.setSelection(QRect(200, 200, 80, 60));
    cs.setMode(CaptureSession::Mode::Record);
    cs.accept();
    QVERIFY(f.app->recordSession());
    QCOMPARE(f.app->recordSession()->region(), QRect(200, 200, 80, 60));
    f.app->recordSession()->cancel();
    QCoreApplication::processEvents();
    QVERIFY(!f.app->recordSession());
    // 命令行
    QCOMPARE(f.app->runCommand({QStringLiteral("record")}), QStringLiteral("error: region required (x,y,w,h or last)"));
    QCOMPARE(f.app->runCommand({QStringLiteral("record"), QStringLiteral("10,10,64,48"), QStringLiteral("--quick"), QStringLiteral("--start")}), QStringLiteral("ok"));
    QVERIFY(f.app->recordSession());
    QCOMPARE(f.app->recordSession()->mode(), RecordSession::Mode::Quick);
    QCOMPARE(f.app->recordSession()->state(), RecordSession::State::Recording);
    f.app->recordSession()->cancel();
}

void TestRecord::realMp4Encoder()
{
#if defined(Q_OS_WIN)
    QTemporaryDir tmp;
    const QString path = tmp.filePath(QStringLiteral("real.mp4"));
    win::MfVideoWriter writer;
    if (!writer.open(path, QSize(64, 48), 30, 48000, 1))
        QSKIP(qPrintable(QStringLiteral("Media Foundation H.264 encoder unavailable: ") + writer.lastError()));
    for (int i = 0; i < 15; ++i) {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(QColor(i * 10, 100, 150));
        QVERIFY2(writer.addFrame(img, i * 33), qPrintable(writer.lastError()));
    }
    QVector<qint16> pcm(48000 / 2, 2000);
    writer.addAudio(pcm, 0);
    QVERIFY2(writer.finish(), qPrintable(writer.lastError()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray head = f.read(12);
    QCOMPARE(head.mid(4, 4), QByteArray("ftyp")); // MP4 容器
    QVERIFY(f.size() > 1000);
#else
    QSKIP("Windows only");
#endif
}

QTEST_MAIN(TestRecord)
#include "tst_record.moc"
