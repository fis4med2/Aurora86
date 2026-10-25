#include "shell.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class ShellTest : public QObject {
    Q_OBJECT
  private slots:
    void systemCommandsControlOnlyTheVirtualMachine() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QSignalSpy launches(&shell, &Shell::launchRequested);
        QVERIFY(shell.execute("systemctl").contains("network.service"));
        QVERIFY(shell.execute("systemctl stop spooler").contains("stopped"));
        QCOMPARE(shell.execute("systemctl is-active spooler"), QString("inactive"));
        QCOMPARE(shell.lastStatus(), 3);
        QVERIFY(shell.execute("lpr docs/journal.txt").contains("error: spooler is stopped"));
        QVERIFY(shell.execute("systemctl restart spooler").contains("started"));
        QVERIFY(shell.execute("lpr docs/journal.txt").contains("queued"));
        QVERIFY(shell.execute("systemctl start nonexistent").startsWith("error:"));
        QVERIFY(shell.execute("journalctl -u spooler -n 1").contains("Service started"));
        QVERIFY(shell.execute("journalctl -n -1").startsWith("usage:"));
        QCOMPARE(shell.execute("hostnamectl set-hostname studio16"), QString());
        QCOMPARE(shell.execute("hostname"), QString("studio16"));
        QVERIFY(shell.execute("hostnamectl").contains("A16"));
        QVERIFY(shell.execute("uptime").contains("simulated"));
        QCOMPARE(shell.execute("doom"), QString());
        QCOMPARE(launches.last().first().toString(), QString("doom"));
        QCOMPARE(shell.execute("mission-control"), QString());
        QCOMPARE(launches.last().first().toString(), QString("mission-control"));
    }

    void findAndDuUseTheVirtualDiskAndRespectPermissions() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QVERIFY(system.mkdir("projects"));
        QVERIFY(system.mkdir("projects/nested"));
        QVERIFY(system.write("projects/nested/readme.txt", QByteArray(1100, 'a')));
        QVERIFY(system.write("projects/data.bin", "data"));
        QCOMPARE(shell.execute("find projects -name '*.txt' -type f"), QString("/home/guest/projects/nested/readme.txt"));
        QVERIFY(shell.execute("du projects").startsWith("2 KiB"));
        QVERIFY(shell.execute("find projects -type d").contains("/home/guest/projects/nested"));
        QVERIFY(shell.execute("find missing").startsWith("error:"));
        QVERIFY(system.chmod("projects/nested", 0600));
        QVERIFY(shell.execute("find projects").startsWith("error: Permission denied"));
    }

    void pipesFeedStandardInput() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QCOMPARE(shell.execute("aurora -i grep coreutils sort").startsWith("error:"), false);
        QCOMPARE(shell.execute("echo \"aurora is here\" | grep aurora"), QString("aurora is here"));
        QCOMPARE(shell.execute("cat docs/commands.txt | wc -l").trimmed().toInt() > 0, true);
        QCOMPARE(shell.execute("ls docs | grep journal"), QString("journal.txt"));
        QVERIFY(shell.execute("cat docs/journal.txt | grep keyboard").isEmpty());
        QCOMPARE(shell.execute("cat docs/journal.txt | grep -c 1986"), QString("1"));
    }

    void sequencesAndStatusCodes() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QCOMPARE(shell.execute("mkdir a ; cd a ; pwd"), QString("/home/guest/a"));
        QCOMPARE(shell.execute("true ; echo $?"), QString("0"));
        QCOMPARE(shell.execute("false ; echo $?"), QString("1"));
        const QString chained = shell.execute("false && echo never");
        QVERIFY(!chained.contains("never"));
        QCOMPARE(shell.lastStatus(), 1);
        QCOMPARE(shell.execute("cd ~"), QString());
    }

    void variablesExportAndUnset() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QCOMPARE(shell.execute("export GREETING=ola ; echo $GREETING"), QString("ola"));
        QCOMPARE(shell.execute("echo ${HOME}"), QString("/home/guest"));
        QVERIFY(shell.execute("env").contains("PATH=/bin"));
        QCOMPARE(shell.execute("unset GREETING ; echo \"[$GREETING]\""), QString("[]"));
        QVERIFY(shell.execute("export 1BAD=x").startsWith("error:"));

        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.setting("shellEnv").toMap().value("HOME").toString(), QString());
    }

    void aliasesPersist() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QCOMPARE(shell.execute("alias ll=ls"), QString());
        QCOMPARE(shell.execute("alias"), QString("alias ll='ls'"));
        QVERIFY(shell.execute("ll").contains("docs"));
        QCOMPARE(shell.execute("unalias ll"), QString());
        QCOMPARE(shell.execute("alias"), QString("No aliases defined."));
        QVERIFY(shell.execute("alias bad=1 ; bad").startsWith("error: unknown command"));

        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.setting("shellAliases").toMap().size(), 1);
    }

    void wildcardsExpandAgainstTheDisk() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QCOMPARE(shell.execute("echo *.md"), QString("*.md"));
        QCOMPARE(shell.execute("touch alfa.txt beta.txt gama.md"), QString());
        QCOMPARE(shell.execute("mkdir pasta1 pasta2"), QString());
        QVERIFY(system.exists("/home/guest/pasta2"));
        QCOMPARE(shell.execute("echo *.txt"), QString("README.txt alfa.txt beta.txt"));
        QCOMPARE(shell.execute("echo *.doesnotexist"), QString("*.doesnotexist"));
        QCOMPARE(shell.execute("echo docs/*.txt"), QString("docs/commands.txt docs/journal.txt"));
    }

    void historyAndWhich() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        shell.execute("pwd");
        shell.execute("whoami");
        const QString history = shell.execute("history");
        QVERIFY(history.contains("pwd"));
        QVERIFY(history.contains("whoami"));
        QVERIFY(shell.execute("which ls").contains("built-in"));
        QVERIFY(shell.execute("which grep").contains("not installed"));
        QVERIFY(shell.execute("which nothing-here").startsWith("error:"));
        QCOMPARE(shell.execute("history -c"), QString());
        QCOMPARE(shell.history().size(), 0);
    }

    void procExposesLiveValues() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QVERIFY(system.exists("/proc/meminfo"));
        QVERIFY(shell.execute("cat /proc/meminfo").contains("MemTotal"));
        QVERIFY(shell.execute("cat /proc/cpuinfo").contains("A16"));
        QVERIFY(shell.execute("cat /proc/version").contains("AURORA/86"));
        QVERIFY(shell.execute("ls /proc").contains("uptime"));
        QVERIFY(shell.execute("cat /proc/uptime").contains("."));
        QVERIFY(shell.execute("echo nope > /proc/meminfo").startsWith("error:"));
    }

    void printerQueue() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QVERIFY(shell.execute("lpq").startsWith("A16 printer queue is empty"));
        QVERIFY(shell.execute("lpr docs/journal.txt").contains("job 1"));
        QVERIFY(shell.execute("lpr docs/commands.txt").contains("job 2"));
        const QString queue = shell.execute("lpq");
        QVERIFY(queue.contains("journal"));
        QVERIFY(queue.contains("commands"));
        QVERIFY(shell.execute("lprm 1").contains("1 job"));
        QVERIFY(!shell.execute("lpq").contains("journal"));
        QVERIFY(shell.execute("lprm all").contains("1 job"));
        QVERIFY(shell.execute("lpq").startsWith("A16 printer queue is empty"));
        QVERIFY(shell.execute("lpr missing.txt").startsWith("error:"));
    }

    void archivesRoundTrip() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QVERIFY(shell.execute("pack backup.arc docs/journal.txt docs/commands.txt").contains("2 members"));
        const QString listing = shell.execute("arc -l backup.arc");
        QVERIFY(listing.contains("journal.txt"));
        QVERIFY(listing.contains("commands.txt"));
        QVERIFY(shell.execute("unpack backup.arc restored").contains("journal.txt"));
        QCOMPARE(system.read("/home/guest/restored/journal.txt"), system.read("/home/guest/docs/journal.txt"));
        QVERIFY(shell.execute("unpack docs/journal.txt").startsWith("error:"));
        QVERIFY(shell.execute("pack one.arc docs/journal.txt").contains("1 members"));
    }

    void basicRunsInsideTheTerminal() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QSignalSpy sounds(&shell, &Shell::soundRequested);
        QVERIFY(shell.execute("basic").contains("A16 BASIC"));
        QVERIFY(shell.inBasic());
        QCOMPARE(shell.prompt(), QString("basic> "));
        QCOMPARE(shell.execute("10 PRINT \"HI\""), QString());
        QCOMPARE(shell.execute("20 BEEP"), QString());
        QCOMPARE(shell.execute("RUN"), QString("HI\n"));
        QCOMPARE(sounds.count(), 1);
        QVERIFY(shell.execute("SAVE \"DEMO.BAS\"").isEmpty());
        QVERIFY(system.exists("/home/guest/DEMO.BAS"));
        QVERIFY(shell.execute("SYSTEM").contains("Leaving"));
        QVERIFY(!shell.inBasic());
        QVERIFY(shell.prompt().endsWith("$ "));

        QVERIFY(shell.execute("basic").contains("A16 BASIC"));
        QVERIFY(shell.execute("LOAD \"DEMO.BAS\"").isEmpty());
        QCOMPARE(shell.execute("RUN"), QString("HI\n"));
        QVERIFY(shell.execute("SYSTEM").contains("Leaving"));
    }

    void shippedBasicDemoRuns() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QVERIFY(shell.execute("basic docs/welcome.bas").contains("Loaded"));
        const QString output = shell.execute("RUN");
        QVERIFY(output.contains("A16 BASIC"));
        QVERIFY(output.contains("1 SQUARED IS"));
        QVERIFY(output.contains("4 SQUARED IS"));
        shell.execute("SYSTEM");
    }

    void basicClearReachesTheTerminal() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QSignalSpy cleared(&shell, &Shell::clearRequested);
        shell.execute("basic");
        shell.execute("10 CLS");
        QCOMPARE(shell.execute("RUN"), QString());
        QCOMPARE(cleared.count(), 1);
        shell.execute("SYSTEM");
    }
};

QTEST_GUILESS_MAIN(ShellTest)
#include "shell_test.moc"
