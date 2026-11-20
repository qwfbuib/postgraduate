#ifndef FILEQUEUE_H
#define FILEQUEUE_H

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

class FileQueue : public QObject
{
    Q_OBJECT

public:
    enum class Status { Sending, Sent, Failed };

    explicit FileQueue(QObject *parent = nullptr);

    bool start(const QString &terminal, QTcpSocket *socket, const QStringList &paths);
    bool isActive() const;

signals:
    void activeChanged(bool active);
    void fileChanged(int index, FileQueue::Status status, const QString &detail);
    void progressChanged(int filePercent, int batchPercent);
    void finished(const QString &terminal, bool success, const QString &detail);

private:
    void startFile();
    void pump();
    void bytesWritten(qint64 count);
    void updateProgress();
    void fail(const QString &reason, bool resetFileChannel);
    void finish(bool success, const QString &detail, bool resetFileChannel = false);

    QPointer<QTcpSocket> m_socket;
    QMetaObject::Connection m_writeConnection;
    QMetaObject::Connection m_disconnectConnection;
    QTimer m_spacing;
    QFile m_file;
    QStringList m_paths;
    QString m_terminal;
    QByteArray m_pending;
    qint64 m_headerSize = 0;
    qint64 m_payloadSize = 0;
    qint64 m_unreadPayload = 0;
    qint64 m_written = 0;
    int m_index = 0;
    int m_filePercent = -1;
    bool m_inFile = false;
    bool m_startedFrame = false;
};

#endif
