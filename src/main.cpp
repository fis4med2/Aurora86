#include "desktop.h"
#include "system.h"
#include <QImageReader>
#include <QTemporaryDir>
#include <QWebEngineView>
#include <QtWidgets>
#include <iostream>
int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);
    app.setApplicationName("AURORA86");
    app.setDesktopFileName("io.github.fis4med2.Aurora86");
    app.setOrganizationName("Aurora");
    app.setApplicationVersion("1.1.0");
    app.setStyle("Fusion");
    QImageReader::setAllocationLimit(16);
    QFontDatabase::addApplicationFont(":/fonts/AuroraFixed.ttf");
    QFontDatabase::addApplicationFont(":/fonts/DejaVuSansMono.ttf");
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(16);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);
    QCommandLineParser parser;
    parser.setApplicationDescription("AURORA/86 — estação pessoal fictícia de 1986");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({{{"f", "fullscreen"}, "Iniciar em tela cheia"},
                       {"skip-boot", "Pular autoteste"},
                       {"state-dir", "Diretório real reservado ao estado do simulador", "diretorio"},
                       {"self-test", "Verificar aplicativos sem rede e encerrar"},
                       {"browser-check", "Verificar navegação HTTP/HTTPS e encerrar", "url"},
                       {"screenshot", "Gravar captura do desktop e encerrar", "arquivo"},
                       {"boot-screenshot", "Gravar captura durante boot e encerrar", "arquivo"}});
    parser.process(app);
    QTemporaryDir temporary;
    const bool test = parser.isSet("self-test");
    QString state = test ? temporary.path() : parser.value("state-dir");
    if (state.isEmpty())
        state = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(state)) {
        QMessageBox::warning(nullptr, "Estado", "Não foi possível criar o diretório do simulador.");
        return 1;
    }
    QLockFile sessionLock(state + "/session.lock");
    sessionLock.setStaleLockTime(0);
    if (!sessionLock.tryLock()) {
        QMessageBox::information(nullptr, "AURORA/86",
                                 "Este disco virtual já está aberto em outra sessão. Use outro --state-dir "
                                 "para uma estação separada.");
        return 1;
    }
    SimSystem system(state);
    const bool loaded = system.load();
    const QString loadError = system.lastError();
    Desktop desktop(&system, test || parser.isSet("skip-boot") || parser.isSet("screenshot") ||
                                 parser.isSet("browser-check"));
    if (parser.isSet("fullscreen"))
        desktop.showFullScreen();
    else
        desktop.show();
    if (!loaded && !test)
        QTimer::singleShot(250, &desktop, [&desktop, loadError] {
            QMessageBox::warning(
                &desktop, "Estado preservado",
                loadError +
                    "\nO arquivo original foi preservado. Use outro --state-dir para uma sessão nova.");
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
                    std::cerr << "FAIL: navegação real\n";
                    app.exit(1);
                    return;
                }
                view->page()->runJavaScript("document.title + '|' + document.body.innerText.length",
                                            [&, view](const QVariant &value) {
                                                std::cout << "PASS: Chromium HTTP/HTTPS e JavaScript / "
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
                std::cerr << "FAIL: timeout navegador\n";
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
                editor->setPlainText("teste integrado AURORA/86");
                auto *parent = editor->parentWidget();
                for (auto *b : parent->findChildren<QPushButton *>())
                    if (b->text() == "Gravar")
                        b->click();
                ok = ok && system.read("/home/guest/docs/sem-titulo.txt") == "teste integrado AURORA/86";
            } else
                ok = false;
            auto *terminal = desktop.findChild<QLineEdit *>("terminalInput");
            if (terminal) {
                terminal->setText("echo integração > /home/guest/docs/terminal.txt");
                QMetaObject::invokeMethod(terminal, "returnPressed");
                ok = ok && QString::fromUtf8(system.read("/home/guest/docs/terminal.txt")).trimmed() ==
                               "integração";
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
            std::cout << (ok ? "PASS" : "FAIL")
                      << ": desktop, 14 aplicativos, terminal, editor, calculadora, processos e tela cheia\n";
            app.exit(ok ? 0 : 1);
        });
    return app.exec();
}
