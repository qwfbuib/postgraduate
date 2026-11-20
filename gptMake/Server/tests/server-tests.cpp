#include "addresslist.h"
#include "filequeue.h"
#include "mainwindow.h"
#include "terminalserver.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkProxy>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {

const QString FirstTerminal = QStringLiteral("127.0.0.1");
const QString OtherTerminal = QStringLiteral("127.0.0.2");

bool connectFrom(QTcpSocket &socket, quint16 port, const QString &address)
{
    socket.setProxy(QNetworkProxy::NoProxy);
    if (address != FirstTerminal && !socket.bind(QHostAddress(address), quint16(0))) {
        qWarning() << "Binding test address failed:" << address << socket.errorString();
        return false;
    }
    socket.connectToHost(FirstTerminal, port, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
    if (socket.state() == QAbstractSocket::ConnectedState || socket.waitForConnected(3000)) {
        return true;
    }
    qWarning() << "Connecting test peer failed:" << address << socket.errorString();
    return false;
}

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

QByteArray oldWireFrame(const QString &name, const QByteArray &payload)
{
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_9);
    stream << qint64(0) << qint64(0) << name;
    const auto headerSize = frame.size();
    stream.device()->seek(0);
    stream << qint64(headerSize) + payload.size() << qint64(headerSize - 16);
    frame.append(payload);
    return frame;
}

void capture(QTcpSocket &socket, QByteArray &buffer)
{
    QObject::connect(&socket, &QTcpSocket::readyRead, &socket,
                     [&socket, &buffer] { buffer.append(socket.readAll()); });
}

}

class ServerTests : public QObject
{
    Q_OBJECT

private slots:
    void listensAndReportsPortConflict();
    void pairsConnectionsAndLogsSplitText();
    void sendsBatchByteForByteWithoutReceipt();
    void stopsAfterMissingFile();
    void stopsAfterSocketDisconnectAndCanReconnect();
    void receivesFragmentedAndAdjacentUploads();
    void dropsIncompleteUploadWithoutLeavingFile();
    void wrapsAllLocalAddressesOnWindowResize();
    void selectsAndControlsTerminalsInTheWindow();
    void sendsOnlyPendingFilesForEachTerminal();
};

void ServerTests::listensAndReportsPortConflict()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer first(directory.path());
    TerminalServer second(directory.path());
    QSignalSpy logs(&second, &TerminalServer::logMessage);
    QVERIFY(first.start(0, QHostAddress::LocalHost));
    const auto port = first.port();
    QVERIFY(!second.start(port, QHostAddress::LocalHost));
    QVERIFY(!second.errorString().isEmpty());
    QVERIFY(logs.last().first().toString().contains(QStringLiteral("无法监听端口")));
    first.stop();
    QVERIFY(second.start(port, QHostAddress::LocalHost));
}

void ServerTests::pairsConnectionsAndLogsSplitText()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QSignalSpy logs(&server, &TerminalServer::logMessage);
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QTcpSocket control;
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QTRY_VERIFY(server.hasControl(FirstTerminal));
    QVERIFY(!server.fileSocket(FirstTerminal));

    QTcpSocket files;
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    QTcpSocket extra;
    QVERIFY(connectFrom(extra, server.port(), FirstTerminal));
    QTRY_COMPARE(extra.state(), QAbstractSocket::UnconnectedState);
    QVERIFY(server.fileSocket(FirstTerminal));

    const auto lines = QStringLiteral("接收文件完成\r\n终端已就绪\n").toUtf8();
    const int logsBeforeText = logs.size();
    QCOMPARE(control.write(lines.left(5)), qint64(5));
    QTest::qWait(40);
    QCOMPARE(logs.size(), logsBeforeText);
    QCOMPARE(control.write(lines.mid(5)), qint64(lines.size() - 5));
    QTRY_COMPARE(logs.size(), logsBeforeText + 2);
    QVERIFY(logs.at(logsBeforeText).first().toString().contains(QStringLiteral("文件回执")));
    QVERIFY(logs.last().first().toString().endsWith(QStringLiteral("终端已就绪")));

    control.abort();
    QTRY_VERIFY(!server.hasControl(FirstTerminal));
    QTRY_COMPARE(files.state(), QAbstractSocket::UnconnectedState);
}

void ServerTests::sendsBatchByteForByteWithoutReceipt()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QByteArray received;
    QTcpSocket control;
    QTcpSocket files;
    capture(files, received);
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    const QStringList names = {QStringLiteral("风景.jpg"), QStringLiteral("空文件.txt"),
                               QStringLiteral("大文件.mp4")};
    QByteArray large("0123456789abcdef");
    large = large.repeated(700000);
    const QList<QByteArray> contents = {QByteArray("photo example"), QByteArray(), large};
    QStringList paths;
    QByteArray expected;
    for (int i = 0; i < names.size(); ++i) {
        const QString path = directory.filePath(names.at(i));
        QVERIFY(writeFile(path, contents.at(i)));
        paths.append(path);
        expected.append(oldWireFrame(names.at(i), contents.at(i)));
    }

    FileQueue queue;
    QSignalSpy done(&queue, &FileQueue::finished);
    QSignalSpy progress(&queue, &FileQueue::progressChanged);
    QVERIFY(!queue.start(FirstTerminal, server.fileSocket(FirstTerminal), {}));
    QVERIFY(queue.start(FirstTerminal, server.fileSocket(FirstTerminal), paths));
    QVERIFY(!queue.start(FirstTerminal, server.fileSocket(FirstTerminal), paths));
    QTRY_VERIFY_WITH_TIMEOUT(!queue.isActive(), 15000);
    QTRY_COMPARE_WITH_TIMEOUT(received.size(), expected.size(), 8000);
    QVERIFY(received == expected);
    QCOMPARE(done.size(), 1);
    QVERIFY(done.first().at(1).toBool());
    QCOMPARE(progress.last().at(1).toInt(), 100);
}

void ServerTests::stopsAfterMissingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QTcpSocket control;
    QTcpSocket files;
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    const QString first = directory.filePath(QStringLiteral("first.txt"));
    const QString removed = directory.filePath(QStringLiteral("removed.txt"));
    const QString last = directory.filePath(QStringLiteral("last.txt"));
    QVERIFY(writeFile(first, "first"));
    QVERIFY(writeFile(removed, "removed"));
    QVERIFY(writeFile(last, "last"));

    FileQueue queue;
    QList<int> sent;
    QList<int> failed;
    connect(&queue, &FileQueue::fileChanged, this,
            [&](int row, FileQueue::Status status, const QString &) {
        if (status == FileQueue::Status::Sent) {
            sent.append(row);
        }
        if (status == FileQueue::Status::Failed) {
            failed.append(row);
        }
    });

    QVERIFY(queue.start(FirstTerminal, server.fileSocket(FirstTerminal), {first, removed, last}));
    QVERIFY(QFile::remove(removed));
    QTRY_VERIFY(!queue.isActive());
    QCOMPARE(sent, QList<int>({0}));
    QCOMPARE(failed, QList<int>({1}));
    QVERIFY(server.fileSocket(FirstTerminal));
}

void ServerTests::stopsAfterSocketDisconnectAndCanReconnect()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QTcpSocket control;
    QTcpSocket files;
    files.setReadBufferSize(1);
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    const QString largePath = directory.filePath(QStringLiteral("large.mp4"));
    QFile large(largePath);
    QVERIFY(large.open(QIODevice::WriteOnly));
    QVERIFY(large.resize(24 * 1024 * 1024));
    large.close();

    FileQueue queue;
    QSignalSpy done(&queue, &FileQueue::finished);
    int failedRow = -1;
    connect(&queue, &FileQueue::fileChanged, this,
            [&](int row, FileQueue::Status status, const QString &) {
        if (status == FileQueue::Status::Failed) {
            failedRow = row;
        }
    });
    QVERIFY(queue.start(FirstTerminal, server.fileSocket(FirstTerminal), {largePath, largePath}));
    files.abort();
    QTRY_VERIFY(!queue.isActive());
    QCOMPARE(failedRow, 0);
    QCOMPARE(done.size(), 1);
    QVERIFY(!done.first().at(1).toBool());
    QVERIFY(server.hasControl(FirstTerminal));
    QVERIFY(!server.fileSocket(FirstTerminal));

    QTcpSocket replacement;
    QVERIFY(connectFrom(replacement, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));
    server.stop();
    QTRY_COMPARE(replacement.state(), QAbstractSocket::UnconnectedState);
}

void ServerTests::receivesFragmentedAndAdjacentUploads()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QTcpSocket control;
    QTcpSocket files;
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    const QString name = QStringLiteral("终端上传.txt");
    const QByteArray first = oldWireFrame(name, "abcdef");
    QCOMPARE(files.write(first.left(9)), qint64(9));
    QTest::qWait(35);
    QVERIFY(!QFile::exists(directory.filePath(name)));
    const QByteArray rest = first.mid(9) + oldWireFrame(name, "second")
        + oldWireFrame(QStringLiteral("blank.txt"), {});
    QCOMPARE(files.write(rest), qint64(rest.size()));

    QTRY_VERIFY(QFile::exists(directory.filePath(QStringLiteral("blank.txt"))));
    QCOMPARE(readFile(directory.filePath(name)), QByteArray("abcdef"));
    QCOMPARE(readFile(directory.filePath(QStringLiteral("终端上传 (2).txt"))), QByteArray("second"));
    QCOMPARE(QFileInfo(directory.filePath(QStringLiteral("blank.txt"))).size(), qint64(0));
}

void ServerTests::dropsIncompleteUploadWithoutLeavingFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    TerminalServer server(directory.path());
    QVERIFY(server.start(0, QHostAddress::LocalHost));

    QTcpSocket control;
    QTcpSocket files;
    QVERIFY(connectFrom(control, server.port(), FirstTerminal));
    QVERIFY(connectFrom(files, server.port(), FirstTerminal));
    QTRY_VERIFY(server.fileSocket(FirstTerminal));

    QByteArray partial = oldWireFrame(QStringLiteral("partial.txt"), "incomplete");
    partial.chop(4);
    QCOMPARE(files.write(partial), qint64(partial.size()));
    const QDir uploaded(directory.path());
    QTRY_COMPARE(uploaded.entryList(QDir::Files | QDir::Hidden).size(), 1);
    files.disconnectFromHost();
    QTRY_VERIFY(!server.fileSocket(FirstTerminal));
    QTRY_VERIFY(uploaded.entryList(QDir::Files | QDir::Hidden).isEmpty());
}

void ServerTests::wrapsAllLocalAddressesOnWindowResize()
{
    MainWindow window(false);
    window.show();
    auto *list = dynamic_cast<AddressList *>(
        window.findChild<QWidget *>(QStringLiteral("localAddresses")));
    QVERIFY(list);
    QVERIFY(!list->findChildren<QLabel *>().isEmpty());

    QStringList addresses;
    for (int i = 1; i <= 24; ++i) {
        addresses.append(QStringLiteral("192.168.233.%1").arg(i + 100));
    }
    list->setAddresses(addresses);

    const auto labels = list->findChildren<QLabel *>();
    QCOMPARE(labels.size(), addresses.size());

    const auto allAddressesVisible = [list, &labels, &addresses] {
        for (int i = 0; i < labels.size(); ++i) {
            const auto *label = labels.at(i);
            if (!label->isVisible() || label->text() != addresses.at(i)
                || label->width() < label->sizeHint().width()
                || !list->rect().contains(label->geometry())) {
                return false;
            }
        }
        return true;
    };

    window.resize(1640, 820);
    QTRY_VERIFY(allAddressesVisible());
    const int wideLastRow = labels.last()->y();

    window.resize(window.minimumWidth(), 820);
    QTRY_VERIFY(allAddressesVisible());
    QVERIFY(labels.last()->y() > wideLastRow);

    window.resize(1640, 820);
    QTRY_VERIFY(allAddressesVisible());
    QCOMPARE(labels.last()->y(), wideLastRow);
}


void ServerTests::selectsAndControlsTerminalsInTheWindow()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer occupied;
    QVERIFY(occupied.listen(QHostAddress::Any, 0));

    MainWindow window(false);
    window.show();
    const auto findButton = [&](const char *name) {
        return window.findChild<QPushButton *>(QString::fromLatin1(name));
    };
    auto *port = window.findChild<QSpinBox *>(QStringLiteral("listenPort"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("listenStatus"));
    auto *terminals = window.findChild<QTableWidget *>(QStringLiteral("terminalList"));
    auto *files = window.findChild<QTableWidget *>(QStringLiteral("fileQueue"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
    auto *message = window.findChild<QLineEdit *>(QStringLiteral("customMessage"));
    auto *send = findButton("sendFiles");
    QVERIFY(port && status && terminals && files && log && message && send);
    const auto localPort = occupied.serverPort();
    port->setValue(localPort);
    findButton("startListening")->click();
    QCOMPARE(status->text(), QStringLiteral("监听失败"));

    occupied.close();
    findButton("startListening")->click();
    QCOMPARE(status->text(), QStringLiteral("监听中"));
    QVERIFY(!send->isEnabled());

    QByteArray firstCommands;
    QByteArray secondCommands;
    QByteArray firstFiles;
    QByteArray secondFiles;
    QTcpSocket firstControl;
    QTcpSocket firstData;
    QTcpSocket secondControl;
    QTcpSocket secondData;
    capture(firstControl, firstCommands);
    capture(firstData, firstFiles);
    capture(secondControl, secondCommands);
    capture(secondData, secondFiles);

    QVERIFY(connectFrom(firstControl, localPort, FirstTerminal));
    QVERIFY(connectFrom(firstData, localPort, FirstTerminal));
    QVERIFY(connectFrom(secondControl, localPort, OtherTerminal));
    QVERIFY(connectFrom(secondData, localPort, OtherTerminal));
    QTRY_COMPARE(terminals->rowCount(), 2);
    QTRY_COMPARE(terminals->item(1, 1)->text(), QStringLiteral("就绪"));
    QCOMPARE(terminals->item(0, 0)->text(), FirstTerminal);
    QCOMPARE(terminals->item(1, 0)->text(), OtherTerminal);

    QDir folder(directory.path());
    QVERIFY(folder.mkdir(QStringLiteral("示例素材")));
    QVERIFY(folder.cd(QStringLiteral("示例素材")));
    QVERIFY(folder.mkdir(QStringLiteral("子文件夹")));
    const QString firstName = QStringLiteral("01-景区照片.jpg");
    const QString secondName = QStringLiteral("02-游客通知.txt");
    const QByteArray photo(80000, 'x');
    QVERIFY(writeFile(folder.filePath(firstName), photo));
    QVERIFY(writeFile(folder.filePath(secondName), "notice"));
    QVERIFY(writeFile(folder.filePath(QStringLiteral("子文件夹/忽略.txt")), "nested"));

    bool selectedFolder = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->setDirectory(directory.path());
            dialog->selectFile(folder.absolutePath());
            selectedFolder = QMetaObject::invokeMethod(dialog, "accept");
            if (dialog->isVisible()) {
                dialog->reject();
            }
        }
    });
    findButton("addFolder")->click();
    QVERIFY(selectedFolder);
    QCOMPARE(files->rowCount(), 2);

    bool selectedFile = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->selectFile(folder.filePath(QStringLiteral("子文件夹/忽略.txt")));
            selectedFile = QMetaObject::invokeMethod(dialog, "accept");
            if (dialog->isVisible()) {
                dialog->reject();
            }
        }
    });
    findButton("addFiles")->click();
    QVERIFY(selectedFile);
    QCOMPARE(files->rowCount(), 3);
    files->selectRow(2);
    findButton("removeFiles")->click();
    QCOMPARE(files->rowCount(), 2);

    terminals->selectRow(0);
    findButton("displayVideo")->click();
    findButton("displayImages")->click();
    findButton("displayNotice")->click();
    findButton("displayCycle")->click();
    QVERIFY(send->isEnabled());
    send->click();

    terminals->selectRow(1);
    QVERIFY(!send->isEnabled());
    message->setText(QStringLiteral("测试信息"));
    findButton("sendMessage")->click();
    QTRY_COMPARE(secondCommands, QStringLiteral("测试信息\n").toUtf8());
    QTRY_COMPARE(firstCommands,
                 QStringLiteral("显示视频\n显示图片\n显示通知\n循环播放\n").toUtf8());

    const QByteArray expected = oldWireFrame(firstName, photo) + oldWireFrame(secondName, "notice");
    QTRY_COMPARE(firstFiles.size(), expected.size());
    QVERIFY(firstFiles == expected);
    QCOMPARE(files->item(1, 2)->text(), QStringLiteral("未发送"));
    terminals->selectRow(0);
    QTRY_COMPARE(files->item(1, 2)->text(), QStringLiteral("已发出"));
    QVERIFY(secondFiles.isEmpty());
    QVERIFY(log->toPlainText().contains(QStringLiteral("素材批次发送完成")));
    for (const auto *bar : window.findChildren<QProgressBar *>()) {
        QCOMPARE(bar->value(), 100);
    }

    const QString snapshot = qEnvironmentVariable("SCENIC_UI_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        QVERIFY(window.grab().save(snapshot));
    }

    findButton("stopListening")->click();
    QCOMPARE(terminals->rowCount(), 0);
    QCOMPARE(status->text(), QStringLiteral("未监听"));
    QVERIFY(!send->isEnabled());
}


void ServerTests::sendsOnlyPendingFilesForEachTerminal()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::Any, 0));
    const auto port = reservation.serverPort();
    reservation.close();

    MainWindow window(false);
    window.show();
    const auto findButton = [&window](const char *name) {
        return window.findChild<QPushButton *>(QString::fromLatin1(name));
    };
    window.findChild<QSpinBox *>(QStringLiteral("listenPort"))->setValue(port);
    findButton("startListening")->click();
    auto *terminals = window.findChild<QTableWidget *>(QStringLiteral("terminalList"));
    auto *files = window.findChild<QTableWidget *>(QStringLiteral("fileQueue"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("eventLog"));
    auto *send = findButton("sendFiles");
    QCOMPARE(window.findChild<QLabel *>(QStringLiteral("listenStatus"))->text(), QStringLiteral("监听中"));

    QByteArray firstReceived;
    QByteArray secondReceived;
    QTcpSocket firstControl;
    QTcpSocket firstData;
    QTcpSocket secondControl;
    QTcpSocket secondData;
    capture(firstData, firstReceived);
    capture(secondData, secondReceived);
    QVERIFY(connectFrom(firstControl, port, FirstTerminal));
    QVERIFY(connectFrom(firstData, port, FirstTerminal));
    QVERIFY(connectFrom(secondControl, port, OtherTerminal));
    QVERIFY(connectFrom(secondData, port, OtherTerminal));
    QTRY_VERIFY(terminals->rowCount() == 2 && terminals->item(1, 1)
                && terminals->item(1, 1)->text() == QStringLiteral("就绪"));
    terminals->selectRow(0);

    const auto addFile = [&](const QString &path) {
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept");
                if (dialog->isVisible()) {
                    dialog->reject();
                }
            }
        });
        findButton("addFiles")->click();
    };
    const QString firstName = QStringLiteral("首发.txt");
    const QString retryName = QStringLiteral("重试.txt");
    const QString tailName = QStringLiteral("追加.txt");
    const QString first = directory.filePath(firstName);
    const QString retry = directory.filePath(retryName);
    const QString tail = directory.filePath(tailName);
    QVERIFY(writeFile(first, "original"));
    addFile(first);
    QCOMPARE(files->rowCount(), 1);
    QVERIFY(send->isEnabled());
    send->click();

    const QByteArray originalFrame = oldWireFrame(firstName, "original");
    QTRY_COMPARE(firstReceived, originalFrame);
    QTRY_COMPARE(files->item(0, 2)->text(), QStringLiteral("已发出"));
    send->click();
    QVERIFY(log->toPlainText().contains(QStringLiteral("没有待发送的素材")));
    QCOMPARE(firstReceived, originalFrame);

    QVERIFY(writeFile(retry, "retry"));
    QVERIFY(writeFile(tail, "new tail"));
    addFile(retry);
    addFile(tail);
    QCOMPARE(files->rowCount(), 3);
    QVERIFY(QFile::remove(retry));
    send->click();
    QCOMPARE(files->item(0, 2)->text(), QStringLiteral("已发出"));
    QCOMPARE(files->item(1, 2)->text(), QStringLiteral("失败"));
    QCOMPARE(files->item(2, 2)->text(), QStringLiteral("未发送"));
    QCOMPARE(firstReceived, originalFrame);

    QVERIFY(writeFile(retry, "retry"));
    send->click();
    const QByteArray all = originalFrame + oldWireFrame(retryName, "retry") + oldWireFrame(tailName, "new tail");
    QTRY_COMPARE(firstReceived, all);
    QTRY_COMPARE(files->item(2, 2)->text(), QStringLiteral("已发出"));
    QVERIFY(secondReceived.isEmpty());

    terminals->selectRow(1);
    QCOMPARE(files->item(0, 2)->text(), QStringLiteral("未发送"));
    send->click();
    QTRY_COMPARE(secondReceived, all);
    QTRY_COMPARE(files->item(2, 2)->text(), QStringLiteral("已发出"));

    QVERIFY(writeFile(first, "updated version"));
    const QByteArray update = oldWireFrame(firstName, "updated version");
    send->click();
    QTRY_COMPARE(secondReceived, all + update);
    QTRY_COMPARE(files->item(0, 2)->text(), QStringLiteral("已发出"));
    QCOMPARE(firstReceived, all);

    terminals->selectRow(0);
    QCOMPARE(files->item(0, 2)->text(), QStringLiteral("未发送"));
    QCOMPARE(files->item(1, 2)->text(), QStringLiteral("已发出"));
    send->click();
    QTRY_COMPARE(firstReceived, all + update);
    QTRY_COMPARE(files->item(0, 2)->text(), QStringLiteral("已发出"));
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    ServerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "server-tests.moc"
