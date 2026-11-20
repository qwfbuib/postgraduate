#include "uploadreceiver.h"

#include "legacyprotocol.h"

#include <QDir>
#include <QFileInfo>
#include <QTcpSocket>
#include <QTemporaryFile>

namespace {
constexpr qint64 ChunkSize = 64 * 1024;
}

UploadReceiver::UploadReceiver(QTcpSocket *socket, const QString &directory)
    : QObject(socket), m_socket(socket), m_directory(directory)
{
    m_socket->setReadBufferSize(ChunkSize * 4);
    connect(m_socket, &QTcpSocket::readyRead, this, &UploadReceiver::receive);
}

UploadReceiver::~UploadReceiver() = default;

bool UploadReceiver::hasPendingFile() const
{
    return m_file || m_socket->bytesAvailable() != 0;
}

void UploadReceiver::receive()
{
    while (m_socket->state() == QAbstractSocket::ConnectedState) {
        if (!m_file) {
            if (m_socket->bytesAvailable() < LegacyProtocol::SizeFields) {
                return;
            }

            qint64 nameSize = 0;
            qint64 payloadSize = 0;
            if (!LegacyProtocol::readSizes(m_socket->peek(LegacyProtocol::SizeFields),
                                          nameSize, payloadSize)) {
                reject(QStringLiteral("文件头无效"));
                return;
            }

            if (m_socket->bytesAvailable() < LegacyProtocol::SizeFields + nameSize) {
                return;
            }
            if (!openFile(nameSize, payloadSize)) {
                return;
            }
        }

        while (m_remaining > 0 && m_socket->bytesAvailable() > 0) {
            const QByteArray chunk = m_socket->read(qMin(ChunkSize, m_remaining));
            if (chunk.isEmpty() || m_file->write(chunk) != chunk.size()) {
                reject(QStringLiteral("写入 %1 失败").arg(m_name));
                return;
            }
            m_remaining -= chunk.size();
        }

        if (m_remaining > 0 || !saveFile()) {
            return;
        }
    }
}

bool UploadReceiver::openFile(qint64 nameSize, qint64 payloadSize)
{
    m_socket->read(LegacyProtocol::SizeFields);
    m_name = LegacyProtocol::readName(m_socket->read(nameSize));
    if (m_name.isEmpty()) {
        reject(QStringLiteral("文件名无效"));
        return false;
    }
    if (!QDir().mkpath(m_directory)) {
        reject(QStringLiteral("无法创建接收目录"));
        return false;
    }

    m_file = std::make_unique<QTemporaryFile>(
        QDir(m_directory).filePath(QStringLiteral(".upload-XXXXXX")));
    if (!m_file->open()) {
        reject(QStringLiteral("无法保存 %1").arg(m_name));
        return false;
    }

    m_remaining = payloadSize;
    return true;
}

bool UploadReceiver::saveFile()
{
    if (!m_file->flush()) {
        reject(QStringLiteral("保存 %1 失败：%2").arg(m_name, m_file->errorString()));
        return false;
    }

    const QDir folder(m_directory);
    const QFileInfo original(m_name);
    const QString extension = original.suffix().isEmpty()
        ? QString{} : QStringLiteral(".") + original.suffix();
    QString savedName = m_name;
    int suffix = 2;
    while (folder.exists(savedName)) {
        savedName = QStringLiteral("%1 (%2)%3")
                        .arg(original.completeBaseName()).arg(suffix++).arg(extension);
    }

    if (!m_file->rename(folder.filePath(savedName))) {
        reject(QStringLiteral("保存 %1 失败：%2").arg(m_name, m_file->errorString()));
        return false;
    }
    m_file->setAutoRemove(false);
    m_file.reset();
    m_name.clear();
    emit saved(savedName);
    return true;
}

void UploadReceiver::reject(const QString &reason)
{
    m_file.reset();
    m_remaining = 0;
    emit failed(reason);
    m_socket->abort();
}
