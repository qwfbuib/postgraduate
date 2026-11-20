#include "clientwindow.h"
#include "display.h"
#include "filequeue.h"
#include "mainwindow.h"
#include "terminalserver.h"
#include "testpreview.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QUrlQuery>
#include <QtTest>
#include <atomic>

namespace {

using Mode = LegacyProtocol::DisplayMode;
const QString Loopback = QStringLiteral("127.0.0.1");

const QByteArray LiveFixture = QStringLiteral(R"({
  "status":"1","lives":[{"city":"成都市","adcode":"510100","weather":"多云",
  "temperature":"22","humidity":64,"winddirection":"东北","windpower":"≤3",
  "reporttime":"2026-11-21 16:30:00"}]
})").toUtf8();

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QPushButton *button(QWidget &window, const char *name)
{
    return window.findChild<QPushButton *>(QString::fromLatin1(name));
}

void pointAt(ClientWindow &window, quint16 port)
{
    window.findChild<QSpinBox *>(QStringLiteral("serverPort"))->setValue(port);
    button(window, "connectServer")->click();
}

int countReceipts(const QSignalSpy &logs)
{
    int count = 0;
    for (const auto &entry : logs) {
        if (entry.first().toString().contains(QStringLiteral("文件回执：接收文件完成"))) {
            ++count;
        }
    }
    return count;
}

}

class ClientTests : public QObject
{
    Q_OBJECT

private slots:
    void parsesWeatherAndRetainsLastReading();
    void receivesAndDisplaysFilesAndNotices();
    void reconnectsAndDiscardsPartialFile();
    void worksWithExistingManagerWindow();
    void switchesLayoutAndWritesPreviews();
    void playsVideoOnWindows();
};

void ClientTests::parsesWeatherAndRetainsLastReading()
{
    QString error;
    const auto parsed = WeatherService::parse(LiveFixture, error);
    QVERIFY(error.isEmpty());
    QCOMPARE(parsed.city, QStringLiteral("成都市"));
    QCOMPARE(parsed.temperature, QStringLiteral("22"));
    QCOMPARE(parsed.humidity, QStringLiteral("64"));
    QCOMPARE(parsed.published, QStringLiteral("2026-11-21 16:30:00"));
    QVERIFY(WeatherService::parse("{", error).city.isEmpty());
    QVERIFY(!error.isEmpty());
    QVERIFY(WeatherService::parse(R"({"status":"1","lives":[]})", error).city.isEmpty());
    QVERIFY(!error.isEmpty());
    WeatherService::parse(R"({"status":"0","infocode":"10001"})", error);
    QVERIFY(error.contains(QStringLiteral("10001")));
    QVERIFY(!WeatherService::isCityCode(QStringLiteral("123abc")));

    std::atomic<int> calls{0};
    WeatherService service([&calls](const QUrl &url) -> WeatherResponse {
        const QUrlQuery query(url);
        if (url.scheme() != QStringLiteral("https") || url.host() != QStringLiteral("restapi.amap.com")
            || query.queryItemValue(QStringLiteral("city")) != QStringLiteral("510100")
            || query.queryItemValue(QStringLiteral("extensions")) != QStringLiteral("base")
            || query.queryItemValue(QStringLiteral("key")) != QStringLiteral("test-key")) {
            return {{}, QStringLiteral("请求参数错误")};
        }
        if (++calls == 1) {
            return {LiveFixture, {}};
        }
        return {{}, QStringLiteral("模拟网络超时")};
    });
    service.configure(QStringLiteral("510100"), {});
    QCOMPARE(service.status(), WeatherService::Status::NoKey);
    QCOMPARE(calls.load(), 0);
    service.setDemo(true);
    QCOMPARE(service.status(), WeatherService::Status::Demo);
    QCOMPARE(service.reading().city, QStringLiteral("成都市"));

    service.configure(QStringLiteral("510100"), QStringLiteral("test-key"));
    QTRY_COMPARE(service.status(), WeatherService::Status::Live);
    QCOMPARE(service.reading().published, parsed.published);
    service.refresh();
    QTRY_COMPARE(service.status(), WeatherService::Status::Stale);
    QCOMPARE(service.reading().temperature, parsed.temperature);
    QCOMPARE(service.reading().published, parsed.published);
    QCOMPARE(service.error(), QStringLiteral("模拟网络超时"));
}

void ClientTests::receivesAndDisplaysFilesAndNotices()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QDir root(folder.path());
    QVERIFY(root.mkdir(QStringLiteral("a")));
    QVERIFY(root.mkdir(QStringLiteral("b")));
    const QString receive = root.filePath(QStringLiteral("RecvFile"));

    TerminalServer manager(root.filePath(QStringLiteral("manager")));
    QVERIFY(manager.start(0, QHostAddress::LocalHost));
    QSignalSpy logs(&manager, &TerminalServer::logMessage);
    QSettings settings(root.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);

    ClientWindow client(receive, settings, false);
    client.show();
    pointAt(client, manager.port());
    QTRY_VERIFY(manager.fileSocket(Loopback));
    QCOMPARE(settings.value(QStringLiteral("server/port")).toInt(), int(manager.port()));

    auto *board = client.findChild<DisplayBoard *>();
    auto *notice = client.findChild<QPlainTextEdit *>(QStringLiteral("visitorNotice"));
    QVERIFY(board && notice);
    QCOMPARE(board->page(), Mode::Video);
    QVERIFY(manager.sendText(Loopback, QStringLiteral("显示图片")));
    QTRY_COMPARE(board->page(), Mode::Images);
    QVERIFY(board->activeImage().isEmpty());
    const auto message = QStringLiteral("提示：今日显示视频安排有更新。");
    QVERIFY(manager.sendText(Loopback, message));
    QTRY_COMPARE(notice->toPlainText(), message);
    QCOMPARE(board->page(), Mode::Images);

    const auto name = QStringLiteral("山水.png");
    const QString first = root.filePath(QStringLiteral("a/") + name);
    const QString second = root.filePath(QStringLiteral("b/") + name);
    QVERIFY(createLandscape(first));
    QVERIFY(createLandscape(second, true));
    const QString textPath = root.filePath(QStringLiteral("景区通知.txt"));
    const QString newNotice = QStringLiteral("步道维护通知\n请从东侧入口进入。");
    QVERIFY(writeFile(textPath, QByteArray::fromHex("efbbbf") + newNotice.toUtf8()));
    const QString zeroPath = root.filePath(QStringLiteral("零字节.bin"));
    QVERIFY(writeFile(zeroPath, {}));

    FileQueue queue;
    QVERIFY(queue.start(Loopback, manager.fileSocket(Loopback), {first, second, textPath, zeroPath}));
    QTRY_COMPARE_WITH_TIMEOUT(countReceipts(logs), 4, 12000);
    QVERIFY(!queue.isActive());
    const QDir received(receive);
    QVERIFY(readFile(received.filePath(name)) == readFile(first));
    QVERIFY(readFile(received.filePath(QStringLiteral("山水 (2).png"))) == readFile(second));
    QCOMPARE(QFileInfo(received.filePath(QStringLiteral("零字节.bin"))).size(), qint64(0));
    QCOMPARE(notice->toPlainText(), newNotice);
    QCOMPARE(board->page(), Mode::Images);
    QVERIFY(!board->activeImage().isEmpty());
    auto *slides = board->findChild<QTimer *>(QStringLiteral("slideTimer"));
    QVERIFY(slides && slides->isActive());
    QCOMPARE(slides->interval(), 5000);
    const QString current = board->activeImage();
    QTRY_VERIFY_WITH_TIMEOUT(board->activeImage() != current, 6500);

    QVERIFY(manager.sendText(Loopback, QStringLiteral("显示通知")));
    QTRY_COMPARE(board->page(), Mode::Notice);
    QVERIFY(manager.sendText(Loopback, QStringLiteral("显示视频")));
    QTRY_COMPARE(board->page(), Mode::Video);
    QVERIFY(manager.sendText(Loopback, QStringLiteral("循环播放")));
    QTRY_VERIFY(board->cycling());
    QCOMPARE(board->page(), Mode::Video);
    auto *cycle = board->findChild<QTimer *>(QStringLiteral("pageCycle"));
    QVERIFY(cycle);
    QCOMPARE(cycle->interval(), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(board->page(), Mode::Images, 17000);
    QVERIFY(QMetaObject::invokeMethod(cycle, "timeout"));
    QCOMPARE(board->page(), Mode::Notice);
    QVERIFY(manager.sendText(Loopback, QStringLiteral("显示图片")));
    QTRY_VERIFY(!board->cycling() && board->page() == Mode::Images);

    button(client, "disconnectServer")->click();
    client.close();
    ClientWindow reopened(receive, settings, false);
    auto *restored = reopened.findChild<QPlainTextEdit *>(QStringLiteral("visitorNotice"));
    QVERIFY(restored);
    QCOMPARE(restored->toPlainText(), newNotice);
}

void ClientTests::reconnectsAndDiscardsPartialFile()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const auto receive = folder.filePath(QStringLiteral("RecvFile"));
    TerminalServer manager(folder.filePath(QStringLiteral("manager")));
    QVERIFY(manager.start(0, QHostAddress::LocalHost));

    ClientConnection client(receive);
    QSignalSpy status(&client, &ClientConnection::statusChanged);
    client.connectToServer(Loopback, manager.port());
    QTRY_VERIFY(manager.fileSocket(Loopback));
    QTRY_COMPARE(status.last().first().toString(), QStringLiteral("已连接"));

    const QByteArray partial = LegacyProtocol::fileHeader(QStringLiteral("未完成.mp4"), 100) + "half";
    QCOMPARE(manager.fileSocket(Loopback)->write(partial), qint64(partial.size()));
    const QDir output(receive);
    QTRY_COMPARE(output.entryList(QDir::Files | QDir::Hidden).size(), 1);
    manager.fileSocket(Loopback)->abort();
    QTRY_COMPARE(status.last().first().toString(), QStringLiteral("等待自动重连"));
    QTRY_VERIFY(output.entryList(QDir::Files | QDir::Hidden).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(status.last().first().toString(), QStringLiteral("已连接"), 5500);
    QVERIFY(manager.hasControl(Loopback));
    QVERIFY(manager.fileSocket(Loopback));

    manager.stop();
    QTRY_COMPARE(status.last().first().toString(), QStringLiteral("等待自动重连"));
    client.disconnectFromServer();
    QVERIFY(manager.start(0, QHostAddress::LocalHost));
    QTest::qWait(3200);
    QCOMPARE(status.last().first().toString(), QStringLiteral("已断开"));
    QVERIFY(!manager.hasControl(Loopback));
}


void ClientTests::worksWithExistingManagerWindow()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString fixture = folder.filePath(QStringLiteral("联调风景.png"));
    QVERIFY(createLandscape(fixture));

    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    MainWindow manager(false);
    manager.show();
    manager.findChild<QSpinBox *>(QStringLiteral("listenPort"))->setValue(port);
    button(manager, "startListening")->click();
    QCOMPARE(manager.findChild<QLabel *>(QStringLiteral("listenStatus"))->text(),
             QStringLiteral("监听中"));

    QSettings settings(folder.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    const QString receive = folder.filePath(QStringLiteral("RecvFile"));
    ClientWindow client(receive, settings, false);
    client.show();
    pointAt(client, port);
    auto *terminals = manager.findChild<QTableWidget *>(QStringLiteral("terminalList"));
    QTRY_VERIFY(terminals->rowCount() == 1 && terminals->item(0, 1)
                && terminals->item(0, 1)->text() == QStringLiteral("就绪"));

    auto *board = client.findChild<DisplayBoard *>();
    button(manager, "displayImages")->click();
    QTRY_COMPARE(board->page(), Mode::Images);

    QTimer::singleShot(0, &manager, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->selectFile(fixture);
            QMetaObject::invokeMethod(dialog, "accept");
            if (dialog->isVisible()) {
                dialog->reject();
            }
        }
    });
    button(manager, "addFiles")->click();
    QVERIFY(button(manager, "sendFiles")->isEnabled());
    button(manager, "sendFiles")->click();
    const QString result = QDir(receive).filePath(QStringLiteral("联调风景.png"));
    QTRY_COMPARE(board->activeImage(), result);
    QVERIFY(readFile(result) == readFile(fixture));

    auto *log = manager.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
    QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("文件回执：接收文件完成")));
    auto *message = manager.findChild<QLineEdit *>(QStringLiteral("customMessage"));
    const QString text = QStringLiteral("请按现场标识有序通行。");
    message->setText(text);
    button(manager, "sendMessage")->click();
    button(manager, "displayNotice")->click();
    QTRY_COMPARE(board->page(), Mode::Notice);
    auto *notice = client.findChild<QPlainTextEdit *>(QStringLiteral("visitorNotice"));
    QCOMPARE(notice->toPlainText(), text);
}

void ClientTests::switchesLayoutAndWritesPreviews()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QVERIFY(createLandscape(folder.filePath(QStringLiteral("日出图集.png"))));
    const QString text = QStringLiteral("步道通行提醒\n\n"
                                        "东侧观景栈道今日正常开放。请沿指示牌单向通行，"
                                        "在观景平台停留拍照时注意脚下台阶。\n\n"
                                        "如遇降雨，山间步道容易湿滑。请放慢脚步，并照顾同行的儿童与长者。");
    QVERIFY(writeFile(folder.filePath(QStringLiteral("游客通知.txt")), text.toUtf8()));
    QSettings settings(folder.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    ClientWindow client(folder.path(), settings, false);
    client.show();
    client.activateWindow();
    auto *debug = client.findChild<QDialog *>(QStringLiteral("debugPanel"));
    auto *board = client.findChild<DisplayBoard *>();
    auto *weather = client.findChild<QFrame *>(QStringLiteral("weatherCard"));
    auto *report = client.findChild<QLabel *>(QStringLiteral("weatherReport"));
    QVERIFY(debug && board && weather && report);
    QVERIFY(!debug->isVisible());
    QCOMPARE(board->page(), Mode::Video);
    QVERIFY(QTest::qWaitForWindowActive(&client));

    QTest::keyClick(&client, Qt::Key_F12);
    QTRY_VERIFY(debug->isVisible());
    button(client, "previewImages")->click();
    auto *demo = client.findChild<QCheckBox *>(QStringLiteral("demoWeather"));
    demo->setChecked(true);
    QCOMPARE(report->text(), QStringLiteral("演示数据"));
    QCOMPARE(board->page(), Mode::Images);
    QVERIFY(!board->activeImage().isEmpty());

    QTest::keyClick(debug, Qt::Key_F12);
    QTRY_VERIFY(!debug->isVisible());
    client.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&client));
    QTest::keyClick(&client, Qt::Key_F11);
    QTRY_VERIFY(client.isFullScreen());
    QTest::keyClick(&client, Qt::Key_F11);
    QTRY_VERIFY(!client.isFullScreen());

    const QString previewDirectory = qEnvironmentVariable("SCENIC_PREVIEW_DIR");
    client.resize(1280, 720);
    QTRY_VERIFY(!weather->property("compact").toBool());
    QVERIFY(weather->isVisible());
    if (!previewDirectory.isEmpty()) {
        QVERIFY(QDir().mkpath(previewDirectory));
        QVERIFY(client.grab().save(QDir(previewDirectory).filePath(QStringLiteral("landscape.png"))));
    }

    button(client, "previewNotice")->click();
    client.resize(430, 760);
    QTRY_VERIFY(weather->property("compact").toBool());
    auto *conditions = client.findChild<QLabel *>(QStringLiteral("weatherConditions"));
    QTRY_VERIFY(report->y() > conditions->y());
    QCOMPARE(board->page(), Mode::Notice);
    auto *notice = client.findChild<QPlainTextEdit *>(QStringLiteral("visitorNotice"));
    QCOMPARE(notice->toPlainText(), text);
    QVERIFY(notice->isVisible());
    QVERIFY(weather->isVisible());
    QVERIFY(client.centralWidget()->rect().contains(weather->geometry()));
    if (!previewDirectory.isEmpty()) {
        QVERIFY(client.grab().save(QDir(previewDirectory).filePath(QStringLiteral("portrait.png"))));
    }

    button(client, "previewImages")->click();
    QVERIFY(QFile::remove(folder.filePath(QStringLiteral("日出图集.png"))));
    QVERIFY(writeFile(folder.filePath(QStringLiteral("损坏图片.png")), "invalid-image"));
    button(client, "rescanMedia")->click();
    QVERIFY(board->activeImage().isEmpty());
    bool sawInvalidImage = false;
    for (const auto *hint : board->findChildren<QLabel *>(QStringLiteral("mediaHint"))) {
        sawInvalidImage |= hint->isVisible() && hint->text() == QStringLiteral("当前图片无法读取");
    }
    QVERIFY(sawInvalidImage);
}

void ClientTests::playsVideoOnWindows()
{
    const QString videoPath = qEnvironmentVariable("SCENIC_TEST_VIDEO");
    if (videoPath.isEmpty()) {
        QSKIP("Set SCENIC_TEST_VIDEO to a local playable video to test the Windows backend.");
    }
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = folder.filePath(QFileInfo(videoPath).fileName());
    QVERIFY(QFile::copy(videoPath, path));
    QSettings settings(folder.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    ClientWindow client(folder.path(), settings, false);
    client.show();
    auto *board = client.findChild<DisplayBoard *>();
    auto *decoder = client.findChild<NativeVideo *>(QStringLiteral("nativeVideo"));
    auto *player = client.findChild<QMediaPlayer *>(QStringLiteral("scenePlayer"));
    QVERIFY(board && player && decoder);

    if (NativeVideo::accepts(path)) {
        int frames = 0;
        QImage lastFrame;
        connect(decoder, &NativeVideo::frameReady, &client, [&](const QImage &frame) {
            ++frames;
            lastFrame = frame;
        });
        QTRY_VERIFY_WITH_TIMEOUT(frames >= 6, 10000);
        QVERIFY(lastFrame.width() > 0 && lastFrame.height() > 0);
        if (QFileInfo(videoPath).fileName() == QStringLiteral("star.mp4")) {
            QCOMPARE(lastFrame.size(), QSize(1920, 1080));
        }
        QVERIFY(client.findChild<QWidget *>(QStringLiteral("nativeVideoFrame"))->isVisible());
        const QString playbackPreview = qEnvironmentVariable("SCENIC_PLAYBACK_PREVIEW");
        if (!playbackPreview.isEmpty()) {
            QVERIFY(client.grab().save(playbackPreview));
        }
        button(client, "previewImages")->click();
        QTest::qWait(150);
        const int pausedFrames = frames;
        QTest::qWait(250);
        QCOMPARE(frames, pausedFrames);
        button(client, "previewVideo")->click();
        QTRY_VERIFY(frames > pausedFrames);
    } else {
        QTRY_VERIFY_WITH_TIMEOUT(player->state() == QMediaPlayer::PlayingState && player->position() >= 750, 10000);
        QVERIFY(player->isVideoAvailable());
        button(client, "previewImages")->click();
        QTRY_COMPARE(player->state(), QMediaPlayer::PausedState);
        button(client, "previewVideo")->click();
        QTRY_COMPARE(player->state(), QMediaPlayer::PlayingState);
    }
}

int main(int argc, char **argv)
{
    qunsetenv("AMAP_WEATHER_KEY");
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    ClientTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "client-tests.moc"
