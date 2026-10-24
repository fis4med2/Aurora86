#pragma once

#include "system.h"

class Shell : public QObject {
    Q_OBJECT
public:
    explicit Shell(SimSystem *system, QObject *parent = nullptr);
    QString execute(const QString &line);
    QString cwd() const;
    QString prompt() const;
signals:
    void launchRequested(const QString &app, const QString &path);
    void clearRequested();
    void shutdownRequested();
private:
    SimSystem *m_system;
    QString m_cwd = "/home/guest";
};
