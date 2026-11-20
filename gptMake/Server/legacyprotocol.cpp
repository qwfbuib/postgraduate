#include "legacyprotocol.h"

#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>
#include <limits>

namespace LegacyProtocol {

QString command(DisplayMode mode)
{
    switch (mode) {
    case DisplayMode::Video: return QStringLiteral("显示视频");
    case DisplayMode::Images: return QStringLiteral("显示图片");
    case DisplayMode::Notice: return QStringLiteral("显示通知");
    case DisplayMode::Cycle: return QStringLiteral("循环播放");
    }
    return {};
}

QByteArray textFrame(const QString &text)
{
    if (text.trimmed().isEmpty() || text.contains(QLatin1Char('\n'))
        || text.contains(QLatin1Char('\r'))) {
        return {};
    }

    QByteArray frame = text.toUtf8();
    frame.append('\n');
    return frame.size() <= MaxTextBytes ? frame : QByteArray{};
}

QByteArray fileHeader(const QString &name, qint64 payloadSize)
{
    QByteArray nameField;
    QDataStream nameStream(&nameField, QIODevice::WriteOnly);
    nameStream.setVersion(QDataStream::Qt_5_9);
    nameStream << name;

    const qint64 headerSize = SizeFields + nameField.size();
    if (name.isEmpty() || nameField.size() > MaxNameBytes || payloadSize < 0
        || payloadSize > std::numeric_limits<qint64>::max() - headerSize) {
        return {};
    }

    QByteArray header;
    QDataStream headerStream(&header, QIODevice::WriteOnly);
    headerStream.setVersion(QDataStream::Qt_5_9);
    headerStream << (headerSize + payloadSize) << qint64(nameField.size());
    header.append(nameField);
    return header;
}

bool readSizes(const QByteArray &fields, qint64 &nameSize, qint64 &payloadSize)
{
    QDataStream stream(fields);
    stream.setVersion(QDataStream::Qt_5_9);
    qint64 totalSize = 0;
    stream >> totalSize >> nameSize;

    if (stream.status() != QDataStream::Ok || nameSize < 6 || nameSize > MaxNameBytes
        || totalSize < SizeFields + nameSize) {
        return false;
    }

    payloadSize = totalSize - SizeFields - nameSize;
    return true;
}

QString readName(const QByteArray &field)
{
    QDataStream stream(field);
    stream.setVersion(QDataStream::Qt_5_9);

    quint32 byteCount = 0;
    stream >> byteCount;
    if (stream.status() != QDataStream::Ok || byteCount % 2 != 0
        || byteCount != quint32(field.size() - int(sizeof(byteCount)))) {
        return {};
    }

    stream.device()->seek(0);
    QString name;
    stream >> name;

    static const QRegularExpression invalidChars(QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
    static const QRegularExpression deviceName(
        QStringLiteral(R"(^(CON|PRN|AUX|NUL|CONIN\$|CONOUT\$|(?:COM|LPT)[0-9¹²³])\s*(?:\.|$))"),
        QRegularExpression::CaseInsensitiveOption);

    if (stream.status() != QDataStream::Ok || name.isEmpty() || name.size() > 255
        || name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))
        || invalidChars.match(name).hasMatch() || deviceName.match(name).hasMatch()) {
        return {};
    }

    return name;
}

}
