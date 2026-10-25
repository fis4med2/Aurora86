#pragma once
#include <QString>

struct DoomWadInfo {
    QString mode;
    QString error;
    bool valid() const { return !mode.isEmpty() && error.isEmpty(); }
};

DoomWadInfo inspectDoomWad(const QString &path);
