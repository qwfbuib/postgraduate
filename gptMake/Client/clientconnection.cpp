#include "clientconnection.h"

#include "uploadreceiver.h"

#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpSocket>

namespace {
constexpr int RetryDelayMs = 3000;
constexpr int ConnectTimeoutMs = 6000;
}

ClientConnection::ClientConnection(const QString &receiveDirectory, QObject *parent)
    : QObject(parent), m_receiveDirectory(receiveDirectory)
{
    m_retry.setSingleShot(true);
    m_deadline.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &ClientConnection::openControl);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        connectionLost(QStringLiteral("连接超时"));
    });
}

ClientConnection::~ClientConnection()
{
    closeSockets();
}

void ClientConnection::connectToServer(const QString &host, quint16 port)
{
    m_retry.stop();
    closeSockets();
    m_host = host;
    m_port = port;
    m_reconnect = true;
    openControl();
}

void ClientConnection::disconnectFromServer()
{
    m_reconnect = false;
    m_retry.stop();
    if (m_receiver && m_receiver->hasPendingFile()) {
        emit logMessage(QStringLiteral("已清理未接收完整的文件"));
    }
    closeSockets();
    emit statusChanged(QStringLiteral("已断开"));
    emit logMessage(QStringLiteral("已手动断开"));
}

void ClientConnection::openControl()
{
    if (!m_reconnect) {
        return;
    }

    emit statusChanged(QStringLiteral("正在连接指令通道"));
    m_control = new QTcpSocket(this);
    m_control->setProxy(QNetworkProxy::NoProxy);
    m_control->setReadBufferSize(LegacyProtocol::MaxTextBytes);
    watchSocket(m_control);

    connect(m_control, &QTcpSocket::readyRead, this, &ClientConnection::readCommands);
    connect(m_control, &QTcpSocket::connected, this, &ClientConnection::openFiles);
    m_deadline.start(ConnectTimeoutMs);
    m_control->connectToHost(m_host, m_port);
}

void ClientConnection::openFiles()
{
    emit statusChanged(QStringLiteral("正在连接文件通道"));
    m_files = new QTcpSocket(this);
    m_files->setProxy(QNetworkProxy::NoProxy);
    m_receiver = new UploadReceiver(m_files, m_receiveDirectory);

    connect(m_receiver, &UploadReceiver::saved, this, &ClientConnection::acknowledgeFile);
    connect(m_receiver, &UploadReceiver::failed, this, [this](const QString &reason) {
        emit logMessage(QStringLiteral("接收文件失败：%1").arg(reason));
    });
    watchSocket(m_files);
    connect(m_files, &QTcpSocket::connected, this, [this] {
        m_deadline.stop();
        emit statusChanged(QStringLiteral("已连接"));
        emit logMessage(QStringLiteral("已连接 %1:%2，指令和文件通道就绪").arg(m_host).arg(m_port));
    });
    m_deadline.start(ConnectTimeoutMs);
    m_files->connectToHost(m_control->peerAddress(), m_port);
}

void ClientConnection::readCommands()
{
    using Mode = LegacyProtocol::DisplayMode;
    while (m_control && m_control->canReadLine()) {
        QByteArray line = m_control->readLine();
        line.chop(1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }

        const QString text = QString::fromUtf8(line);
        if (text.trimmed().isEmpty()) {
            continue;
        }

        bool command = false;
        for (const auto mode : {Mode::Video, Mode::Images, Mode::Notice, Mode::Cycle}) {
            if (text == LegacyProtocol::command(mode)) {
                emit logMessage(QStringLiteral("收到指令：%1").arg(text));
                emit displayRequested(mode);
                command = true;
                break;
            }
        }
        if (!command) {
            emit logMessage(QStringLiteral("收到自定义通知"));
            emit noticeReceived(text);
        }
    }
    if (m_control && m_control->bytesAvailable() >= LegacyProtocol::MaxTextBytes) {
        connectionLost(QStringLiteral("收到过长的指令行"));
    }
}

void ClientConnection::acknowledgeFile(const QString &name)
{
    const auto frame = LegacyProtocol::textFrame(QStringLiteral("接收文件完成"));
    const bool sent = m_control && m_control->state() == QAbstractSocket::ConnectedState
        && m_control->write(frame) == frame.size();
    emit logMessage(sent ? QStringLiteral("已保存 %1，并发送回执").arg(name)
                         : QStringLiteral("已保存 %1，回执未发出").arg(name));
    emit fileReceived(name);
}

void ClientConnection::watchSocket(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
        if (socket == m_control || socket == m_files) {
            connectionLost(QStringLiteral("与管理端的连接已断开"));
        }
    });
    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError error) {
        // The disconnected signal follows a remote close after the final data has been delivered.
        if (error != QAbstractSocket::RemoteHostClosedError && (socket == m_control || socket == m_files)) {
            connectionLost(socket->errorString());
        }
    });
}

void ClientConnection::connectionLost(const QString &reason)
{
    const bool incomplete = m_receiver && m_receiver->hasPendingFile();
    closeSockets();
    emit logMessage(reason);
    if (incomplete) {
        emit logMessage(QStringLiteral("已清理未接收完整的文件"));
    }
    if (m_reconnect) {
        emit statusChanged(QStringLiteral("等待自动重连"));
        m_retry.start(RetryDelayMs);
    }
}

void ClientConnection::closeSockets()
{
    m_deadline.stop();
    if (m_receiver) {
        disconnect(m_receiver, nullptr, this, nullptr);
        m_receiver = nullptr;
    }

    for (QTcpSocket **channel : {&m_files, &m_control}) {
        if (auto *socket = *channel) {
            *channel = nullptr;
            disconnect(socket, nullptr, this, nullptr);
            socket->abort();
            socket->deleteLater();
        }
    }
}
