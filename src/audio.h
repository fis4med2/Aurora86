#pragma once
#include "system.h"
#include <QObject>
#if AURORA_WITH_MULTIMEDIA
#include <QAudioSink>
#include <QBuffer>
#endif
class Speaker : public QObject {
  public:
    explicit Speaker(SimSystem *system, QObject *parent = nullptr);
    void play(const QString &event);

  private:
    SimSystem *system_;
#if AURORA_WITH_MULTIMEDIA
    QAudioSink *sink_ = nullptr;
    QBuffer *buffer_ = nullptr;
#endif
};
