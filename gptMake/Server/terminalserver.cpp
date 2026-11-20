#include "terminalserver.h"

#include "legacyprotocol.h"
#include "uploadreceiver.h"

#include <QTcpSocket>

namespace {

QString terminalAddress(const QHostAddress &address)
{
    bool isIpv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIpv4);
    return isIpv4 ? QHostAddress(ipv4).toString() : address.toString();
}

bool connected(const QTcpSocket *socket)
{
    return socket && socket->state() == QAbstractSocket::ConnectedState;
}

}

TerminalServer::TerminalServer(const QString &receiveDirectory, QObject *parent)
    : QObject(parent), m_receiveDirectory(receiveDirectory)
{
    connect(&m_server, &QTcpServer::newConnection, this, &TerminalServer::acceptConnections);
}

TerminalServer::~TerminalServer()
{
    disconnect(this, nullptr, nullptr, nullptr);
    stop();
}

bool TerminalServer::start(quint16 port, const QHostAddress &address)
{
    if (m_server.isListening()) {
        return false;
    }

    if (!m_server.listen(address, port)) {
        emit logMessage(QStringLiteral("无法监听端口 %1：%2").arg(port).arg(errorString()));
        return false;
    }

    emit listeningChanged(true);
    emit logMessage(QStringLiteral("开始监听端口 %1").arg(this->port()));
    return true;
}

void TerminalServer::stop()
{
    const bool wasListening = m_server.isListening();
    m_server.close();
    for (const QString &terminal : m_sessions.keys()) {
        removeSession(terminal);
    }

    if (wasListening) {
        emit listeningChanged(false);
        emit logMessage(QStringLiteral("已停止监听"));
    }
}

quint16 TerminalServer::port() const
{
    return m_server.serverPort();
}

QString TerminalServer::errorString() const
{
    return m_server.errorString();
}

bool TerminalServer::hasControl(const QString &terminal) const
{
    return connected(m_sessions.value(terminal).control);
}

QTcpSocket *TerminalServer::fileSocket(const QString &terminal) const
{
    auto *socket = m_sessions.value(terminal).files;
    return connected(socket) ? socket : nullptr;
}

bool TerminalServer::sendText(const QString &terminal, const QString &text)
{
    const QByteArray frame = LegacyProtocol::textFrame(text);
    auto *socket = m_sessions.value(terminal).control;
    if (frame.isEmpty() || !connected(socket) || socket->write(frame) != frame.size()) {
        return false;
    }

    emit logMessage(QStringLiteral("%1 · 已发指令：%2").arg(terminal, text));
    return true;
}

void TerminalServer::acceptConnections()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (!connected(socket)) {
            socket->deleteLater();
            continue;
        }

        const QString terminal = terminalAddress(socket->peerAddress());
        auto &session = m_sessions[terminal];
        if (!session.control) {
            session.control = socket;
            socket->setReadBufferSize(LegacyProtocol::MaxTextBytes);
            connect(socket, &QTcpSocket::readyRead, this,
                    [this, terminal, socket] { readText(terminal, socket); });
            connect(socket, &QTcpSocket::disconnected, this,
                    [this, terminal] { removeSession(terminal); });
            emit logMessage(QStringLiteral("%1 · 指令通道已连接").arg(terminal));
            emit terminalChanged(terminal, true, false);
            continue;
        }

        if (session.files) {
            emit logMessage(QStringLiteral("%1 · 已有两条连接，拒绝额外连接").arg(terminal));
            closeSocket(socket);
            continue;
        }

        session.files = socket;
        session.receiver = new UploadReceiver(socket, m_receiveDirectory);
        connect(session.receiver, &UploadReceiver::saved, this, [this, terminal](const QString &name) {
            emit logMessage(QStringLiteral("%1 · 已接收文件：%2").arg(terminal, name));
        });
        connect(session.receiver, &UploadReceiver::failed, this, [this, terminal](const QString &reason) {
            emit logMessage(QStringLiteral("%1 · 接收文件失败：%2").arg(terminal, reason));
        });
        connect(socket, &QTcpSocket::disconnected, this,
                [this, terminal, socket] { removeFileSocket(terminal, socket); });
        emit logMessage(QStringLiteral("%1 · 文件通道已连接").arg(terminal));
        emit terminalChanged(terminal, true, true);
    }
}

void TerminalServer::readText(const QString &terminal, QTcpSocket *socket)
{
    while (socket->canReadLine()) {
        QByteArray line = socket->readLine();
        line.chop(1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }

        const QString message = QString::fromUtf8(line);
        if (!message.isEmpty()) {
            const bool receipt = message == QStringLiteral("接受文件完成")
                || message == QStringLiteral("接收文件完成");
            emit logMessage(receipt
                ? QStringLiteral("%1 · 文件回执：%2").arg(terminal, message)
                : QStringLiteral("%1 · 收到消息：%2").arg(terminal, message));
        }
    }

    if (socket->bytesAvailable() >= LegacyProtocol::MaxTextBytes) {
        emit logMessage(QStringLiteral("%1 · 文本超过单行长度限制，已断开").arg(terminal));
        socket->abort();
    }
}

void TerminalServer::removeFileSocket(const QString &terminal, QTcpSocket *socket)
{
    auto session = m_sessions.find(terminal);
    if (session == m_sessions.end() || session->files != socket) {
        return;
    }

    const bool controlConnected = connected(session->control);
    const bool incomplete = session->receiver->hasPendingFile();
    session->files = nullptr;
    session->receiver = nullptr;
    closeSocket(socket);
    if (incomplete) {
        emit logMessage(QStringLiteral("%1 · 接收中断，已丢弃未完成的文件").arg(terminal));
    }
    emit logMessage(QStringLiteral("%1 · 文件通道已断开").arg(terminal));
    emit terminalChanged(terminal, controlConnected, false);
}

void TerminalServer::removeSession(const QString &terminal)
{
    const auto session = m_sessions.take(terminal);
    if (!session.control) {
        return;
    }

    if (session.receiver && session.receiver->hasPendingFile()) {
        emit logMessage(QStringLiteral("%1 · 接收中断，已丢弃未完成的文件").arg(terminal));
    }
    closeSocket(session.files);
    closeSocket(session.control);
    emit logMessage(QStringLiteral("%1 · 终端已断开").arg(terminal));
    emit terminalChanged(terminal, false, false);
}

void TerminalServer::closeSocket(QTcpSocket *socket)
{
    if (socket) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
}
