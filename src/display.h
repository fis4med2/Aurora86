#pragma once
#include <QSize>
#include <QWidget>
class SimSystem;
QSize displayPresetSize(const QString &preset);
QWidget *createDisplayPanel(SimSystem *system, QWidget *parent = nullptr);
