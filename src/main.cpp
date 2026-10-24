#include "desktop.h"
#include "login.h"
#include "system.h"
#include <QImageReader>
#include <QTemporaryDir>
#include <QWebEngineView>
#include <QtWidgets>
#include <iostream>
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QApplication app(argc, argv);
    app.setApplicationName("AURORA86");
    app.setDesktopFileName("io.github.fis4med2.Aurora86");
    app.setOrganizationName("Aurora");
    app.setApplicationVersion("1.3.0");
    app.setStyle("Fusion");
    QImageReader::setAllocationLimit(16);
    QFontDatabase::addApplicationFont(":/fonts/AuroraFixed.ttf");
    QFontDatabase::addApplicationFont(":/fonts/DejaVuSansMono.ttf");
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(16);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    QCommandLineParser parser;
    parser.setApplicationDescription("AURORA/86 — a fictional 1986 personal workstation");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({{{"f", "fullscreen"}, "Start in fullscreen"},
                       {"skip-boot", "Skip the startup self-test"},
                       {"state-dir", "Host directory reserved for simulator state", "directory"},
                       {"self-test", "Check applications without network access and exit"},
                       {"browser-check", "Check HTTP/HTTPS navigation and exit", "url"},
                       {"screenshot", "Save a desktop screenshot and exit", "file"},
                       {"boot-screenshot", "Save a boot screenshot and exit", "file"}});
    parser.process(app);
    QTemporaryDir temporary;
    const bool test = parser.isSet("self-test");
    QString state = test ? temporary.path() : parser.value("state-dir");
    if (state.isEmpty())
        state = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(state)) {
        QMessageBox::warning(nullptr, "State", "Could not create the simulator directory.");
        return 1;
    }
    QLockFile sessionLock(state + "/session.lock");
    sessionLock.setStaleLockTime(0);
    if (!sessionLock.tryLock()) {
        QMessageBox::information(
            nullptr, "AURORA/86",
            "This virtual disk is already open in another session. Use a different --state-dir "
            "for a separate workstation.");
        return 1;
    }
    SimSystem system(state);
    const bool loaded = system.load();
    const QString loadError = system.lastError();
    if (loaded && system.setting("modsDirectory").toString().isEmpty()) {
        QString base = test ? temporary.path() : QCoreApplication::applicationDirPath();
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        if (!test && !appImage.isEmpty())
            base = QFileInfo(appImage).absolutePath();
        if (!test && appImage.isEmpty() && QDir("mods").exists() && !QDir(base + "/mods").exists())
            base = QDir::currentPath();
        QString folder = base + "/mods";
        if (!QDir().mkpath(folder))
            folder = state + "/mods";
        if (QDir().mkpath(folder)) {
            for (const QString &name : QStringList{"hello", "notepad", "theme-switcher"}) {
                const QString target = folder + "/" + name + ".lua";
                if (!QFile::exists(target) && QFile::copy(":/mods/" + name + ".lua", target))
                    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                                      QFileDevice::ReadGroup | QFileDevice::ReadOther);
            }
            system.setSetting("modsDirectory", QDir(folder).absolutePath());
        }
    }

    const bool capture = parser.isSet("screenshot") || parser.isSet("browser-check");
    Desktop desktop(&system, test || parser.isSet("skip-boot") || capture, nullptr,
                    test || (capture && !hasLoginPassword(&system)));
    if (parser.isSet("fullscreen") || system.setting("startupFullscreen", false).toBool())
        desktop.showFullScreen();
    else
        desktop.show();
    if (!loaded && !test)
        QTimer::singleShot(250, &desktop, [&desktop, loadError] {
            QMessageBox::warning(
                &desktop, "State preserved",
                loadError +
                    "\nThe original file has been preserved. Use another --state-dir for a new session.");
        });
    if (parser.isSet("screenshot") && !parser.isSet("browser-check"))
        QTimer::singleShot(700, &desktop, [&] {
            desktop.screenshot(parser.value("screenshot"));
            app.exit(0);
        });
    if (parser.isSet("boot-screenshot"))
        QTimer::singleShot(2000, &desktop, [&] {
            desktop.screenshot(parser.value("boot-screenshot"));
            app.exit(0);
        });
    if (parser.isSet("browser-check"))
        QTimer::singleShot(100, &desktop, [&] {
            desktop.launch("browser");
            auto *view = desktop.findChild<QWebEngineView *>("browserView");
            if (!view) {
                app.exit(1);
                return;
            }
            QUrl target(parser.value("browser-check"));
            QObject::connect(view, &QWebEngineView::loadFinished, &desktop, [&, view, target](bool ok) {
                if (view->url().host() != target.host())
                    return;
                if (!ok) {
                    std::cerr << "FAIL: real navigation\n";
                    app.exit(1);
                    return;
                }
                view->page()->runJavaScript("document.title + '|' + document.body.innerText.length",
                                            [&, view](const QVariant &value) {
                                                std::cout << "PASS: Chromium HTTP/HTTPS and JavaScript / "
                                                          << value.toString().toStdString() << "\n";
                                                if (parser.isSet("screenshot"))
                                                    QTimer::singleShot(500, &desktop, [&] {
                                                        desktop.screenshot(parser.value("screenshot"));
                                                        app.exit(0);
                                                    });
                                                else
                                                    app.exit(0);
                                            });
            });
            view->load(target);
            QTimer::singleShot(30000, &desktop, [&] {
                std::cerr << "FAIL: browser timeout\n";
                app.exit(1);
            });
        });
    if (test)
        QTimer::singleShot(50, &desktop, [&] {
            for (const QString &id :
                 QStringList{"files", "editor", "calc", "image", "settings", "monitor", "snake", "blocks",
                             "prism", "garden", "echo", "games", "about"})
                desktop.launch(id);
            auto *calculator = desktop.findChild<QLineEdit *>("calculatorInput");
            bool ok = loaded && calculator;
            if (calculator) {
                calculator->setText("2*(3+4)");
                QMetaObject::invokeMethod(calculator, "returnPressed");
                ok = ok && calculator->text() == "14";
            }
            auto *editor = desktop.findChild<QPlainTextEdit *>("editorText");
            if (editor) {
                editor->setPlainText("AURORA/86 integration test");
                auto *parent = editor->parentWidget();
                for (auto *b : parent->findChildren<QPushButton *>())
                    if (b->text() == "Save")
                        b->click();
                ok = ok && system.read("/home/guest/docs/untitled.txt") == "AURORA/86 integration test";
            } else
                ok = false;
            auto *terminal = desktop.findChild<QLineEdit *>("terminalInput");
            if (terminal) {
                terminal->setText("echo integration > /home/guest/docs/terminal.txt");
                QMetaObject::invokeMethod(terminal, "returnPressed");
                ok = ok && QString::fromUtf8(system.read("/home/guest/docs/terminal.txt")).trimmed() ==
                               "integration";
            } else
                ok = false;
            auto *area = desktop.findChild<QMdiArea *>("desktopArea");
            ok = ok && area && area->subWindowList().size() == 14 && system.processes().size() == 16;
            bool initiallyFull = desktop.isFullScreen();
            desktop.toggleFullscreen();
            ok = ok && desktop.isFullScreen() != initiallyFull;
            desktop.toggleFullscreen();
            if (area) {
                auto windows = area->subWindowList();
                if (!windows.isEmpty()) {
                    auto *last = windows.last();
                    for (auto *b : last->findChildren<QPushButton *>())
                        if (b->text() == "[]") {
                            b->click();
                            ok = ok && last->isMaximized();
                            b->click();
                            ok = ok && !last->isMaximized();
                        }
                    for (auto *b : last->findChildren<QPushButton *>())
                        if (b->text() == "X") {
                            b->click();
                            break;
                        }
                }
                ok = ok && system.processes().size() == 15;
            }
            std::cout
                << (ok ? "PASS" : "FAIL")
                << ": desktop, 14 applications, terminal, editor, calculator, processes and fullscreen\n";
            app.exit(ok ? 0 : 1);
        });
    return app.exec();
}
