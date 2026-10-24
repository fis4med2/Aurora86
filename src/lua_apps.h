#pragma once
#include "apps.h"
QWidget *createLuaProgram(SimSystem *, const QString &path, QWidget *parent = nullptr);
QWidget *createLuaStudio(SimSystem *, AppLauncher, QWidget *parent = nullptr);
