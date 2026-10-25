#pragma once
#include "audio.h"
#include "system.h"
#include <QWidget>
#include <functional>
using AppLauncher = std::function<void(const QString &, const QString &)>;
QWidget *createTerminal(SimSystem *, Speaker *, AppLauncher, QWidget * = nullptr);
QWidget *createFiles(SimSystem *, Speaker *, AppLauncher, const QString &, QWidget * = nullptr);
QWidget *createEditor(SimSystem *, Speaker *, const QString &, QWidget * = nullptr);
QWidget *createCalculator(QWidget * = nullptr);
QWidget *createImageViewer(SimSystem *, const QString &, QWidget * = nullptr);
QWidget *createSettings(SimSystem *, QWidget * = nullptr);
QWidget *createMonitor(SimSystem *, QWidget * = nullptr);
QWidget *createBrowser(SimSystem *, Speaker *, const QString &, QWidget * = nullptr);
QWidget *createAbout(QWidget * = nullptr);
QWidget *createHandbook(AppLauncher, QWidget * = nullptr);
QWidget *createDoom(SimSystem *, QWidget * = nullptr);
QWidget *createGameLibrary(SimSystem *, AppLauncher, QWidget * = nullptr);
bool editorMayClose(QWidget *);
QIcon appIcon(const QString &, QColor ink = QColor("#69ddcf"));
