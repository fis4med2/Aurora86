#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
class SimSystem;
struct AuroraMod {
    QString name, version, description;
    QByteArray source;
};
QList<AuroraMod> availableMods(SimSystem *system, QString *error = nullptr);
QList<AuroraMod> installedMods(SimSystem *system, QString *error = nullptr);
QString installMod(SimSystem *system, const QString &name);
QString removeMod(SimSystem *system, const QString &name);
bool isModInstalled(SimSystem *system, const QString &name);
QByteArray installedModSource(SimSystem *system, const QString &name);

bool isModNameReserved(SimSystem *system, const QString &name);
