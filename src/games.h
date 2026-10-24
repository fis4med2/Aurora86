#pragma once
#include <QWidget>
class SimSystem;

QWidget *createCircuitGame(SimSystem *system, QWidget *parent = nullptr);
QWidget *createMeteorGame(SimSystem *system, QWidget *parent = nullptr);
