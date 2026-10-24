#include "desktop.h"
#include "apps.h"
#include "creative_games.h"
#include "display.h"
#include "games.h"
#include "login.h"
#include "lua_apps.h"
#include "package_apps.h"
#include "packages.h"
#include "wallpaper.h"
#include <QtWidgets>
#include <functional>

namespace {
class WindowBar final : public QWidget {
    QMdiSubWindow *window_;
    QPoint last_;

  public:
    WindowBar(QMdiSubWindow *window) : window_(window) {
        setObjectName("windowBar");
        setFixedHeight(36);
        setStyleSheet("QWidget#windowBar{background:palette(button);border-bottom:2px solid "
                      "palette(text)}QLabel{color:palette(text);background:transparent}QPushButton{padding:0;"
                      "min-height:0;border:1px solid "
                      "palette(text);background:palette(button);color:palette(text)}QPushButton:hover{"
                      "background:palette(highlight);color:palette(highlighted-text)}");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(8, 3, 4, 3);
        row->setSpacing(4);
        auto *title = new QLabel(window->windowTitle());
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        row->addWidget(title, 1);
        for (const QString &name : QStringList{"_", "[]", "X"}) {
            auto *b = new QPushButton(name);
            b->setFixedSize(28, 28);
            row->addWidget(b);
            if (name == "_") {
                b->setToolTip("Minimize");
                connect(b, &QPushButton::clicked, window, &QWidget::showMinimized);
            } else if (name == "[]") {
                b->setToolTip("Maximize / restore");
                connect(b, &QPushButton::clicked, window, [window] {
                    if (window->isMaximized())
                        window->showNormal();
                    else
                        window->showMaximized();
                });
            } else {
                b->setToolTip("Close");
                connect(b, &QPushButton::clicked, window, &QWidget::close);
            }
        }
    }

  protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton)
            last_ = event->globalPosition().toPoint();
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (event->buttons() & Qt::LeftButton && !window_->isMaximized()) {
            QPoint now = event->globalPosition().toPoint();
            QPoint pos = window_->pos() + now - last_;
            pos.setX(qBound(0, pos.x(), qMax(0, window_->mdiArea()->viewport()->width() - 80)));
            pos.setY(qBound(0, pos.y(), qMax(0, window_->mdiArea()->viewport()->height() - 30)));
            window_->move(pos);
            last_ = now;
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            if (window_->isMaximized())
                window_->showNormal();
            else
                window_->showMaximized();
        }
    }
};
class AppWindow final : public QMdiSubWindow {
  public:
    std::function<void()> finished;
    QWidget *content = nullptr;
    void setContent(QWidget *app) {
        content = app;
        auto *host = new QWidget;
        auto *layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(new WindowBar(this));
        layout->addWidget(app, 1);
        auto *bottom = new QHBoxLayout;
        bottom->setContentsMargins(0, 0, 0, 0);
        bottom->addStretch();
        auto *grip = new QSizeGrip(host);
        grip->setFixedSize(13, 10);
        bottom->addWidget(grip);
        layout->addLayout(bottom);
        host->setFocusProxy(app);
        setWidget(host);
    }
    void closeEvent(QCloseEvent *e) override {
        if (!editorMayClose(content)) {
            e->ignore();
            return;
        }
        if (finished) {
            auto done = std::move(finished);
            done();
        }
        QMdiSubWindow::closeEvent(e);
    }
};
struct Application {
    const char *id;
    const char *title;
    int ram;
    QSize size;
};
const Application applications[] = {{"terminal", "Terminal / tty", 192, {760, 460}},
                                    {"files", "Disk 0 / File Manager", 128, {800, 470}},
                                    {"editor", "Writer / document", 128, {740, 470}},
                                    {"calc", "Calculator", 64, {370, 420}},
                                    {"image", "Atelier / images", 160, {650, 490}},
                                    {"settings", "Control Panel", 96, {790, 610}},
                                    {"monitor", "System Monitor", 64, {790, 450}},
                                    {"browser", "Network Mail", 768, {960, 600}},
                                    {"snake", "Circuit / signal lab", 128, {660, 500}},
                                    {"blocks", "Orbital Watch", 192, {660, 500}},
                                    {"prism", "Prism / optics lab", 128, {720, 560}},
                                    {"garden", "Orbital Garden", 128, {720, 570}},
                                    {"echo", "Echo / signal memory", 96, {720, 560}},
                                    {"games", "Games Room", 64, {760, 580}},
                                    {"packages", "Package Center", 128, {840, 580}},
                                    {"htop", "htop / A16 process viewer", 96, {840, 540}},
                                    {"lua", "Lua program", 192, {790, 580}},
                                    {"lua-studio", "Lua Studio / mods", 128, {900, 620}},
                                    {"pager", "less / document reader", 96, {740, 490}},
                                    {"about", "About this station", 32, {660, 470}}};
QDateTime simulatedTime(SimSystem *s) {
    QDateTime now = QDateTime::currentDateTime(), vintage = now;
    vintage.setDate(QDate(1986, now.date().month(),
                          qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
    return now.addSecs(s->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong());
}
} // namespace
Desktop::Desktop(SimSystem *s, bool skipBoot, QWidget *parent, bool bypassLogin)
    : QMainWindow(parent), system_(s), bypassLogin_(bypassLogin) {
    setWindowTitle("AURORA/86 — personal computer");
    QSize initial(1180, 820);
    if (auto *screen = QGuiApplication::primaryScreen())
        initial = initial.boundedTo(screen->availableGeometry().size() - QSize(40, 60));
    resize(initial.expandedTo(QSize(960, 640)));
    setMinimumSize(960, 640);
    screens_ = new QStackedWidget;
    setCentralWidget(screens_);
    menuBar()->hide();
    statusBar()->hide();
    resolutionTimer_ = new QTimer(this);
    resolutionTimer_->setSingleShot(true);
    resolutionTimer_->setInterval(200);
    connect(resolutionTimer_, &QTimer::timeout, this, &Desktop::recordResolution);
    speaker_ = new Speaker(s, this);
    applyTheme();
    qApp->installEventFilter(this);
    auto *full = new QShortcut(QKeySequence(Qt::Key_F11), this);
    full->setContext(Qt::ApplicationShortcut);
    connect(full, &QShortcut::activated, this, &Desktop::toggleFullscreen);
    auto *lock = new QShortcut(QKeySequence("Ctrl+Alt+L"), this);
    lock->setContext(Qt::ApplicationShortcut);
    connect(lock, &QShortcut::activated, this, &Desktop::lockSession);
    auto *terminal = new QShortcut(QKeySequence("Ctrl+Alt+T"), this);
    connect(terminal, &QShortcut::activated, this, [this] { launch("terminal"); });
    auto *close = new QShortcut(QKeySequence("Alt+F4"), this);
    connect(close, &QShortcut::activated, this, [this] {
        if (locked_)
            this->close();
        else if (area_ && area_->activeSubWindow())
            area_->activeSubWindow()->close();
        else
            this->close();
    });
    connect(s, &SimSystem::settingsChanged, this, [this] {
        const QString theme = system_->setting("theme", "green").toString();
        const int fontSize = qBound(14, system_->setting("fontSize", 16).toInt(), 20);
        const bool lines = system_->setting("scanlines", true).toBool();
        const QString path = system_->setting("wallpaperPath").toString();
        const QString mode = system_->setting("wallpaperMode", "cover").toString();
        const bool retro = system_->setting("wallpaperRetro", false).toBool();
        const bool repaint = theme != activeTheme_ || lines != activeScanlines_ ||
                             fontSize != activeFontSize_ || path != activeWallpaperPath_ ||
                             mode != activeWallpaperMode_ || retro != activeWallpaperRetro_;
        if (theme != activeTheme_ || fontSize != activeFontSize_)
            applyTheme();
        activeScanlines_ = lines;
        updateMeters();
        if (system_->setting("displayPreset", "auto").toString() != activeDisplayPreset_)
            applyDisplayMode();
        if (repaint)
            wallpaper();
    });
    connect(s, &SimSystem::changed, this, [this] {
        updateMeters();
        if (!activeWallpaperPath_.isEmpty())
            wallpaper();
    });
    connect(s, &SimSystem::processesChanged, this, [this] {
        updateMeters();
        if (!area_)
            return;
        const auto processes = system_->processes();
        for (auto *window : area_->subWindowList()) {
            int pid = window->property("pid").toInt();
            bool found = false;
            for (const auto &p : processes)
                if (p.pid == pid)
                    found = true;
            if (!found) {
                auto *app = static_cast<AppWindow *>(window);
                app->finished = {};
                window->deleteLater();
            }
        }
    });
    if (skipBoot || !s->setting("boot", true).toBool())
        finishBoot();
    else
        beginBoot();
}
void Desktop::applyTheme() {
    QString mode = system_->setting("theme", "green").toString();
    activeTheme_ = mode;
    activeFontSize_ = qBound(14, system_->setting("fontSize", 16).toInt(), 20);
    QFont readable("DejaVu Sans Mono");
    readable.setPixelSize(activeFontSize_);
    readable.setStyleStrategy(QFont::PreferAntialias);
    qApp->setFont(readable);
    activeScanlines_ = system_->setting("scanlines", true).toBool();
    QString dark = "#172b2a", accent = "#b6cc95", paper = "#e5e6c8", mid = "#547361";
    if (mode == "amber") {
        dark = "#302519";
        accent = "#e7b968";
        paper = "#ebdbac";
        mid = "#8d6c3f";
    } else if (mode == "paper") {
        dark = "#25292a";
        accent = "#d6d8ca";
        paper = "#e9e9da";
        mid = "#777c75";
    }
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(paper));
    palette.setColor(QPalette::WindowText, QColor(dark));
    palette.setColor(QPalette::Base, QColor(paper));
    palette.setColor(QPalette::AlternateBase, QColor(accent));
    palette.setColor(QPalette::Text, QColor(dark));
    palette.setColor(QPalette::Button, QColor(paper));
    palette.setColor(QPalette::ButtonText, QColor(dark));
    palette.setColor(QPalette::Highlight, QColor(dark));
    palette.setColor(QPalette::HighlightedText, QColor(paper));
    palette.setColor(QPalette::Light, QColor(paper));
    palette.setColor(QPalette::Dark, QColor(dark));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString(R"CSS(
 QWidget{font-family:'DejaVu Sans Mono';font-size:%5px;}
 QMainWindow{background:%1;} QMenuBar{background:%3;color:%1;border-bottom:2px solid %1;padding:3px;} QMenuBar::item{padding:6px 12px;} QMenuBar::item:selected,QMenu::item:selected{background:%1;color:%3;}
 QMenu{background:%3;border:2px solid %1;padding:4px;}QMenu::item{padding:7px 24px 7px 12px;}QMenu::separator{height:1px;background:%1;margin:4px;}
 QPushButton{background:%3;border:1px solid %1;border-bottom:3px solid %1;padding:6px 10px;min-height:16px;}QPushButton:hover{background:%2;}QPushButton:pressed{border-bottom:1px solid %1;padding-top:8px;}QPushButton:disabled{color:%4;}
 QLineEdit,QPlainTextEdit,QTextEdit,QTableWidget,QComboBox,QDateTimeEdit{background:%3;color:%1;border:1px solid %1;padding:4px;selection-background-color:%1;selection-color:%3;}
 QPlainTextEdit#terminalOutput,QWidget#terminal,QWidget#terminal QLineEdit,QWidget#terminal QLabel{background:%1;color:%2;border:none;}
 QTableWidget{gridline-color:%4;}QHeaderView::section{background:%2;color:%1;border:0;border-bottom:2px solid %1;padding:6px;}QTableWidget::item{padding:4px;}
 QStatusBar{background:%3;color:%1;border-top:2px solid %1;}QStatusBar::item{border:0;}
 QMdiSubWindow{background:%3;border:2px solid %1;}QMdiSubWindow::title{background:%1;color:%3;padding:5px;}
 QScrollBar:vertical{background:%3;width:14px;}QScrollBar::handle:vertical{background:%4;border:1px solid %1;min-height:18px;}QScrollBar:horizontal{background:%3;height:14px;}QScrollBar::handle:horizontal{background:%4;border:1px solid %1;min-width:18px;}
 QProgressBar{background:%3;border:1px solid %1;text-align:center;min-height:20px;}QProgressBar::chunk{background:%4;}
 QCheckBox{spacing:10px;padding:4px;}QCheckBox::indicator{width:14px;height:14px;border:1px solid %1;background:%3;}QCheckBox::indicator:checked{background:%4;}
 QToolTip{background:%3;color:%1;border:1px solid %1;}
 )CSS")
                            .arg(dark, accent, paper, mid, QString::number(activeFontSize_)));
}
void Desktop::beginBoot() {
    boot_ = new QWidget;
    boot_->setStyleSheet("background:#101e1c;color:#b6cc95");
    auto *l = new QVBoxLayout(boot_);
    l->setContentsMargins(70, 50, 70, 40);
    auto *brand = new QLabel("A U R O R A   /   8 6");
    QFont large = brand->font();
    large.setPixelSize(26);
    brand->setFont(large);
    brand->setStyleSheet("font-size:26px;color:#b6cc95;background:#101e1c");
    l->addWidget(brand);
    l->addWidget(new QLabel("A16 WORKSTATION / BOOT SYSTEM 1.04\nCOPYRIGHT (C) 1986 AURORA LABORATORIES\n"));
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setFrameStyle(QFrame::NoFrame);
    log->setObjectName("bootLog");
    l->addWidget(log, 1);
    auto *bar = new QProgressBar;
    bar->setRange(0, 13);
    bar->setTextVisible(false);
    l->addWidget(bar);
    l->addWidget(new QLabel("ENTER / SPACE  continue     |     F11  fullscreen"));
    screens_->addWidget(boot_);
    screens_->setCurrentWidget(boot_);
    QStringList steps = {"[ ROM ] A16 / 16-bit bus / 8 MHz oscillator",
                         "[ RAM ] 0000 ... 1024 KiB : OK",
                         "[ RAM ] 1024 ... 2048 KiB : OK",
                         "[ RAM ] 2048 ... 4096 KiB : OK",
                         "[ VID ] LUME adapter / 960 x 640 / 4 tones",
                         "[ I/O ] serial keyboard and pointer : present",
                         "[ DSK ] M2 magnetic drive / 2048 KiB : OK",
                         "[ VFS ] mounting persistent virtual disk ...",
                         "[ MOD ] scheduler, permissions, clock, speaker",
                         "[ NET ] protected network bridge / available on demand",
                         "[ SYS ] AURORA 1.2 / kernel ready",
                         "[ USR ] guest session / /home/guest",
                         "[ GUI ] starting desktop environment ..."};
    bootTimer_ = new QTimer(this);
    connect(bootTimer_, &QTimer::timeout, this, [this, log, bar, steps] {
        if (bootStep_ >= steps.size()) {
            bootTimer_->stop();
            finishBoot();
            return;
        }
        log->appendPlainText(steps[bootStep_]);
        bar->setValue(++bootStep_);
    });
    bootTimer_->start(240);
}
void Desktop::startDesktop() {
    if (started_)
        return;
    started_ = true;
    if (bootTimer_)
        bootTimer_->stop();
    locked_ = false;
    menuBar()->show();
    statusBar()->show();
    area_ = new QMdiArea;
    area_->setObjectName("desktopArea");
    area_->setOption(QMdiArea::DontMaximizeSubWindowOnActivation);
    area_->setViewMode(QMdiArea::SubWindowView);
    area_->setFrameShape(QFrame::NoFrame);
    area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    area_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    display_ = new QScrollArea;
    display_->setObjectName("displayFrame");
    display_->setFrameShape(QFrame::NoFrame);
    display_->setAlignment(Qt::AlignCenter);
    display_->setWidget(area_);
    screens_->addWidget(display_);
    screens_->setCurrentWidget(display_);
    applyDisplayMode();
    area_->viewport()->installEventFilter(this);
    auto action = [this](QMenu *menu, const QString &label, const QString &id) {
        auto *a = menu->addAction(appIcon(id, QColor("#35574b")), label);
        connect(a, &QAction::triggered, this, [this, id] { launch(id); });
    };
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    auto *systemMenu = bar->addMenu("AURORA / 86");
    action(systemMenu, "About this station", "about");
    action(systemMenu, "Control Panel", "settings");
    auto *personalize = systemMenu->addAction("Wallpaper...");
    connect(personalize, &QAction::triggered, this, [this] { launch("settings", "wallpaper"); });
    action(systemMenu, "System Monitor", "monitor");
    systemMenu->addSeparator();
    auto *lockAction = systemMenu->addAction("Lock session    Ctrl+Alt+L");
    connect(lockAction, &QAction::triggered, this, &Desktop::lockSession);
    auto *profile = systemMenu->addAction("auroraSYNC profile...");
    connect(profile, &QAction::triggered, this, [this] { launch("settings", "identity"); });
    auto *screenSettings = systemMenu->addAction("Display settings...");
    connect(screenSettings, &QAction::triggered, this, [this] { launch("settings", "display"); });
    auto *full = systemMenu->addAction("Fullscreen    F11");
    connect(full, &QAction::triggered, this, &Desktop::toggleFullscreen);
    auto *quit = systemMenu->addAction("Shut down");
    connect(quit, &QAction::triggered, this, &Desktop::close);
    auto *fileMenu = bar->addMenu("File");
    action(fileMenu, "New document", "editor");
    action(fileMenu, "Explore disk", "files");
    auto *apps = bar->addMenu("Applications");
    for (const auto &a : applications)
        if (QStringList{"terminal", "files", "editor", "calc", "image", "browser", "packages", "lua-studio"}
                .contains(a.id))
            action(apps, QString::fromUtf8(a.title), a.id);
    auto *games = bar->addMenu("Games");
    action(games, "Games Room", "games");
    games->addSeparator();
    action(games, "Prism", "prism");
    action(games, "Orbital Garden", "garden");
    action(games, "Echo", "echo");
    action(games, "Circuit", "snake");
    action(games, "Orbital Watch", "blocks");
    auto *windows = bar->addMenu("Windows");
    auto *cascade = windows->addAction("Cascade");
    connect(cascade, &QAction::triggered, area_, &QMdiArea::cascadeSubWindows);
    auto *tile = windows->addAction("Tile");
    connect(tile, &QAction::triggered, area_, &QMdiArea::tileSubWindows);
    windows->addSeparator();
    connect(windows, &QMenu::aboutToShow, this, [this, windows] {
        const auto acts = windows->actions();
        for (int i = 3; i < acts.size(); ++i)
            windows->removeAction(acts[i]), acts[i]->deleteLater();
        for (auto *w : area_->subWindowList()) {
            auto *a = windows->addAction(w->windowTitle());
            connect(a, &QAction::triggered, this, [this, w] {
                w->showNormal();
                area_->setActiveSubWindow(w);
            });
        }
    });
    clock_ = new QLabel;
    clock_->setContentsMargins(12, 0, 12, 0);
    bar->setCornerWidget(clock_, Qt::TopRightCorner);
    meter_ = new QLabel;
    statusBar()->addWidget(new QLabel("  A16  /  DISK 0: ONLINE"));
    statusBar()->addPermanentWidget(meter_);
    statusBar()->setSizeGripEnabled(false);
    const QStringList desktopIds = {"terminal", "files", "editor", "browser", "games", "settings"};
    const QStringList names = {"Terminal", "Disk 0:", "Writer", "Network", "Games", "Control"};
    for (int i = 0; i < desktopIds.size(); ++i) {
        auto *b = new QToolButton(area_->viewport());
        b->setObjectName("desktopIcon");
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIcon(appIcon(desktopIds[i]));
        b->setIconSize({32, 32});
        b->setText(names[i]);
        b->setGeometry(14, 20 + i * 91, 128, 84);
        b->setStyleSheet("QToolButton{color:#e5e6c8;background:#172b2a;border:1px solid "
                         "#547361;padding:7px}QToolButton:hover{background:#35574b;border:1px dotted "
                         "#b6cc95}QToolButton:pressed{background:#547361}");
        connect(b, &QToolButton::clicked, this, [this, id = desktopIds[i]] { launch(id); });
        b->show();
    }
    area_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(area_->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto *item = menu->addAction("Customize wallpaper...");
        connect(item, &QAction::triggered, this, [this] { launch("settings", "wallpaper"); });
        menu->popup(area_->viewport()->mapToGlobal(pos));
    });
    auto *timer = new QTimer(this);
    timer->start(1000);
    connect(timer, &QTimer::timeout, this, &Desktop::updateMeters);
    updateMeters();
    wallpaper();
    launch("terminal");
    speaker_->play("boot");
    if (!system_->lastError().isEmpty())
        QTimer::singleShot(100, this,
                           [this] { QMessageBox::warning(this, "Virtual disk", system_->lastError()); });
}
void Desktop::launch(const QString &requested, const QString &path) {
    if (requested == "lock") {
        lockSession();
        return;
    }
    if (!started_ || locked_)
        return;
    QString id = requested;
    if (id == "circuit")
        id = "snake";
    if (id == "meteor")
        id = "blocks";
    if (id == "edit")
        id = "editor";
    if (id == "close") {
        if (area_->activeSubWindow())
            area_->activeSubWindow()->close();
        return;
    }
    const Application *spec = nullptr;
    for (const auto &a : applications)
        if (id == a.id) {
            spec = &a;
            break;
        }
    if (!spec) {
        QMessageBox::warning(this, "Application", "Unknown application: " + id);
        return;
    }
    if ((id == "htop" || id == "pager") &&
        !PackageManager(system_).isInstalled(id == "htop" ? "htop" : "less")) {
        speaker_->play("error");
        QMessageBox::information(this, "Package not installed",
                                 "Install this tool first:\naurora -i " +
                                     QString(id == "htop" ? "htop" : "less"));
        return;
    }
    const int pid = system_->startProcess(id, spec->ram);
    if (pid < 0) {
        speaker_->play("error");
        QMessageBox::warning(this, "Not enough memory", system_->lastError());
        return;
    }
    AppLauncher open = [this](const QString &a, const QString &p) { launch(a, p); };
    QWidget *content = nullptr;
    if (id == "terminal")
        content = createTerminal(system_, speaker_, open);
    else if (id == "files")
        content = createFiles(system_, speaker_, open, path);
    else if (id == "editor")
        content = createEditor(system_, speaker_, path);
    else if (id == "calc")
        content = createCalculator();
    else if (id == "image")
        content = createImageViewer(system_, path);
    else if (id == "settings") {
        content = createSettings(system_);
        auto *tabs = content->findChild<QTabWidget *>("controlTabs");
        if (path == "wallpaper")
            tabs->setCurrentIndex(1);
        else if (path == "identity")
            tabs->setCurrentIndex(2);
        else if (path == "display")
            tabs->setCurrentIndex(3);
    } else if (id == "monitor")
        content = createMonitor(system_);
    else if (id == "browser")
        content = createBrowser(system_, speaker_, path);
    else if (id == "packages")
        content = createPackageManager(system_, open);
    else if (id == "htop")
        content = createHtop(system_);
    else if (id == "pager")
        content = createTextPager(system_, path);
    else if (id == "lua")
        content = createLuaProgram(system_, path);
    else if (id == "lua-studio")
        content = createLuaStudio(system_, open);
    else if (id == "snake")
        content = createCircuitGame(system_);
    else if (id == "blocks")
        content = createMeteorGame(system_);
    else if (id == "prism")
        content = createPrismGame(system_);
    else if (id == "garden")
        content = createGardenGame(system_);
    else if (id == "echo")
        content = createEchoGame(system_);
    else if (id == "games")
        content = createGameLibrary(system_, open);
    else
        content = createAbout();
    auto *window = new AppWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(QString::fromUtf8(spec->title) + QString("  [%1]").arg(pid));
    window->setWindowIcon(appIcon(id));
    window->setProperty("pid", pid);
    window->setObjectName("app_" + id);
    window->setWindowFlags(Qt::SubWindow | Qt::FramelessWindowHint);
    window->setContent(content);
    area_->addSubWindow(window);
    window->resize(spec->size.boundedTo(area_->viewport()->size() - QSize(25, 35)));
    int count = area_->subWindowList().size() - 1;
    const int preferredX = 155 + (count % 5) * 18, preferredY = 135 + (count % 5) * 18;
    window->move(qMax(0, qMin(preferredX, area_->viewport()->width() - window->width() - 10)),
                 qMax(0, qMin(preferredY, area_->viewport()->height() - window->height() - 10)));
    window->finished = [this, pid] { system_->stopProcess(pid); };
    window->show();
    if (!isVisible())
        QTimer::singleShot(0, window, [this, window, spec] {
            const QSize available = area_->viewport()->size();
            window->resize(spec->size.boundedTo(available - QSize(25, 35)));
            window->move(qMax(0, qMin(155, available.width() - window->width() - 10)),
                         qMax(0, qMin(135, available.height() - window->height() - 10)));
        });
    area_->setActiveSubWindow(window);
    content->setFocus();
    speaker_->play("operation");
}
void Desktop::updateMeters() {
    if (!clock_)
        return;
    clock_->setText(simulatedTime(system_).toString("dd/MM/yyyy  HH:mm:ss"));
    meter_->setText(QString("%1 KiB FREE   /   %2 PROCESSES   ")
                        .arg(SimSystem::MemoryKiB - system_->usedMemoryKiB())
                        .arg(system_->processes().size()));
}
void Desktop::wallpaper() {
    if (!area_)
        return;
    QSize size = area_->viewport()->size();
    if (size.isEmpty())
        return;
    QString mode = system_->setting("theme", "green").toString();
    QColor base = mode == "amber"   ? QColor("#342b20")
                  : mode == "paper" ? QColor("#303632")
                                    : QColor("#203d35");
    QColor dot = mode == "amber"   ? QColor("#514332")
                 : mode == "paper" ? QColor("#3e4540")
                                   : QColor("#2b4c40");
    QColor ink = mode == "amber" ? QColor("#a18a5a") : QColor("#63816b");
    QPixmap map(size);
    map.fill(base);
    QPainter p(&map);
    const QString wallpaperPath = system_->setting("wallpaperPath").toString();
    const QString wallpaperMode = system_->setting("wallpaperMode", "cover").toString();
    const bool wallpaperRetro = system_->setting("wallpaperRetro", false).toBool();
    const QByteArray imageBytes = wallpaperPath.isEmpty() ? QByteArray() : system_->read(wallpaperPath);
    if (wallpaperPath != activeWallpaperPath_ || imageBytes != wallpaperBytes_) {
        wallpaperBytes_ = imageBytes;
        wallpaperImage_ = wallpaperPath.isEmpty() ? QImage() : readWallpaper(system_);
    }
    activeWallpaperPath_ = wallpaperPath;
    activeWallpaperMode_ = wallpaperMode;
    activeWallpaperRetro_ = wallpaperRetro;
    if (!wallpaperImage_.isNull()) {
        QImage picture = wallpaperImage_;
        if (wallpaperRetro)
            picture = applyWallpaperRetro(picture);
        QSize target = picture.size();
        if (wallpaperMode == "stretch")
            target = size;
        else if (wallpaperMode != "center")
            target.scale(size, wallpaperMode == "fit" ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        p.setRenderHint(QPainter::SmoothPixmapTransform, !wallpaperRetro);
        p.drawImage(
            QRect(QPoint((size.width() - target.width()) / 2, (size.height() - target.height()) / 2), target),
            picture);
        if (activeScanlines_) {
            p.setPen(QColor(0, 0, 0, 20));
            for (int y = 0; y < size.height(); y += 3)
                p.drawLine(0, y, size.width(), y);
        }
        p.end();
        area_->setBackground(QBrush(map));
        return;
    }
    p.setPen(dot);
    for (int y = 0; y < size.height(); y += 4)
        for (int x = (y % 8) ? 0 : 2; x < size.width(); x += 4)
            p.drawPoint(x, y);
    if (system_->setting("scanlines", true).toBool()) {
        p.setPen(QColor(0, 0, 0, 20));
        for (int y = 0; y < size.height(); y += 3)
            p.drawLine(0, y, size.width(), y);
    }
    p.setPen(ink);
    QFont logo = font();
    logo.setPixelSize(26);
    p.setFont(logo);
    int x = qMax(170, size.width() - 395);
    p.drawText(x, 54, "A U R O R A / 8 6");
    p.setFont(font());
    p.drawText(x, 79, "PERSONAL STATION / A16 SERIES");
    p.drawLine(x, 91, size.width() - 40, 91);
    p.drawText(x, 113, "A SMALL STATION.");
    p.drawText(x, 132, "AN ENTIRE WORLD.");
    int horizon = size.height() - 95;
    int center = size.width() - 200;
    p.setPen(ink);
    p.drawLine(155, horizon, size.width(), horizon);
    for (int i = -5; i <= 5; ++i)
        p.drawLine(center + i * 15, horizon, center + i * 90, size.height());
    for (int y = 12; y < 90; y += y / 2 + 6)
        p.drawLine(155, horizon + y, size.width(), horizon + y);
    p.drawPolyline(QPolygon({{size.width() - 420, horizon},
                             {size.width() - 300, horizon - 82},
                             {size.width() - 225, horizon - 31},
                             {size.width() - 140, horizon - 128},
                             {size.width() - 30, horizon}}));
    p.drawEllipse(QRect(size.width() - 218, horizon - 165, 58, 58));
    p.drawText(160, size.height() - 22, "AURORA SYSTEMS / OCT. 1986   •   16 BITS OF POSSIBILITY");
    p.end();
    area_->setBackground(QBrush(map));
}
void Desktop::toggleFullscreen() {
    if (isFullScreen())
        showNormal();
    else
        showFullScreen();
}
bool Desktop::eventFilter(QObject *object, QEvent *event) {
    if (boot_ && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Space || key->key() == Qt::Key_Escape) {
            finishBoot();
            return true;
        }
    }
    if (area_ && object == area_->viewport() && event->type() == QEvent::Resize) {
        QTimer::singleShot(0, this, &Desktop::wallpaper);
        resolutionTimer_->start();
    }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (!key->isAutoRepeat() && !key->text().isEmpty())
            speaker_->play("key");
    }
    return QMainWindow::eventFilter(object, event);
}
void Desktop::closeEvent(QCloseEvent *event) {
    if (area_) {
        for (auto *w : area_->subWindowList())
            if (!editorMayClose(static_cast<AppWindow *>(w)->content)) {
                event->ignore();
                return;
            }
    }
    if (!system_->save() && !system_->lastError().isEmpty())
        QMessageBox::warning(this, "Saving system state", system_->lastError());
    event->accept();
}
void Desktop::screenshot(const QString &path) { grab().save(path); }

void Desktop::finishBoot() {
    if (bootTimer_)
        bootTimer_->stop();
    if (boot_) {
        screens_->removeWidget(boot_);
        boot_->deleteLater();
        boot_ = nullptr;
    }
    if (bypassLogin_ || !system_->setting("loginEnabled", true).toBool())
        startDesktop();
    else
        showLogin();
}
void Desktop::showLogin() {
    locked_ = true;
    menuBar()->hide();
    statusBar()->hide();
    if (login_) {
        screens_->removeWidget(login_);
        login_->deleteLater();
    }
    login_ = new LoginScreen(system_);
    screens_->addWidget(login_);
    screens_->setCurrentWidget(login_);
    connect(login_, &LoginScreen::authenticated, this, [this] {
        auto *finished = login_;
        login_ = nullptr;
        screens_->removeWidget(finished);
        finished->setObjectName("retiredLogin");
        finished->deleteLater();
        locked_ = false;
        if (!started_)
            startDesktop();
        else {
            screens_->setCurrentWidget(display_);
            menuBar()->show();
            statusBar()->show();
        }
        speaker_->play("operation");
    });
    connect(login_, &LoginScreen::shutdownRequested, this, &Desktop::close);
}
void Desktop::lockSession() {
    if (started_ && !locked_)
        showLogin();
}
void Desktop::applyDisplayMode() {
    activeDisplayPreset_ = system_->setting("displayPreset", "auto").toString();
    if (!display_)
        return;
    const QSize size = displayPresetSize(activeDisplayPreset_);
    if (size.isValid()) {
        display_->setWidgetResizable(false);
        area_->setFixedSize(size);
    } else {
        area_->setMinimumSize(0, 0);
        area_->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        display_->setWidgetResizable(true);
    }
    resolutionTimer_->start();
}
void Desktop::recordResolution() {
    if (!area_)
        return;
    const QSize size = area_->viewport()->size();
    if (size.isEmpty())
        return;
    if (system_->setting("resolutionWidth", 0).toInt() != size.width() ||
        system_->setting("resolutionHeight", 0).toInt() != size.height())
        system_->setSettings({{"resolutionWidth", size.width()}, {"resolutionHeight", size.height()}});
}
