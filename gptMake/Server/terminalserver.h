#ifndef TERMINALSERVER_H
#define TERMINALSERVER_H

#include <QHash>
#include <QHostAddress>
#include <QTcpServer>

class QTcpSocket;
class UploadReceiver;

class TerminalServer : public QObject
{
    Q_OBJECT

public:
    explicit TerminalServer(const QString &receiveDirectory, QObject *parent = nullptr);
    ~TerminalServer() override;

    bool start(quint16 port, const QHostAddress &address = QHostAddress::Any);
    void stop();
    quint16 port() const;
    QString errorString() const;

    bool hasControl(const QString &terminal) const;
    QTcpSocket *fileSocket(const QString &terminal) const;
    bool sendText(const QString &terminal, const QString &text);

signals:
    void listeningChanged(bool listening);
    void terminalChanged(const QString &terminal, bool controlConnected, bool fileConnected);
    void logMessage(const QString &message);

private:
    struct Session {
        QTcpSocket *control = nullptr;
        QTcpSocket *files = nullptr;
        UploadReceiver *receiver = nullptr;
    };

    void acceptConnections();
    void readText(const QString &terminal, QTcpSocket *socket);
    void removeFileSocket(const QString &terminal, QTcpSocket *socket);
    void removeSession(const QString &terminal);
    void closeSocket(QTcpSocket *socket);

    QTcpServer m_server;
    QHash<QString, Session> m_sessions;
    QString m_receiveDirectory;
};

#endif
