#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// A16ARC: the small archive format used by pack/unpack/arc.
//
// The container is deliberately simple: a text magic header followed by a
// zlib-compressed stream that lists name, size and bytes for every member.
// Both ends of the format live in this file, so archives are portable across
// AURORA versions as long as the format version stays 1.
struct ArchiveEntry {
    QString name;
    QByteArray data;
};

class Archive {
  public:
    static constexpr int MaximumMembers = 512;
    static constexpr int MaximumBytes = 512 * 1024;
    static constexpr const char *Magic = "A16ARC1\n";

    static QByteArray pack(const QList<ArchiveEntry> &entries, QString *error = nullptr);
    static QList<ArchiveEntry> unpack(const QByteArray &archive, QString *error = nullptr);
    static bool isArchive(const QByteArray &archive);
};
