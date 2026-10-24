#include "archive.h"

#include <QDataStream>
#include <QIODevice>

namespace {
constexpr quint32 FormatVersion = 1;

void fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
}
} // namespace

bool Archive::isArchive(const QByteArray &archive)
{
    return archive.startsWith(Magic);
}

QByteArray Archive::pack(const QList<ArchiveEntry> &entries, QString *error)
{
    if (error)
        error->clear();
    if (entries.isEmpty()) {
        fail(error, "Nothing to pack.");
        return {};
    }
    if (entries.size() > MaximumMembers) {
        fail(error, QString("Too many members (limit %1).").arg(MaximumMembers));
        return {};
    }
    qint64 total = 0;
    for (const auto &entry : entries) {
        if (entry.name.isEmpty() || entry.name.contains('/') || entry.name.contains('\\')) {
            fail(error, "Member names must be plain file names: " + entry.name);
            return {};
        }
        total += entry.data.size();
    }
    if (total > MaximumBytes) {
        fail(error, QString("Archive would exceed %1 KiB.").arg(MaximumBytes / 1024));
        return {};
    }
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << FormatVersion << quint32(entries.size());
    for (const auto &entry : entries) {
        stream << entry.name << qint32(entry.data.size());
        stream.writeRawData(entry.data.constData(), entry.data.size());
    }
    if (stream.status() != QDataStream::Ok) {
        fail(error, "Could not build the archive.");
        return {};
    }
    QByteArray result(Magic);
    result += qCompress(payload, 9);
    return result;
}

QList<ArchiveEntry> Archive::unpack(const QByteArray &archive, QString *error)
{
    if (error)
        error->clear();
    QList<ArchiveEntry> entries;
    if (!isArchive(archive)) {
        fail(error, "Not an A16ARC archive.");
        return entries;
    }
    const QByteArray compressed = archive.mid(int(qstrlen(Magic)));
    if (compressed.isEmpty()) {
        fail(error, "Archive is empty.");
        return entries;
    }
    const QByteArray payload = qUncompress(compressed);
    if (payload.isEmpty()) {
        fail(error, "Archive is damaged.");
        return entries;
    }
    QDataStream stream(payload);
    stream.setVersion(QDataStream::Qt_6_0);
    quint32 version = 0, count = 0;
    stream >> version >> count;
    if (stream.status() != QDataStream::Ok || version != FormatVersion) {
        fail(error, "Unsupported archive version.");
        return entries;
    }
    if (count > quint32(MaximumMembers)) {
        fail(error, "Archive lists too many members.");
        return entries;
    }
    qint64 total = 0;
    for (quint32 i = 0; i < count; ++i) {
        QString name;
        qint32 size = 0;
        stream >> name >> size;
        if (stream.status() != QDataStream::Ok || size < 0 || size > MaximumBytes) {
            fail(error, "Archive is damaged.");
            return {};
        }
        if (name.isEmpty() || name.contains('/') || name.contains('\\')) {
            fail(error, "Archive contains an unsafe member name.");
            return {};
        }
        total += size;
        if (total > MaximumBytes) {
            fail(error, "Archive expands beyond the safe limit.");
            return {};
        }
        QByteArray data(size, '\0');
        if (size > 0 && stream.readRawData(data.data(), size) != size) {
            fail(error, "Archive is truncated.");
            return {};
        }
        entries.append({name, data});
    }
    return entries;
}
