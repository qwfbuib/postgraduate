#ifndef UPLOADRECEIVER_H
#define UPLOADRECEIVER_H

#include <QObject>
#include <QString>
#include <memory>

class QTcpSocket;
class QTemporaryFile;

class UploadReceiver : public QObject
{
    Q_OBJECT

public:
    UploadReceiver(QTcpSocket *socket, const QString &directory);
    ~UploadReceiver() override;

    bool hasPendingFile() const;

signals:
    void saved(const QString &name);
    void failed(const QString &reason);

private:
    void receive();
    bool openFile(qint64 nameSize, qint64 payloadSize);
    bool saveFile();
    void reject(const QString &reason);

    QTcpSocket *m_socket;
    QString m_directory;
    QString m_name;
    std::unique_ptr<QTemporaryFile> m_file;
    qint64 m_remaining = 0;
};

#endif
