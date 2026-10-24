#pragma once
#include "system.h"
#include <QList>
#include <QStringList>

struct AuroraPackage {
    QString name;
    QString version;
    QString description;
    QStringList dependencies;
    int installedBytes;
};

class PackageManager final {
  public:
    explicit PackageManager(SimSystem *system);
    QList<AuroraPackage> catalog() const;
    bool isInstalled(const QString &name) const;
    QStringList installed() const;
    QString install(const QStringList &names);
    QString remove(const QStringList &names);
    QString search(const QString &query) const;
    QString info(const QString &name) const;
    QString run(const QString &command, const QStringList &args, const QString &cwd);
    bool ownsCommand(const QString &command) const;

  private:
    SimSystem *system_;
};
