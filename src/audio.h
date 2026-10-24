#pragma once
#include "system.h"
#include <QAudioSink>
#include <QBuffer>
#include <QObject>
class Speaker : public QObject {
  public:
    explicit Speaker(SimSystem *system, QObject *parent = nullptr);
    void play(const QString &event);

  private:
    SimSystem *system_;
    QAudioSink *sink_ = nullptr;
    QBuffer *buffer_ = nullptr;
};
