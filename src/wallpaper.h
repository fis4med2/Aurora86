#pragma once
#include <QImage>
#include <QString>
#include <QWidget>
class SimSystem;

QWidget *createWallpaperPanel(SimSystem *system, QWidget *parent = nullptr);
bool importWallpaper(SimSystem *system, const QString &localPath, QString *error = nullptr);
QImage readWallpaper(SimSystem *system);
QImage applyWallpaperRetro(QImage image);
