#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

struct VEntry {
    QString path;
    bool directory = false;
    QByteArray data;
    int permissions = 0644;
};

struct SimProcess {
    int pid;
    QString name;
    int memoryKiB;
};

class SimSystem : public QObject {
    Q_OBJECT
  public:
    explicit SimSystem(const QString &stateDirectory, QObject *parent = nullptr);
    bool load();
    bool save();
    QString lastError() const;
    QString normalize(const QString &path, const QString &cwd = "/home/guest") const;
    QList<VEntry> list(const QString &path) const;
    bool exists(const QString &path) const;
    bool isDirectory(const QString &path) const;
    QByteArray read(const QString &path) const;
    bool write(const QString &path, const QByteArray &data);
    bool mkdir(const QString &path);
    bool remove(const QString &path, bool recursive = false);
    bool move(const QString &source, const QString &destination);
    bool copy(const QString &source, const QString &destination);
    bool chmod(const QString &path, int permissions);
    int usedBytes() const;
    static constexpr int DiskBytes = 2 * 1024 * 1024;
    static constexpr int MemoryKiB = 4096;
    int startProcess(const QString &name, int memoryKiB);
    bool stopProcess(int pid);
    QList<SimProcess> processes() const;
    int usedMemoryKiB() const;
    qint64 uptimeSeconds() const;
    QVariant setting(const QString &key, const QVariant &fallback = {}) const;
    void setSetting(const QString &key, const QVariant &value);
    bool setSettings(const QVariantMap &values);
  signals:
    void changed();
    void processesChanged();
    void settingsChanged();

  private:
    QElapsedTimer m_uptime;
    QString m_directory;
    QMap<QString, VEntry> m_entries;
    QVariantMap m_settings;
    QList<SimProcess> m_processes;
    mutable QString m_error;
    bool m_blocked = false;
    int m_nextPid = 3;
    void seed();
    bool fail(const QString &message) const;
    bool accessible(const QString &path) const;
    bool writableParent(const QString &path) const;
    bool protectedPath(const QString &path) const;
    bool commit(const QMap<QString, VEntry> &previous);
    bool virtualPath(const QString &path) const;
    QByteArray virtualFile(const QString &path) const;
};
