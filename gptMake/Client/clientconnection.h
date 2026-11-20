#ifndef CLIENTCONNECTION_H
#define CLIENTCONNECTION_H

#include "legacyprotocol.h"

#include <QObject>
#include <QTimer>

class QTcpSocket;
class UploadReceiver;

class ClientConnection final : public QObject
{
    Q_OBJECT

public:
    explicit ClientConnection(const QString &receiveDirectory, QObject *parent = nullptr);
    ~ClientConnection() override;

    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();

signals:
    void statusChanged(const QString &status);
    void logMessage(const QString &message);
    void displayRequested(LegacyProtocol::DisplayMode mode);
    void noticeReceived(const QString &notice);
    void fileReceived(const QString &name);

private:
    void openControl();
    void openFiles();
    void readCommands();
    void acknowledgeFile(const QString &name);
    void watchSocket(QTcpSocket *socket);
    void connectionLost(const QString &reason);
    void closeSockets();

    QString m_receiveDirectory;
    QString m_host;
    quint16 m_port = 0;
    bool m_reconnect = false;
    QTcpSocket *m_control = nullptr;
    QTcpSocket *m_files = nullptr;
    UploadReceiver *m_receiver = nullptr;
    QTimer m_retry;
    QTimer m_deadline;
};

#endif
