#pragma once

#include "basic.h"
#include "system.h"

#include <QMap>
#include <QPair>
#include <QStringList>

class Shell : public QObject {
    Q_OBJECT
public:
    explicit Shell(SimSystem *system, QObject *parent = nullptr);
    QString execute(const QString &line);
    QString cwd() const;
    QString prompt() const;
    bool inBasic() const { return m_inBasic; }
    int lastStatus() const { return m_status; }
    QStringList history() const { return m_history; }
signals:
    void launchRequested(const QString &app, const QString &path);
    void clearRequested();
    void shutdownRequested();
    void soundRequested(const QString &event);
private:
    SimSystem *m_system;
    QString m_cwd = "/home/guest";
    QStringList m_history;
    int m_status = 0;
    int m_commandStatus = -1;
    bool m_inBasic = false;
    bool m_pipedOutput = false;
    Basic m_basic;

    QString runSimple(const QString &line, const QString &standardInput = {});
    QString executeBasic(const QString &line);
    QString expandVariables(const QString &text) const;
    QString applyAlias(const QString &line) const;
    QStringList expandGlob(const QString &token) const;
    QString envValue(const QString &name) const;
    QMap<QString, QString> aliases() const;
    void storeAliases(const QMap<QString, QString> &map);
    void configureBasic();
};
