#include "desktop.h"
#include "display.h"
#include "login.h"
#include "lua_apps.h"
#include "mods.h"
#include "package_apps.h"
#include "shell.h"
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QFile>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMdiSubWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <memory>

class UiTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");
        QFontDatabase::addApplicationFont(":/fonts/DejaVuSansMono.ttf");
    }
    void readableFontUpdatesExistingAppsAndPersists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        system.setSetting("fontSize", 20);
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QApplication::processEvents();
        auto *initial = desktop.findChild<QMdiSubWindow *>("app_terminal");
        QVERIFY(initial);
        QVERIFY(initial->x() >= 145);
        auto *terminal = desktop.findChild<QPlainTextEdit *>("terminalOutput");
        QVERIFY(terminal);
        QTRY_COMPARE(terminal->font().pixelSize(), 20);
        QCOMPARE(terminal->font().family(), QString("DejaVu Sans Mono"));
        desktop.launch("editor");
        auto *editor = desktop.findChild<QPlainTextEdit *>("editorText");
        QVERIFY(editor);
        system.setSetting("fontSize", 14);
        QTRY_COMPARE(terminal->font().pixelSize(), 14);
        QTRY_COMPARE(editor->font().pixelSize(), 14);
        system.setSetting("fontSize", 20);
        QTRY_COMPARE(editor->font().pixelSize(), 20);
        SimSystem reopened(directory.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.setting("fontSize").toInt(), 20);
    }
    void settingsControlsApplyFontAndRestoreWallpaper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QTest::qWait(30);
        auto *area = desktop.findChild<QMdiArea *>("desktopArea");
        QVERIFY(area);
        const QImage original = area->background().textureImage();
        QVERIFY(!original.isNull());
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(QColor("#d147a3"));
        QByteArray data;
        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        const QString path = "/home/guest/images/teste-wallpaper.png";
        QVERIFY(system.write(path, data));
        system.setSetting("wallpaperPath", path);
        QTRY_VERIFY(area->background().textureImage() != original);
        desktop.launch("settings");
        auto *size = desktop.findChild<QComboBox *>("fontSize");
        QVERIFY(size);
        const int index = size->findData(18);
        QVERIFY(index >= 0);
        size->setCurrentIndex(index);
        QCOMPARE(system.setting("fontSize").toInt(), 18);
        auto *terminal = desktop.findChild<QPlainTextEdit *>("terminalOutput");
        QVERIFY(terminal);
        QTRY_COMPARE(terminal->font().pixelSize(), 18);
        // Return to the original size so the wallpaper brush uses identical text metrics.
        size->setCurrentIndex(size->findData(16));
        auto *restore = desktop.findChild<QPushButton *>("wallpaperRestore");
        QVERIFY(restore);
        restore->click();
        QCOMPARE(system.setting("wallpaperPath").toString(), QString());
        QTRY_COMPARE(area->background().textureImage(), original);
    }
    void packageCommandsInstallAndLaunchOnlyWhenAvailable() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Shell shell(&system);
        QSignalSpy launch(&shell, &Shell::launchRequested);
        QVERIFY(shell.execute("htop").startsWith("error:"));
        QCOMPARE(launch.count(), 0);
        QVERIFY(shell.execute("fastfetch").startsWith("error:"));
        for (const QString &name : QStringList{"fastfetch", "htop", "nano", "less"}) {
            const QString result = shell.execute("aurora -i " + name);
            QVERIFY2(!result.startsWith("error:"), qPrintable(result));
        }
        const QString fetch = shell.execute("fastfetch");
        QVERIFY2(fetch.contains("AURORA"), qPrintable(fetch));
        QVERIFY(fetch.contains("A16"));
        shell.execute("htop");
        QCOMPARE(launch.count(), 1);
        QCOMPARE(launch.takeFirst().at(0).toString(), QString("htop"));
        const QString file = "/home/guest/docs/reader sample.txt";
        QVERIFY(system.write(file, "AURORA English reader\nPersistent virtual text.\n"));
        shell.execute("nano \"docs/reader sample.txt\"");
        QCOMPARE(launch.count(), 1);
        auto request = launch.takeFirst();
        QCOMPARE(request.at(0).toString(), QString("editor"));
        QCOMPARE(request.at(1).toString(), file);
        shell.execute("less \"docs/reader sample.txt\"");
        QCOMPARE(launch.count(), 1);
        request = launch.takeFirst();
        QCOMPARE(request.at(0).toString(), QString("pager"));
        QCOMPARE(request.at(1).toString(), file);
        std::unique_ptr<QWidget> pager(createTextPager(&system, file));
        auto *text = pager->findChild<QPlainTextEdit *>("pagerText");
        QVERIFY(text);
        QVERIFY(text->isReadOnly());
        QCOMPARE(text->toPlainText(), QString("AURORA English reader\nPersistent virtual text.\n"));
        SimSystem reopened(directory.path());
        QVERIFY(reopened.load());
        Shell nextSession(&reopened);
        const QString afterRestart = nextSession.execute("fastfetch");
        QVERIFY2(afterRestart.contains("AURORA"), qPrintable(afterRestart));
    }
    void packageCenterInstallsThroughItsVisibleControls() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        std::unique_ptr<QWidget> center(
            createPackageManager(&system, [](const QString &, const QString &) {}));
        center->resize(840, 580);
        center->show();
        auto *input = center->findChild<QLineEdit *>("packageInput");
        auto *install = center->findChild<QPushButton *>("packageInstall");
        auto *table = center->findChild<QTableWidget *>("packageTable");
        auto *log = center->findChild<QPlainTextEdit *>("packageLog");
        QVERIFY(input);
        QVERIFY(install);
        QVERIFY(table);
        QVERIFY(log);
        QVERIFY(table->rowCount() >= 2);
        input->setText("htop");
        install->click();
        Shell shell(&system);
        auto installed = [&shell] {
            QSignalSpy launch(&shell, &Shell::launchRequested);
            shell.execute("htop");
            return launch.count() == 1;
        };
        QTRY_VERIFY(installed());
        QVERIFY(log->toPlainText().contains("htop", Qt::CaseInsensitive));
    }
    void htopFiltersSortsAndTracksSimulatedProcesses() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        const int low = system.startProcess("ui-test.low", 64);
        const int high = system.startProcess("ui-test.high", 256);
        QVERIFY(low > 2);
        QVERIFY(high > 2);
        std::unique_ptr<QWidget> view(createHtop(&system));
        view->resize(840, 540);
        view->show();
        auto *table = view->findChild<QTableWidget *>("htopTable");
        auto *filter = view->findChild<QLineEdit *>("htopFilter");
        auto *sort = view->findChild<QComboBox *>("htopSort");
        auto *summary = view->findChild<QLabel *>("htopSummary");
        QVERIFY(table);
        QVERIFY(filter);
        QVERIFY(sort);
        QVERIFY(summary);
        QTRY_COMPARE(table->rowCount(), system.processes().size());
        QVERIFY(summary->text().contains("Disk"));
        bool foundRam = false;
        for (auto *bar : view->findChildren<QProgressBar *>())
            if (bar->maximum() == SimSystem::MemoryKiB) {
                foundRam = true;
                QCOMPARE(bar->value(), system.usedMemoryKiB());
            }
        QVERIFY(foundRam);
        filter->setText("ui-test.");
        QTRY_COMPARE(table->rowCount(), 2);
        const int memoryOrder = sort->findData("memory");
        QVERIFY(memoryOrder >= 0);
        sort->setCurrentIndex(memoryOrder);
        QTRY_VERIFY(!table->findItems("ui-test.high", Qt::MatchExactly).isEmpty());
        QTRY_COMPARE(table->findItems("ui-test.high", Qt::MatchExactly).first()->row(), 0);
        filter->setText("ui-test.low");
        QTRY_COMPARE(table->rowCount(), 1);
        QVERIFY(system.stopProcess(low));
        QTRY_COMPARE(table->rowCount(), 0);
        filter->clear();
        QTRY_COMPARE(table->rowCount(), system.processes().size());
        QVERIFY(!table->findItems("ui-test.high", Qt::MatchExactly).isEmpty());
    }
    void fileBrowserNavigatesFiltersAndCopiesOnVirtualDisk() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Speaker speaker(&system);
        std::unique_ptr<QWidget> files(createFiles(&system, &speaker, [](const QString &, const QString &) {}, {}));
        files->resize(900, 600);
        files->show();
        auto *path = files->findChild<QLineEdit *>("filesPath");
        auto *filter = files->findChild<QLineEdit *>("filesSearch");
        auto *table = files->findChild<QTableWidget *>("fileTable");
        auto *back = files->findChild<QPushButton *>("filesBack");
        QVERIFY(path && filter && table && back);
        auto clickButton = [&](const QString &text) {
            for (auto *button : files->findChildren<QPushButton *>())
                if (button->text() == text) { button->click(); return true; }
            return false;
        };
        const auto matches = table->findItems("README.txt", Qt::MatchExactly);
        QCOMPARE(matches.size(), 1);
        table->selectRow(matches.first()->row());
        QVERIFY(clickButton("Copy"));
        path->setText("/home/guest/docs");
        QMetaObject::invokeMethod(path, "returnPressed");
        QVERIFY(clickButton("Paste"));
        QCOMPARE(system.read("/home/guest/docs/README.txt"), system.read("/home/guest/README.txt"));
        filter->setText("welcome.bas");
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QString("welcome.bas"));
        back->click();
        QCOMPARE(path->text(), QString("/home/guest"));
        QVERIFY(filter->text().isEmpty());
    }
    void systemCenterControlsServicesAndTracksProcesses() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        std::unique_ptr<QWidget> center(createMonitor(&system));
        auto *services = center->findChild<QTableWidget *>("systemServices");
        auto *processes = center->findChild<QTableWidget *>("systemProcesses");
        auto *journal = center->findChild<QPlainTextEdit *>("systemJournal");
        QVERIFY(services && processes && journal);
        auto networkRunning = [&] {
            for (const auto &service : system.services())
                if (service.name == "network") return service.running;
            return false;
        };
        const auto network = services->findItems("network", Qt::MatchExactly);
        QCOMPARE(network.size(), 1);
        services->selectRow(network.first()->row());
        center->findChild<QPushButton *>("serviceStop")->click();
        QVERIFY(!networkRunning());
        QVERIFY(journal->toPlainText().contains("Service stopped"));
        center->findChild<QPushButton *>("serviceStart")->click();
        QVERIFY(networkRunning());
        const int pid = system.startProcess("ui.worker", 64);
        QVERIFY(pid > 2);
        QVERIFY(!processes->findItems("ui.worker", Qt::MatchExactly).isEmpty());
        QVERIFY(system.stopProcess(pid));
        QVERIFY(processes->findItems("ui.worker", Qt::MatchExactly).isEmpty());
    }
    void launcherSearchOpensApplicationAndRespectsSessionLock() {
        QTemporaryDir directory;
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QApplication::processEvents();
        auto *shortcut = desktop.findChild<QShortcut *>("launcherShortcut");
        QVERIFY(shortcut);
        QVERIFY(QMetaObject::invokeMethod(shortcut, "activated"));
        auto *search = desktop.findChild<QLineEdit *>("launcherSearch");
        auto *results = desktop.findChild<QListWidget *>("launcherResults");
        QVERIFY(search && results);
        search->setText("Calculator");
        int visible = 0;
        for (int i = 0; i < results->count(); ++i)
            if (!results->item(i)->isHidden()) ++visible;
        QCOMPARE(visible, 1);
        QTest::keyClick(search, Qt::Key_Return);
        QTRY_VERIFY(desktop.findChild<QMdiSubWindow *>("app_calc"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        desktop.lockSession();
        QVERIFY(QMetaObject::invokeMethod(shortcut, "activated"));
        QVERIFY(!desktop.findChild<QWidget *>("appLauncher"));
    }
    void desktopUsesEnglishAndChecksOptionalHtop() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QApplication::processEvents();
        QStringList menus;
        for (auto *action : desktop.menuBar()->actions())
            menus << action->text();
        QVERIFY(menus.contains("File"));
        QVERIFY(menus.contains("Applications"));
        QVERIFY(menus.contains("Games"));
        QVERIFY(menus.contains("Windows"));
        auto *terminal = desktop.findChild<QPlainTextEdit *>("terminalOutput");
        QVERIFY(terminal);
        QVERIFY(terminal->toPlainText().contains("Welcome, Guest (guest)"));
        const auto before = system.processes().size();
        QTimer::singleShot(0, &desktop, [] {
            for (auto *w : QApplication::topLevelWidgets())
                if (auto *box = qobject_cast<QMessageBox *>(w))
                    box->accept();
        });
        desktop.launch("htop");
        QVERIFY(!desktop.findChild<QMdiSubWindow *>("app_htop"));
        QCOMPARE(system.processes().size(), before);
        Shell shell(&system);
        const QString installed = shell.execute("aurora -i htop");
        QVERIFY2(!installed.startsWith("error:"), qPrintable(installed));
        desktop.launch("htop");
        QVERIFY(desktop.findChild<QMdiSubWindow *>("app_htop"));
        QCOMPARE(system.processes().size(), before + 1);
    }
    void auroraSyncGatesAndLocksExistingSession() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QVERIFY(setLoginPassword(&system, "correct passphrase"));
        Desktop desktop(&system, true);
        desktop.show();
        QApplication::processEvents();
        QVERIFY(desktop.isSessionLocked());
        QVERIFY(!desktop.findChild<QMdiArea *>("desktopArea"));
        desktop.launch("calc");
        QCOMPARE(system.processes().size(), 2);
        auto *password = desktop.findChild<QLineEdit *>("loginPassword");
        QVERIFY(password);
        password->setText("wrong");
        QTest::keyClick(password, Qt::Key_Return);
        QVERIFY(desktop.isSessionLocked());
        QVERIFY(!desktop.findChild<QLabel *>("loginError")->text().isEmpty());
        password->setText("correct passphrase");
        QTest::keyClick(password, Qt::Key_Return);
        QTRY_VERIFY(!desktop.isSessionLocked());
        auto *area = desktop.findChild<QMdiArea *>("desktopArea");
        QVERIFY(area);
        auto *terminal = desktop.findChild<QLineEdit *>("terminalInput");
        QVERIFY(terminal);
        terminal->setText("echo keep > /home/guest/session.txt");
        QMetaObject::invokeMethod(terminal, "returnPressed");
        const int processes = system.processes().size();
        desktop.lockSession();
        QVERIFY(desktop.isSessionLocked());
        desktop.launch("calc");
        QCOMPARE(system.processes().size(), processes);
        QVERIFY(!terminal->isVisible());
        password = desktop.findChild<LoginScreen *>("auroraSYNC")->findChild<QLineEdit *>("loginPassword");
        password->setText("correct passphrase");
        QTest::keyClick(password, Qt::Key_Return);
        QTRY_VERIFY(!desktop.isSessionLocked());
        QCOMPARE(desktop.findChild<QMdiArea *>("desktopArea"), area);
        QCOMPARE(system.read("/home/guest/session.txt"), QByteArray("keep\n"));
    }
    void virtualDisplayModesAndProfileApplyToLiveSession() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QApplication::processEvents();
        auto *area = desktop.findChild<QMdiArea *>("desktopArea");
        QVERIFY(area);
        system.setSetting("displayPreset", "1024x768");
        QTRY_COMPARE(area->viewport()->size(), QSize(1024, 768));
        QTRY_COMPARE(system.setting("resolutionWidth").toInt(), 1024);
        QTRY_COMPARE(system.setting("resolutionHeight").toInt(), 768);
        system.setSetting("displayPreset", "1280x960");
        QTRY_COMPARE(area->viewport()->size(), QSize(1280, 960));
        system.setSetting("displayPreset", "auto");
        auto *frame = desktop.findChild<QScrollArea *>("displayFrame");
        QVERIFY(frame);
        QTRY_COMPARE(area->size(), frame->viewport()->size());
        QVERIFY(system.setSettings({{"username", "operator"}, {"displayName", "Aurora Operator"}}));
        Shell shell(&system);
        QVERIFY(shell.prompt().startsWith("operator@aurora86:"));
        QCOMPARE(shell.execute("whoami"), QString("operator"));
        desktop.launch("settings", "identity");
        QVERIFY(desktop.findChild<QLineEdit *>("identityUsername"));
        auto *profile = desktop.findChild<QLineEdit *>("identityUsername");
        profile->setText("pilot");
        desktop.findChild<QPushButton *>("identitySave")->click();
        QCOMPARE(system.setting("username").toString(), QString("pilot"));
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        QCOMPARE(restored.setting("username").toString(), QString("pilot"));
    }
    void userLuaModInstallsLaunchesAndRunsButtons() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QDir().mkpath(folder.filePath("mods"));
        QFile file(folder.filePath("mods/counter.lua"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("local n=0; aurora.window{title='Test Lua "
                   "counter',text='ready',buttons={{label='Increment',action=function() "
                   "n=n+1;aurora.text('Count '..n);aurora.write('/home/guest/count.txt',tostring(n)) end}}}");
        file.close();
        system.setSetting("modsDirectory", folder.filePath("mods"));
        Desktop desktop(&system, true, nullptr, true);
        desktop.show();
        QApplication::processEvents();
        Shell shell(&system);
        QObject::connect(&shell, &Shell::launchRequested, &desktop, &Desktop::launch);
        const QString installed = shell.execute("aurora -i counter");
        QVERIFY2(!installed.startsWith("error:"), qPrintable(installed));
        QVERIFY(isModInstalled(&system, "counter"));
        QVERIFY(shell.execute("aurora --list").contains("counter"));
        QCOMPARE(shell.execute("counter"), QString());
        auto *body = desktop.findChild<QPlainTextEdit *>("luaText");
        QVERIFY(body);
        QCOMPARE(body->toPlainText(), QString("ready"));
        auto *increment = desktop.findChild<QPushButton *>("luaButton_0");
        QVERIFY(increment);
        increment->click();
        QCOMPARE(body->toPlainText(), QString("Count 1"));
        QCOMPARE(system.read("/home/guest/count.txt"), QByteArray("1"));
        QVERIFY(!shell.execute("aurora -r counter").startsWith("error:"));
        QVERIFY(!isModInstalled(&system, "counter"));
        QVERIFY(shell.execute("counter").startsWith("error:"));
    }
    void luaNotebookSavesTypedTextAndRestoresIt() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QFile example(":/mods/notepad.lua");
        QVERIFY(example.open(QIODevice::ReadOnly));
        const QString source = "/home/guest/docs/notepad.lua";
        QVERIFY(system.write(source, example.readAll()));
        std::unique_ptr<QWidget> program(createLuaProgram(&system, source));
        auto *note = program->findChild<QPlainTextEdit *>("luaInput_note");
        QVERIFY(note);
        const QString text = QString::fromUtf8("Workshop notes\nOlá, AURORA!\n");
        note->setPlainText(text);
        program->findChild<QPushButton *>("luaButton_0")->click();
        QCOMPARE(system.read("/home/guest/docs/lua-notes.txt"), text.toUtf8());
        program.reset();
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        program.reset(createLuaProgram(&restored, source));
        note = program->findChild<QPlainTextEdit *>("luaInput_note");
        QVERIFY(note);
        QCOMPARE(note->toPlainText(), text);
        note->setPlainText(QString(4097, 'x'));
        program->findChild<QPushButton *>("luaButton_0")->click();
        QCOMPARE(restored.read("/home/guest/docs/lua-notes.txt"), text.toUtf8());
        QVERIFY(program->findChild<QPlainTextEdit *>("luaOutput")->toPlainText().contains("4096"));
        const QByteArray invalid("bad\0bytes", 9);
        QVERIFY(restored.write("/home/guest/docs/lua-notes.txt", invalid));
        note->setPlainText(text);
        program->findChild<QPushButton *>("luaButton_1")->click();
        program->findChild<QPushButton *>("luaButton_0")->click();
        QCOMPARE(restored.read("/home/guest/docs/lua-notes.txt"), invalid);
        QVERIFY(program->findChild<QPlainTextEdit *>("luaText")->toPlainText().contains("Reload"));
    }
    void handbookAndScreenCaptureUseTheVirtualDisk() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true);
        desktop.show();
        QApplication::processEvents();
        QVERIFY(desktop.isSessionLocked());
        QVERIFY(desktop.captureVirtualScreen().isEmpty());
        desktop.findChild<QPushButton *>("loginSubmit")->click();
        QApplication::processEvents();
        auto *help = desktop.findChild<QShortcut *>("handbookShortcut");
        QVERIFY(help);
        QVERIFY(QMetaObject::invokeMethod(help, "activated"));
        auto *tabs = desktop.findChild<QTabWidget *>("handbookTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 4); // Keyboard, Terminal, HELLGATE, Lua
        auto *capture = desktop.findChild<QShortcut *>("screenshotShortcut");
        QVERIFY(capture);
        QVERIFY(QMetaObject::invokeMethod(capture, "activated"));
        const QString first = "/home/guest/images/snap-0001.png";
        const QByteArray original = system.read(first);
        QVERIFY(!QImage::fromData(original, "PNG").isNull());
        QCOMPARE(desktop.findChild<QLineEdit *>("imagePath")->text(), first);
        QCOMPARE(desktop.captureVirtualScreen(), QString("/home/guest/images/snap-0002.png"));
        QCOMPARE(system.read(first), original);
        QVERIFY(system.chmod("/home/guest/images", 0555));
        QVERIFY(desktop.captureVirtualScreen().isEmpty());
        QVERIFY(!system.exists("/home/guest/images/snap-0003.png"));
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        QCOMPARE(restored.read(first), original);
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
