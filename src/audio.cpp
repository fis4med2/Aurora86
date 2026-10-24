#include "audio.h"
#include <QGuiApplication>
#include <QMediaDevices>
#include <QtMath>
Speaker::Speaker(SimSystem *s, QObject *p) : QObject(p), system_(s) {
    if (QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal" ||
        qEnvironmentVariableIsSet("AURORA_NO_AUDIO"))
        return;
    QAudioFormat format;
    format.setSampleRate(22050);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    const auto device = QMediaDevices::defaultAudioOutput();
    if (!device.isNull() && device.isFormatSupported(format)) {
        sink_ = new QAudioSink(device, format, this);
        sink_->setVolume(.15);
        buffer_ = new QBuffer(this);
    }
}
void Speaker::play(const QString &event) {
    if (!sink_ || !system_->setting("sound", true).toBool())
        return;
    if (event == "key" && !system_->setting("keySound", true).toBool())
        return;
    if (event == "key" && sink_->state() == QAudio::ActiveState)
        return;
    QList<double> notes = event == "boot"    ? QList<double>{330, 440, 660, 880}
                          : event == "error" ? QList<double>{130, 98}
                          : event == "key"   ? QList<double>{950}
                                             : QList<double>{440, 660};
    if (event.startsWith("echo")) {
        const int index = event.mid(4).toInt() - 1;
        const double frequencies[] = {262, 330, 392, 523};
        if (index >= 0 && index < 4)
            notes = {frequencies[index]};
    }
    const int duration = event.startsWith("echo") ? 150 : event == "key" ? 8 : event == "boot" ? 100 : 55;
    QByteArray pcm;
    for (double hz : notes)
        for (int n = 0; n < 22050 * duration / 1000; ++n) {
            const double fade = qMin(1., (22050. * duration / 1000. - n) / 100.);
            qint16 sample = static_cast<qint16>((qSin(2 * M_PI * hz * n / 22050) > 0 ? 1 : -1) * 4200 * fade);
            pcm.append(char(sample & 255));
            pcm.append(char((sample >> 8) & 255));
        }
    sink_->stop();
    buffer_->close();
    buffer_->setData(pcm);
    buffer_->open(QIODevice::ReadOnly);
    sink_->start(buffer_);
}
