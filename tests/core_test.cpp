#include "shell.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

class CoreTest : public QObject {
    Q_OBJECT
  private slots:
    void filesAndPersistence() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SimSystem system(dir.path());
        QVERIFY(system.load());
        QCOMPARE(system.normalize("../../../../etc/release"), QString("/etc/release"));
        QVERIFY(system.mkdir("docs/projeto"));
        QVERIFY(system.write("docs/projeto/notas.txt", "uma ideia\n"));
        QVERIFY(system.move("docs/projeto", "docs/trabalho"));
        QVERIFY(!system.exists("docs/projeto/notas.txt"));
        QCOMPARE(system.read("docs/trabalho/notas.txt"), QByteArray("uma ideia\n"));
        QVERIFY(!system.move("docs/trabalho", "docs/trabalho/subpasta"));
        QVERIFY(system.copy("docs/trabalho/notas.txt", "docs/copia.txt"));
        system.setSetting("theme", "amber");
        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.read("docs/copia.txt"), QByteArray("uma ideia\n"));
        QCOMPARE(reopened.setting("theme").toString(), QString("amber"));
        QVERIFY(!reopened.remove("docs/trabalho"));
        QVERIFY(reopened.remove("docs/trabalho", true));
        QVERIFY(!reopened.exists("docs/trabalho/notas.txt"));
    }

    void permissionsAndBoundaries() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        QVERIFY(!system.write("/etc/release", "invadido"));
        QVERIFY(!system.write("../../../../etc/test", "invadido"));
        QVERIFY(!system.remove("/", true));
        QVERIFY(!system.chmod("/sys/hardware", 0777));
        QVERIFY(system.mkdir("privado"));
        QVERIFY(system.write("privado/segredo", "semente"));
        QVERIFY(system.chmod("privado", 0600));
        QVERIFY(system.read("privado/segredo").isEmpty());
        QVERIFY(!system.lastError().isEmpty());
        QVERIFY(!system.write("privado/outro", "bloqueado"));
        QVERIFY(!system.exists("privado/segredo"));
        QVERIFY(system.chmod("privado", 0755));
        QVERIFY(system.chmod("privado/segredo", 0400));
        QVERIFY(!system.write("privado/segredo", "novo"));
        QCOMPARE(system.read("privado/segredo"), QByteArray("semente"));
        QVERIFY(system.chmod("privado/segredo", 0200));
        QVERIFY(system.read("privado/segredo").isEmpty());
        QVERIFY(!system.lastError().isEmpty());
        QVERIFY(!system.write(QString("bad") + QChar::Null + "name", "x"));
    }

    void diskAndMemoryLimits() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        const int before = system.usedBytes();
        QVERIFY(!system.write("gigante", QByteArray(SimSystem::DiskBytes, 'x')));
        QCOMPARE(system.usedBytes(), before);
        const int available =
            SimSystem::DiskBytes - before - 64 - QString("/home/guest/cheio").toUtf8().size();
        QVERIFY(system.write("cheio", QByteArray(available, 'x')));
        QCOMPARE(system.usedBytes(), SimSystem::DiskBytes);
        QVERIFY(!system.mkdir("mais"));
        QVERIFY(system.remove("cheio"));
        const int pid = system.startProcess("teste", SimSystem::MemoryKiB - 512);
        QVERIFY(pid > 2);
        QCOMPARE(system.usedMemoryKiB(), SimSystem::MemoryKiB);
        QCOMPARE(system.startProcess("excesso", 1), -1);
        QVERIFY(!system.stopProcess(1));
        QVERIFY(system.stopProcess(pid));
        QCOMPARE(system.usedMemoryKiB(), 512);
    }

    void procTracksSimulatedProcesses() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        const int used = system.usedBytes();
        QVERIFY(system.isDirectory("/proc"));
        const auto root = system.list("/");
        QVERIFY(std::any_of(root.cbegin(), root.cend(), [](const VEntry &entry) {
            return entry.path == "/proc" && entry.directory && entry.permissions == 0555;
        }));
        QVERIFY(system.read("/proc/cpuinfo").contains("A16"));
        QVERIFY(system.read("/proc/version").contains("AURORA/86 1.5"));
        QVERIFY(system.read("/proc/loadavg").contains("0.00"));
        QCOMPARE(system.read("/proc/meminfo"),
                 QByteArray("MemTotal: 4096 kB\nMemFree: 3584 kB\nMemUsed: 512 kB\n"));
        const double before = system.read("/proc/uptime").trimmed().toDouble();
        QTest::qWait(30);
        QVERIFY(system.read("/proc/uptime").trimmed().toDouble() > before);
        const int pid = system.startProcess("writer", 128);
        QVERIFY(pid > 2);
        const QString path = "/proc/" + QString::number(pid);
        QVERIFY(system.exists(path + "/cmdline"));
        QCOMPARE(system.read(path + "/cmdline"), QByteArray("writer\0", 7));
        QVERIFY(system.read(path + "/status").contains("VmSize:\t128 kB"));
        QCOMPARE(system.list(path).size(), 2);
        QCOMPARE(system.list("/proc").size(), 8);
        QVERIFY(system.read("/proc/meminfo").contains("MemFree: 3456 kB"));
        QVERIFY(!system.exists("/proc/03"));
        QVERIFY(!system.exists("/proc/self"));
        QCOMPARE(system.usedBytes(), used);
        QVERIFY(system.stopProcess(pid));
        QVERIFY(!system.exists(path));
        QVERIFY(system.read(path + "/cmdline").isEmpty());
        QVERIFY(!system.lastError().isEmpty());
        QVERIFY(system.list(path).isEmpty());
        QCOMPARE(system.read("/proc/meminfo"),
                 QByteArray("MemTotal: 4096 kB\nMemFree: 3584 kB\nMemUsed: 512 kB\n"));
    }

    void procIsReadOnlyAndVolatile() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        const int used = system.usedBytes();
        QVERIFY(!system.write("/proc/cpuinfo", "replacement"));
        QVERIFY(!system.write("/proc/new", "new"));
        QVERIFY(!system.mkdir("/proc/new"));
        QVERIFY(!system.remove("/proc", true));
        QVERIFY(!system.remove("/proc/cpuinfo"));
        QVERIFY(!system.move("/proc/cpuinfo", "cpuinfo"));
        QVERIFY(!system.chmod("/proc/cpuinfo", 0777));
        QCOMPARE(system.usedBytes(), used);
        QVERIFY(system.copy("/proc/cpuinfo", "/home/guest"));
        QCOMPARE(system.read("cpuinfo"), system.read("/proc/cpuinfo"));
        QVERIFY(!system.move("cpuinfo", "/proc"));
        QVERIFY(!system.copy("cpuinfo", "/proc"));
        const int pid = system.startProcess("temporary", 64);
        QVERIFY(pid > 2);
        QVERIFY(system.copy("/proc/meminfo", "memory-snapshot.txt"));
        QVERIFY(system.save());
        QFile saved(dir.filePath("vfs.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto entries = QJsonDocument::fromJson(saved.readAll()).object().value("entries").toArray();
        for (const auto &entry : entries)
            QVERIFY(!entry.toObject().value("path").toString().startsWith("/proc"));
        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        QVERIFY(reopened.exists("/proc/cpuinfo"));
        QVERIFY(!reopened.exists("/proc/" + QString::number(pid)));
        QCOMPARE(reopened.read("cpuinfo"), system.read("/proc/cpuinfo"));
        QVERIFY(reopened.read("memory-snapshot.txt").contains("MemUsed: 576 kB"));
        QVERIFY(reopened.read("/proc/meminfo").contains("MemUsed: 512 kB"));
        QCOMPARE(reopened.usedMemoryKiB(), 512);
    }

    void savedStateCannotSpoofProc() {
        QTemporaryDir dir;
        SimSystem initial(dir.path());
        QVERIFY(initial.load());
        QFile saved(dir.filePath("vfs.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(saved.readAll()).object();
        saved.close();
        auto entries = root.value("entries").toArray();
        entries.append(QJsonObject{{"path", "/proc"}, {"directory", true},
                                   {"data", ""}, {"permissions", 0555}});
        root["entries"] = entries;
        const auto damaged = QJsonDocument(root).toJson();
        QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
        saved.write(damaged);
        saved.close();
        SimSystem restored(dir.path());
        QVERIFY(!restored.load());
        QVERIFY(restored.read("/proc/cpuinfo").contains("A16"));
        QVERIFY(!restored.save());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), damaged);
    }

    void corruptStateIsPreserved() {
        QTemporaryDir dir;
        QFile saved(dir.filePath("vfs.json"));
        QVERIFY(saved.open(QIODevice::WriteOnly));
        const QByteArray damaged("{broken state");
        saved.write(damaged);
        saved.close();
        SimSystem system(dir.path());
        QVERIFY(!system.load());
        QVERIFY(!system.write("nao-salvar", "novo"));
        QVERIFY(!system.exists("nao-salvar"));
        system.setSetting("theme", "green");
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), damaged);
    }

    void invalidTreeIsPreserved() {
        QTemporaryDir dir;
        SimSystem initial(dir.path());
        QVERIFY(initial.load());
        QFile saved(dir.filePath("vfs.json"));
        QVERIFY(saved.open(QIODevice::ReadOnly));
        auto root = QJsonDocument::fromJson(saved.readAll()).object();
        saved.close();
        auto entries = root.value("entries").toArray();
        entries.append(QJsonObject{
            {"path", "/home/guest/missing/file"}, {"directory", false}, {"data", ""}, {"permissions", 0644}});
        root["entries"] = entries;
        const auto damaged = QJsonDocument(root).toJson();
        QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
        saved.write(damaged);
        saved.close();
        SimSystem restored(dir.path());
        QVERIFY(!restored.load());
        QVERIFY(!restored.save());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), damaged);
    }

    void settingsLimitAndClock() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        system.setSetting("oversized", QString(65 * 1024, 'x'));
        QVERIFY(!system.lastError().isEmpty());
        QVERIFY(!system.setting("oversized").isValid());
        Shell shell(&system);
        const auto defaultDate = QDateTime::fromString(shell.execute("date"), "dd/MM/yyyy HH:mm:ss");
        QCOMPARE(defaultDate.date().year(), 1986);
        QCOMPARE(defaultDate.date().month(), QDate::currentDate().month());
        const auto offset =
            QDateTime::currentDateTime().secsTo(QDateTime(QDate(1987, 3, 5), QTime(12, 34, 56)));
        system.setSetting("clockOffsetSeconds", offset);
        const auto adjusted = QDateTime::fromString(shell.execute("date"), "dd/MM/yyyy HH:mm:ss");
        QCOMPARE(adjusted.date(), QDate(1987, 3, 5));
        QVERIFY(qAbs(adjusted.time().secsTo(QTime(12, 34, 56))) <= 2);
        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.setting("clockOffsetSeconds").toLongLong(), offset);
    }

    void shellCommands() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QSignalSpy launch(&shell, &Shell::launchRequested);
        QCOMPARE(shell.execute("pwd"), QString("/home/guest"));
        QCOMPARE(shell.execute("echo 'primeira linha' > 'arquivo novo.txt'"), QString());
        QCOMPARE(shell.execute("echo segunda >> 'arquivo novo.txt'"), QString());
        QCOMPARE(shell.execute("cat 'arquivo novo.txt'"), QString("primeira linha\nsegunda\n"));
        QVERIFY(shell.execute("echo 'incompleto").startsWith("error:"));
        QCOMPARE(shell.execute("echo '>'"), QString(">"));
        QCOMPARE(shell.execute("echo '>' > maior.txt"), QString());
        QCOMPARE(shell.execute("cat maior.txt"), QString(">\n"));
        QVERIFY(shell.execute("rm /etc/release").startsWith("error:"));
        QCOMPARE(shell.execute("mkdir teste"), QString());
        QCOMPARE(shell.execute("mv 'arquivo novo.txt' teste"), QString());
        QCOMPARE(shell.execute("cd teste"), QString());
        QCOMPARE(shell.cwd(), QString("/home/guest/teste"));
        QCOMPARE(shell.execute("cat 'arquivo novo.txt'"), QString("primeira linha\nsegunda\n"));
        QVERIFY(shell.execute("ls -l").contains("-rw-r--r--"));
        QVERIFY(shell.execute("stat 'arquivo novo.txt'").contains("guest"));
        QCOMPARE(shell.execute("edit 'arquivo novo.txt'"), QString());
        QCOMPARE(launch.size(), 1);
        QCOMPARE(launch.first().at(0).toString(), QString("edit"));
        QCOMPARE(launch.first().at(1).toString(), QString("/home/guest/teste/arquivo novo.txt"));
        QVERIFY(shell.execute("$(touch /tmp/escape)").startsWith("error:"));
        QVERIFY(shell.execute("free").contains("4096"));
    }
    void settingsBatchIsAtomic() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QVERIFY(system.setSettings({{"username", "before"}, {"displayName", "Before"}}));
        QSignalSpy changed(&system, &SimSystem::settingsChanged);
        QVERIFY(!system.setSettings({{"username", "after"}, {"oversize", QString(70000, 'x')}}));
        QCOMPARE(system.setting("username").toString(), QString("before"));
        QVERIFY(!system.setting("oversize").isValid());
        QCOMPARE(changed.count(), 0);
        QVERIFY(system.setSettings({{"username", "after"}, {"displayName", "After"}}));
        QCOMPARE(changed.count(), 1);
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        QCOMPARE(restored.setting("username").toString(), QString("after"));
    }
};

QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
