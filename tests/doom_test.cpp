#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

class DoomTest final : public QObject {
    Q_OBJECT
  private slots:
    void configSymlinkCannotWriteOutsideSaveDirectory() {
#ifndef Q_OS_UNIX
        QSKIP("POSIX symlink test");
#else
        QTemporaryDir state, outside;
        QVERIFY(state.isValid() && outside.isValid());
        const QString victim = outside.path() + "/profile";
        const QByteArray original("key_up\t\t119\n");
        QFile file(victim);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(original), original.size());
        file.close();
        QVERIFY(QFile::link(victim, state.path() + "/.doomrc"));
        QProcess engine;
        engine.setWorkingDirectory(state.path());
        engine.start(QCoreApplication::applicationDirPath() + "/aurora86-doom", {QStringLiteral(AURORA_DOOM_TEST_WAD), "doomu"});
        QVERIFY(engine.waitForStarted(5000));
        QVERIFY(engine.waitForReadyRead(5000));
        engine.readAllStandardOutput();
        engine.write("Q\0", 2);
        QVERIFY(engine.waitForFinished(3000));
        QCOMPARE(engine.exitCode(), 0);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
#endif
    }
    void nativeEngineRendersPausesSavesAndExits() {
        QTemporaryDir state;
        QVERIFY(state.isValid());
        QProcess engine;
        engine.setWorkingDirectory(state.path());
        engine.start(QCoreApplication::applicationDirPath() + "/aurora86-doom", {QStringLiteral(AURORA_DOOM_TEST_WAD), "doomu"});
        QVERIFY(engine.waitForStarted(5000));
        QByteArray buffer, image;
        int frames = 0, audio = 0;
        auto pump = [&](int milliseconds) {
            QElapsedTimer clock;
            clock.start();
            while (clock.elapsed() < milliseconds && engine.state() != QProcess::NotRunning) {
                engine.waitForReadyRead(qMin(50, milliseconds - int(clock.elapsed())));
                buffer += engine.readAllStandardOutput();
                while (buffer.size() >= 8) {
                    const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(buffer.constData() + 4));
                    if (size > 256000) return false;
                    if (buffer.size() < qsizetype(size + 8)) break;
                    if (buffer.first(4) == "FRAM" && size == 256000) { ++frames; image = buffer.mid(8, size); }
                    else if (buffer.first(4) == "AUDI" && size == 2048) ++audio;
                    else return false;
                    buffer.remove(0, size + 8);
                }
            }
            return true;
        };
        auto command = [&](char kind, int key) { const char bytes[] = {kind, char(key)}; engine.write(bytes, 2); engine.waitForBytesWritten(); };
        auto tap = [&](int key) { command('D', key); pump(100); command('U', key); pump(100); };
        QVERIFY2(pump(1200), engine.readAllStandardError().constData());
        QVERIFY2(frames >= 10, engine.readAllStandardError().constData());
        QVERIFY(audio >= 5);
        QCOMPARE(image.size(), 256000);
        QVERIFY(image != QByteArray(256000, '\0'));
        command('P', 1);
        QVERIFY(pump(150));
        const int pausedFrames = frames;
        QVERIFY(pump(200));
        QCOMPARE(frames, pausedFrames);
        command('P', 0);
        QVERIFY(pump(250));
        QVERIFY(frames > pausedFrames);
        // The real engine's save menu writes a persistent Doom savegame.
        tap(0xbc); // F2
        tap(13);   // first slot
        for (char key : QByteArray("aurora")) tap(key);
        tap(13);
        QVERIFY(pump(300));
        QVERIFY2(QFileInfo(state.path() + "/doomsav0.dsg").size() > 1000, engine.readAllStandardError().constData());
        command('Q', 0);
        engine.readAllStandardOutput();
        QVERIFY(engine.waitForFinished(3000));
        QCOMPARE(engine.exitStatus(), QProcess::NormalExit);
        QCOMPARE(engine.exitCode(), 0);
        QVERIFY(QFileInfo::exists(state.path() + "/.doomrc"));
    }
};

QTEST_GUILESS_MAIN(DoomTest)
#include "doom_test.moc"
