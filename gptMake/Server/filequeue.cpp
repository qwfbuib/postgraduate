#include "filequeue.h"

#include "legacyprotocol.h"

#include <QFileInfo>

namespace {
constexpr qint64 BufferSize = 64 * 1024;
constexpr qint64 LegacyLargeFile = 10000000;
constexpr int LegacyShortDelay = 100;
constexpr int LegacyLongDelay = 5000;
}

FileQueue::FileQueue(QObject *parent) : QObject(parent)
{
    m_spacing.setSingleShot(true);
    m_spacing.setTimerType(Qt::PreciseTimer);
    connect(&m_spacing, &QTimer::timeout, this, &FileQueue::startFile);
}

bool FileQueue::start(const QString &terminal, QTcpSocket *socket, const QStringList &paths)
{
    if (isActive() || !socket || socket->state() != QAbstractSocket::ConnectedState
        || paths.isEmpty()) {
        return false;
    }

    m_terminal = terminal;
    m_socket = socket;
    m_paths = paths;
    m_index = 0;
    m_writeConnection = connect(socket, &QTcpSocket::bytesWritten, this, &FileQueue::bytesWritten);
    m_disconnectConnection = connect(socket, &QTcpSocket::disconnected, this, [this] {
        fail(QStringLiteral("文件通道已断开"), false);
    });

    emit activeChanged(true);
    startFile();
    return true;
}

bool FileQueue::isActive() const
{
    return !m_paths.isEmpty();
}

void FileQueue::startFile()
{
    m_inFile = true;
    m_startedFrame = false;
    m_filePercent = -1;
    m_written = 0;

    const QFileInfo info(m_paths.at(m_index));
    m_file.setFileName(info.filePath());
    if (!info.isFile() || !m_file.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("无法打开文件：%1").arg(info.fileName()), false);
        return;
    }

    m_payloadSize = m_file.size();
    m_pending = LegacyProtocol::fileHeader(info.fileName(), m_payloadSize);
    if (m_pending.isEmpty()) {
        fail(QStringLiteral("文件名或文件大小超出协议范围：%1").arg(info.fileName()), false);
        return;
    }

    m_headerSize = m_pending.size();
    m_unreadPayload = m_payloadSize;
    emit fileChanged(m_index, Status::Sending, info.fileName());
    updateProgress();
    pump();
}

void FileQueue::pump()
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) {
        fail(QStringLiteral("文件通道已断开"), false);
        return;
    }

    while (m_socket->bytesToWrite() < BufferSize) {
        const qint64 capacity = BufferSize - m_socket->bytesToWrite();

        if (m_pending.isEmpty()) {
            if (m_unreadPayload == 0) {
                return;
            }
            const qint64 chunkSize = qMin(capacity, m_unreadPayload);
            m_pending = m_file.read(chunkSize);
            if (m_pending.size() != chunkSize) {
                fail(QStringLiteral("无法继续读取文件：%1").arg(QFileInfo(m_file).fileName()), true);
                return;
            }
            m_unreadPayload -= m_pending.size();
        }

        const qint64 accepted = m_socket->write(m_pending.constData(), qMin(capacity, qint64(m_pending.size())));
        if (accepted < 0 || (accepted == 0 && m_socket->bytesToWrite() == 0)) {
            fail(QStringLiteral("发送失败：%1").arg(m_socket->errorString()), true);
            return;
        }
        if (accepted == 0) {
            return;
        }

        m_startedFrame = true;
        m_pending.remove(0, int(accepted));
    }
}

void FileQueue::bytesWritten(qint64 count)
{
    if (!m_inFile) {
        return;
    }

    m_written += count;
    updateProgress();
    if (m_written < m_headerSize + m_payloadSize) {
        pump();
        return;
    }

    emit fileChanged(m_index, Status::Sent, QFileInfo(m_file).fileName());
    m_file.close();
    m_inFile = false;
    m_startedFrame = false;

    ++m_index;
    if (m_index == m_paths.size()) {
        finish(true, QStringLiteral("素材批次发送完成"));
        return;
    }

    // Legacy receivers expect time between frames because they read all buffered bytes as one file.
    const bool nextFileIsLarge = QFileInfo(m_paths.at(m_index)).size() > LegacyLargeFile;
    m_spacing.start(nextFileIsLarge ? LegacyLongDelay : LegacyShortDelay);
}

void FileQueue::updateProgress()
{
    const qint64 wireSize = m_headerSize + m_payloadSize;
    const int percent = m_written >= wireSize ? 100
        : m_payloadSize == 0 ? 0
        : qMin(99, int(100.0 * qMax(qint64(0), m_written - m_headerSize) / m_payloadSize));

    if (percent != m_filePercent) {
        m_filePercent = percent;
        const int batchPercent = int((qint64(m_index) * 100 + percent) / m_paths.size());
        emit progressChanged(percent, batchPercent);
    }
}

void FileQueue::fail(const QString &reason, bool resetFileChannel)
{
    if (!isActive()) {
        return;
    }

    if (m_inFile) {
        emit fileChanged(m_index, Status::Failed, reason);
    }
    finish(false, reason, resetFileChannel && m_startedFrame);
}

void FileQueue::finish(bool success, const QString &detail, bool resetFileChannel)
{
    m_spacing.stop();
    disconnect(m_writeConnection);
    disconnect(m_disconnectConnection);
    m_file.close();
    m_pending.clear();
    m_inFile = false;
    m_startedFrame = false;

    const auto socket = m_socket;
    m_socket.clear();
    const QString terminal = m_terminal;
    m_terminal.clear();
    m_paths.clear();
    if (resetFileChannel && socket) {
        socket->abort();
    }
    emit activeChanged(false);
    emit finished(terminal, success, detail);
}
