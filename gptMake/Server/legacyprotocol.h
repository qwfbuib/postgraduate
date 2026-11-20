#ifndef LEGACYPROTOCOL_H
#define LEGACYPROTOCOL_H

#include <QByteArray>
#include <QString>

namespace LegacyProtocol {

enum class DisplayMode { Video, Images, Notice, Cycle };

constexpr qint64 SizeFields = 2 * sizeof(qint64);
constexpr qint64 MaxNameBytes = 4096;
constexpr qint64 MaxTextBytes = 64 * 1024;

QString command(DisplayMode mode);
QByteArray textFrame(const QString &text);
QByteArray fileHeader(const QString &name, qint64 payloadSize);
bool readSizes(const QByteArray &fields, qint64 &nameSize, qint64 &payloadSize);
QString readName(const QByteArray &field);

}

#endif
