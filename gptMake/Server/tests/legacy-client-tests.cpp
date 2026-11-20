#include "../filequeue.h"
#include "../terminalserver.h"
#include "../../../TCPClient/mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class LegacyClientTests : public QObject
{
    Q_OBJECT

private slots:
    void exchangesTextAndFilesWithOriginalClient();
};

void LegacyClientTests::exchangesTextAndFilesWithOriginalClient()
{
    const auto peer = QStringLiteral("127.0.0.1");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    QDir root(directory.path());
    QVERIFY(root.mkdir(QStringLiteral("RecvFile")));
    QVERIFY(root.mkdir(QStringLiteral("outgoing")));
    const auto receivePath = root.filePath(QStringLiteral("RecvFile"));
    TerminalServer server(receivePath);
    QVERIFY(server.start(0, QHostAddress::LocalHost));
    QSignalSpy logs(&server, &TerminalServer::logMessage);

    MainWindow legacyWindow;
    legacyWindow.show();
    auto *host = legacyWindow.findChild<QComboBox *>(QStringLiteral("comboServer"));
    auto *port = legacyWindow.findChild<QSpinBox *>(QStringLiteral("spinPort"));
    auto *connectButton = legacyWindow.findChild<QAction *>(QStringLiteral("actConnect"));
    auto *textLog = legacyWindow.findChild<QPlainTextEdit *>(QStringLiteral("plainTextEdit"));
    auto *filesLog = legacyWindow.findChild<QPlainTextEdit *>(QStringLiteral("plainTextEdit_file"));
    auto *message = legacyWindow.findChild<QLineEdit *>(QStringLiteral("editMsg"));
    auto *sendMessage = legacyWindow.findChild<QPushButton *>(QStringLiteral("btnSend"));
    auto *selectFile = legacyWindow.findChild<QPushButton *>(QStringLiteral("btnSelectFile"));
    auto *sendFile = legacyWindow.findChild<QPushButton *>(QStringLiteral("btnSendFile"));
    QVERIFY(host && port && connectButton && textLog && filesLog && message
            && sendMessage && selectFile && sendFile);

    host->addItem(peer);
    host->setCurrentText(peer);
    port->setValue(server.port());
    connectButton->trigger();
    QTRY_VERIFY(server.fileSocket(peer));

    QVERIFY(server.sendText(peer, QStringLiteral("显示图片")));
    QTRY_VERIFY(textLog->toPlainText().contains(QStringLiteral("显示图片")));
    message->setText(QStringLiteral("接受文件完成"));
    sendMessage->click();
    QTRY_VERIFY(logs.last().first().toString().contains(QStringLiteral("文件回执")));

    const QString downloadName = QStringLiteral("联调下载-%1.txt")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString destination = QDir(QCoreApplication::applicationDirPath()).filePath(downloadName);
    const QString download = root.filePath(QStringLiteral("outgoing/") + downloadName);
    const QByteArray downloadBytes(130000, 'd');
    QFile source(download);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(downloadBytes), qint64(downloadBytes.size()));
    source.close();

    FileQueue queue;
    QSignalSpy sent(&queue, &FileQueue::finished);
    QVERIFY(queue.start(peer, server.fileSocket(peer), {download}));
    QTRY_COMPARE(sent.size(), 1);
    QVERIFY(sent.first().at(1).toBool());
    QTRY_VERIFY(filesLog->toPlainText().contains(QStringLiteral("接收文件 %1 成功！").arg(downloadName)));

    QFile result(destination);
    QVERIFY(result.open(QIODevice::ReadOnly));
    const auto received = result.readAll();
    result.close();
    QVERIFY(result.remove());
    QVERIFY(received == downloadBytes);

    const QString uploadName = QStringLiteral("联调上传.txt");
    const QByteArray uploadBytes(175000, 'u');
    QFile upload(root.filePath(uploadName));
    QVERIFY(upload.open(QIODevice::WriteOnly));
    QCOMPARE(upload.write(uploadBytes), qint64(uploadBytes.size()));
    upload.close();

    QTimer::singleShot(0, &legacyWindow, [&] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            dialog->selectFile(upload.fileName());
            QMetaObject::invokeMethod(dialog, "accept");
            if (dialog->isVisible()) {
                dialog->reject();
            }
        }
    });
    selectFile->click();
    QVERIFY(sendFile->isEnabled());
    sendFile->click();
    const QString savedUpload = QDir(receivePath).filePath(uploadName);
    QTRY_VERIFY(QFile::exists(savedUpload));
    QFile uploaded(savedUpload);
    QVERIFY(uploaded.open(QIODevice::ReadOnly));
    QVERIFY(uploaded.readAll() == uploadBytes);
    QTRY_VERIFY(logs.last().first().toString().contains(QStringLiteral("已接收文件")));
}

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    LegacyClientTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "legacy-client-tests.moc"
