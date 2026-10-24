#include "packages.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class PackageTest final : public QObject {
    Q_OBJECT
  private slots:
    void installResolvePersistAndRemove() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        const int before = system.usedBytes();
        QVERIFY(manager.installed().isEmpty());
        QVERIFY(manager.run("fastfetch", {}, "/home/guest").startsWith("error:"));
        QVERIFY(!manager.install({"neofetch", "htop", "wc"}).startsWith("error:"));
        QCOMPARE(manager.installed(),
                 QStringList({"coreutils", "fastfetch", "htop", "neofetch", "sysutils"}));
        QVERIFY(manager.isInstalled("tail"));
        QVERIFY(system.usedBytes() > before + 35000);
        const auto bytes = system.read("/home/guest/.aurora-packages.json");
        QVERIFY(manager.install({"unknown-package", "tree"}).startsWith("error:"));
        QCOMPARE(system.read("/home/guest/.aurora-packages.json"), bytes);
        QVERIFY(!manager.isInstalled("tree"));
        QVERIFY(manager.remove({"sysutils"}).startsWith("error:"));
        QCOMPARE(system.read("/home/guest/.aurora-packages.json"), bytes);
        SimSystem reopened(dir.path());
        QVERIFY(reopened.load());
        PackageManager restored(&reopened);
        QVERIFY(restored.isInstalled("neofetch"));
        QVERIFY(restored.run("fastfetch", {}, "/home/guest").contains("Memory: 512 / 4096 KiB"));
        QVERIFY(!restored.remove({"neofetch", "htop", "fastfetch", "sysutils"}).startsWith("error:"));
        QCOMPARE(restored.installed(), QStringList{"coreutils"});
        QVERIFY(!restored.remove({"wc"}).startsWith("error:"));
        QVERIFY(restored.installed().isEmpty());
    }
    void quotaAndDatabaseCorruptionAreAtomic() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QVERIFY(!manager.install({"fortune"}).startsWith("error:"));
        const QString registry = "/home/guest/.aurora-packages.json";
        const auto before = system.read(registry);
        const int free = SimSystem::DiskBytes - system.usedBytes();
        const QString path = "/home/guest/filler";
        QVERIFY(system.write(path, QByteArray(free - path.toUtf8().size() - 64 - 100, 'x')));
        QVERIFY(manager.install({"tree", "fastfetch"}).startsWith("error:"));
        QCOMPARE(system.read(registry), before);
        QVERIFY(manager.isInstalled("fortune"));
        QVERIFY(!manager.isInstalled("tree"));
        QVERIFY(system.remove(path));
        QVERIFY(system.write(registry, "{broken database"));
        const auto corrupt = system.read(registry);
        QVERIFY(manager.install({"tree"}).contains("preserved"));
        QVERIFY(manager.remove({"fortune"}).contains("preserved"));
        QCOMPARE(system.read(registry), corrupt);
    }
    void commandsReadAndWriteVirtualFiles() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QVERIFY(
            !manager.install({"coreutils", "tree", "grep", "find", "xxd", "manuals"}).startsWith("error:"));
        QVERIFY(system.mkdir("docs/sample"));
        QVERIFY(system.write("docs/sample/words.txt", "Beta alpha\nalpha\nalpha\nZulu\n"));
        const QString cwd = "/home/guest/docs/sample";
        QCOMPARE(manager.run("wc", {"-lwmc", "words.txt"}, cwd), QString("4 5 28 28 words.txt"));
        QCOMPARE(manager.run("head", {"-n", "2", "words.txt"}, cwd), QString("Beta alpha\nalpha\n"));
        QCOMPARE(manager.run("tail", {"-n", "1", "words.txt"}, cwd), QString("Zulu\n"));
        QCOMPARE(manager.run("grep", {"-in", "ALPHA", "words.txt"}, cwd),
                 QString("1:Beta alpha\n2:alpha\n3:alpha"));
        QCOMPARE(manager.run("grep", {"-c", "alpha", "words.txt"}, cwd), QString("3"));
        QCOMPARE(manager.run("grep", {"-v", "alpha", "words.txt"}, cwd), QString("Zulu"));
        QVERIFY(manager.run("grep", {"[", "words.txt"}, cwd).startsWith("error:"));
        QCOMPARE(manager.run("uniq", {"-c", "words.txt"}, cwd), QString("1 Beta alpha\n2 alpha\n1 Zulu\n"));
        QVERIFY(system.write("docs/sample/numbers.txt", "9\n2\n2\n10\n"));
        QCOMPARE(manager.run("sort", {"-nu", "numbers.txt"}, cwd), QString("2\n9\n10\n"));
        QVERIFY(manager.run("tree", {"."}, cwd).contains("words.txt"));
        QVERIFY(manager.run("find", {".", "-name", "*.txt", "-type", "f"}, cwd).contains(cwd + "/words.txt"));
        const QByteArray binary("\0A\xff\n", 4);
        QVERIFY(system.write("docs/sample/data.bin", binary));
        QVERIFY(manager.run("base64", {"data.bin", "encoded.txt"}, cwd).startsWith("base64: wrote"));
        QVERIFY(manager.run("base64", {"-d", "encoded.txt", "decoded.bin"}, cwd).startsWith("base64: wrote"));
        QCOMPARE(system.read(cwd + "/decoded.bin"), binary);
        QVERIFY(manager.run("xxd", {"data.bin"}, cwd).contains("00 41 ff 0a"));
        QVERIFY(manager.run("man", {"base64"}, cwd).contains("OUTPUT writes exact bytes"));
        QVERIFY(system.chmod(cwd + "/words.txt", 0200));
        QVERIFY(manager.run("grep", {"alpha", "words.txt"}, cwd).startsWith("error:"));
    }
    void summariesUseCurrentIdentityAndDisplay() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QVERIFY(!manager.install({"fastfetch"}).startsWith("error:"));
        system.setSetting("username", "astrid");
        system.setSetting("resolutionWidth", 1440);
        system.setSetting("resolutionHeight", 900);
        const QString output = manager.run("fastfetch", {"--logo", "none"}, "/home/guest");
        QVERIFY(output.startsWith("astrid@aurora"));
        QVERIFY(output.contains("Display: LUME / 1440 x 900 / 4 tones"));
        SimSystem reopened(directory.path());
        QVERIFY(reopened.load());
        PackageManager restored(&reopened);
        QCOMPARE(restored.run("fastfetch", {"--logo", "none"}, "/home/guest").section('\n', 0, 0),
                 QString("astrid@aurora"));
    }
    void readonlyAndTamperedRegistriesArePreserved() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QVERIFY(!manager.install({"fastfetch"}).startsWith("error:"));
        const QString registry = "/home/guest/.aurora-packages.json";
        const QByteArray before = system.read(registry);
        QVERIFY(system.chmod(registry, 0444));
        QVERIFY(manager.install({"tree"}).startsWith("error:"));
        QVERIFY(manager.remove({"fastfetch"}).startsWith("error:"));
        QCOMPARE(system.read(registry), before);
        QVERIFY(manager.isInstalled("fastfetch"));
        QVERIFY(!manager.isInstalled("tree"));
        QVERIFY(system.chmod(registry, 0644));
        auto object = QJsonDocument::fromJson(before).object();
        auto packages = object.value("packages").toArray();
        for (int i = 0; i < packages.size(); ++i)
            if (packages[i].toObject().value("name").toString() == "sysutils") {
                packages.removeAt(i);
                break;
            }
        object["packages"] = packages;
        const QByteArray damaged = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QVERIFY(system.write(registry, damaged));
        QVERIFY(manager.run("fastfetch", {}, "/home/guest").contains("preserved"));
        QVERIFY(manager.install({"tree"}).contains("preserved"));
        QCOMPARE(system.read(registry), damaged);
    }
    void numericSortAndDeepDiskUsageRemainCorrect() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QVERIFY(!manager.install({"coreutils"}).startsWith("error:"));
        QVERIFY(system.write("numbers.txt", "2\nnan\n1\n"));
        QCOMPARE(manager.run("sort", {"-n", "numbers.txt"}, "/home/guest"), QString("nan\n1\n2\n"));
        const int before = system.usedBytes();
        QString path = "/home/guest/deep";
        QVERIFY(system.mkdir(path));
        for (int i = 0; i < 40; ++i) {
            path += "/a";
            QVERIFY(system.mkdir(path));
        }
        QVERIFY(system.write(path + "/leaf", QByteArray(9000, 'x')));
        const int expected = (system.usedBytes() - before + 1023) / 1024;
        const QString output = manager.run("du", {"/home/guest/deep"}, "/home/guest");
        const QString last = output.section('\n', -1);
        QCOMPARE(last.section('\t', 0, 0).toInt(), expected);
        QVERIFY(last.endsWith("\t/home/guest/deep"));
    }
    void everyCatalogCommandIsFunctional() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        PackageManager manager(&system);
        QStringList names;
        for (const auto &p : manager.catalog())
            names << p.name;
        QCOMPARE(names.size(), 16);
        QVERIFY(!manager.install(names).startsWith("error:"));
        const QString cwd = "/home/guest";
        QVERIFY(manager.run("figlet", {"A16"}, cwd).contains('#'));
        QVERIFY(manager.run("cowsay", {"AURORA works"}, cwd).contains("AURORA works"));
        QVERIFY(!manager.run("fortune", {}, cwd).isEmpty());
        QVERIFY(manager.run("cal", {"2", "1988"}, cwd).contains("29"));
        QVERIFY(!manager.run("cal", {"2", "1987"}, cwd).contains("29"));
        QVERIFY(manager.run("uptime", {}, cwd).contains("processes"));
        QVERIFY(manager.run("htop", {}, cwd).contains("aurora.kernel"));
        QVERIFY(manager.run("du", {}, cwd).contains(cwd));
        QVERIFY(manager.run("nano", {"test.txt"}, cwd).contains(cwd + "/test.txt"));
        QVERIFY(manager.run("less", {"/etc/release"}, cwd).contains("AURORA"));
        QVERIFY(manager.ownsCommand("hexdump"));
        QVERIFY(!manager.ownsCommand("sudo"));
        QVERIFY(manager.search("search").contains("grep"));
        QVERIFY(manager.info("xxd").contains("hexutils"));
    }
};
QTEST_GUILESS_MAIN(PackageTest)
#include "package_test.moc"
