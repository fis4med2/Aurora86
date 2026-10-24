#pragma once
#include <QWidget>
class SimSystem;
QWidget *createPrismGame(SimSystem *system, QWidget *parent = nullptr);
QWidget *createGardenGame(SimSystem *system, QWidget *parent = nullptr);
QWidget *createEchoGame(SimSystem *system, QWidget *parent = nullptr);
