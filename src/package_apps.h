#pragma once
#include "apps.h"
QWidget *createPackageManager(SimSystem *, AppLauncher, QWidget * = nullptr);
QWidget *createHtop(SimSystem *, QWidget * = nullptr);
QWidget *createTextPager(SimSystem *, const QString &, QWidget * = nullptr);
