#include "desktop.h"
#include "screensaver.h"
#include "apps.h"
#include "creative_games.h"
#include "display.h"
#include "games.h"
#include "hellgate.h"
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
        setFixedHeight(40);
        setStyleSheet("QWidget#windowBar{background:palette(button);border-bottom:1px solid palette(mid);"
                      "border-top-left-radius:9px;border-top-right-radius:9px}"
                      "QWidget#windowBar[active=true]{background:palette(mid);border-bottom-color:palette(highlight)}"
                      "QLabel{color:palette(text);background:transparent;font-size:13px;font-weight:600}"
                      "QPushButton{padding:0;min-height:0;border:0;border-radius:5px;"
                      "background:transparent;color:palette(text);font-size:16px}"
                      "QPushButton:hover{background:palette(mid)}"
                      "QPushButton#windowClose:hover{background:#d86667;color:white}");
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
                b->setObjectName("windowClose");
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
                                    {"monitor", "System Center", 96, {900, 650}},
                                    {"browser", "Network Mail", 768, {960, 600}},
                                    {"snake", "Circuit / signal lab", 128, {660, 500}},
                                    {"blocks", "Orbital Watch", 192, {660, 500}},
                                    {"prism", "Prism / optics lab", 128, {720, 560}},
                                    {"garden", "Orbital Garden", 128, {720, 570}},
                                    {"echo", "Echo / signal memory", 96, {720, 560}},
                                    {"hellgate", "HELLGATE / corridor 13", 512, {860, 640}},
                                    {"doom", "DOOM / native engine", 768, {940, 730}},
                                    {"games", "Games Room", 64, {820, 640}},
                                    {"packages", "Package Center", 128, {840, 580}},
                                    {"htop", "htop / A16 process viewer", 96, {840, 540}},
                                    {"lua", "Lua program", 192, {790, 580}},
                                    {"lua-studio", "Lua Studio / mods", 128, {900, 620}},
                                    {"pager", "less / document reader", 96, {740, 490}},
                                    {"handbook", "Station handbook", 96, {850, 650}},
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
    idleTimer_ = new QTimer(this);
    idleTimer_->setInterval(5000);
    connect(idleTimer_, &QTimer::timeout, this, &Desktop::checkIdle);
    idleTimer_->start();
    lastActivity_ = QDateTime::currentMSecsSinceEpoch();
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
    auto *launcher = new QShortcut(QKeySequence("Ctrl+Space"), this);
    launcher->setObjectName("launcherShortcut");
    connect(launcher, &QShortcut::activated, this, &Desktop::showLauncher);
    auto *help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    help->setObjectName("handbookShortcut");
    connect(help, &QShortcut::activated, this, [this] { launch("handbook"); });
    auto *capture = new QShortcut(QKeySequence(Qt::Key_Print), this);
    capture->setObjectName("screenshotShortcut");
    connect(capture, &QShortcut::activated, this, [this] { captureVirtualScreen(); });
    auto *reference = new QShortcut(QKeySequence("Shift+F1"), this);
    reference->setObjectName("referenceShortcut");
    reference->setContext(Qt::ApplicationShortcut);
    connect(reference, &QShortcut::activated, this, &Desktop::showKeyboardReference);
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
        const bool lines = system_->setting("scanlines", false).toBool();
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
Desktop::~Desktop() {
    // QWidget tears down its children after Desktop's members. Stop callbacks first:
    // QMdiArea emits activation changes while destroying its subwindows.
    qApp->removeEventFilter(this);
    disconnect(system_, nullptr, this, nullptr);
    for (auto *timer : findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly)) {
        timer->stop();
        disconnect(timer, nullptr, this, nullptr);
    }
    if (area_) {
        disconnect(area_, nullptr, this, nullptr);
        area_->viewport()->removeEventFilter(this);
        for (auto *window : area_->subWindowList())
            static_cast<AppWindow *>(window)->finished = {};
    }
    area_ = nullptr;
    taskbarLayout_ = nullptr;
    taskbar_ = nullptr;
    clock_ = nullptr;
    meter_ = nullptr;
}
void Desktop::applyTheme() {
    const QString mode = system_->setting("theme", "green").toString();
    activeTheme_ = mode;
    activeFontSize_ = qBound(14, system_->setting("fontSize", 16).toInt(), 20);
    QFont readable("DejaVu Sans");
    readable.setPixelSize(activeFontSize_);
    readable.setStyleStrategy(QFont::PreferAntialias);
    qApp->setFont(readable);
    activeScanlines_ = system_->setting("scanlines", false).toBool();
    QString base = "#0e1928", panel = "#18283b", ink = "#dce9f2", accent = "#70e0cd", edge = "#31465b";
    if (mode == "amber") {
        base = "#211b20"; panel = "#322731"; ink = "#f2e4d7"; accent = "#efbd83"; edge = "#5b4851";
    } else if (mode == "paper") {
        base = "#eef1ee"; panel = "#ffffff"; ink = "#243c43"; accent = "#26766d"; edge = "#c7d3d2";
    }
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(base));
    palette.setColor(QPalette::WindowText, QColor(ink));
    palette.setColor(QPalette::Base, QColor(base));
    palette.setColor(QPalette::AlternateBase, QColor(panel));
    palette.setColor(QPalette::Text, QColor(ink));
    palette.setColor(QPalette::Button, QColor(panel));
    palette.setColor(QPalette::ButtonText, QColor(ink));
    palette.setColor(QPalette::Highlight, QColor(accent));
    palette.setColor(QPalette::HighlightedText, QColor(base));
    palette.setColor(QPalette::Light, QColor(edge));
    palette.setColor(QPalette::Mid, QColor(edge));
    palette.setColor(QPalette::Dark, QColor(base));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(edge));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(edge));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString(R"CSS(
 QWidget{font-family:'DejaVu Sans';font-size:%6px;}
 QMainWindow{background:%1;}
 QMenuBar{background:%1;color:%3;border-bottom:1px solid %5;padding:5px 8px;}
 QMenuBar::item{padding:7px 12px;border-radius:5px;}QMenuBar::item:selected,QMenu::item:selected{background:%2;color:%4;}
 QMenu{background:%2;color:%3;border:1px solid %5;border-radius:8px;padding:7px;}
 QMenu::item{padding:8px 26px 8px 12px;border-radius:4px;}QMenu::separator{height:1px;background:%5;margin:6px;}
 QPushButton,QToolButton{background:%2;color:%3;border:1px solid %5;border-radius:6px;padding:7px 12px;min-height:18px;}
 QPushButton:hover,QToolButton:hover{border-color:%4;color:%4;}QPushButton:pressed,QToolButton:pressed{background:%5;}
 QPushButton:default{background:%4;color:%1;border-color:%4;font-weight:600;}
 QPushButton:disabled{color:%5;}
 QLineEdit,QPlainTextEdit,QTextEdit,QTextBrowser,QTableWidget,QListWidget,QComboBox,QDateTimeEdit,QSpinBox,QDoubleSpinBox{background:%1;color:%3;border:1px solid %5;border-radius:5px;padding:6px;selection-background-color:%4;selection-color:%1;}
 QLineEdit:focus,QPlainTextEdit:focus,QComboBox:focus{border-color:%4;}
 QWidget#terminal,QWidget#terminal QPlainTextEdit,QWidget#terminal QLineEdit,QWidget#terminal QLabel{font-family:'DejaVu Sans Mono';background:%1;color:%4;border:none;}
 QTableWidget{gridline-color:%2;}QHeaderView::section{background:%2;color:%3;border:0;border-bottom:1px solid %5;padding:8px;text-align:left;}QTableWidget::item{padding:5px;}
 QStatusBar{background:%1;color:%3;border-top:1px solid %5;padding:4px;}QStatusBar::item{border:0;}
 QMdiSubWindow{background:%1;border:1px solid %5;border-radius:9px;}QMdiSubWindow:focus{border-color:%4;}
 QToolButton#taskbarButton{background:transparent;border:0;border-radius:5px;padding:6px 10px;font-size:12px;}
 QToolButton#taskbarButton:checked{background:%2;color:%4;border-bottom:2px solid %4;}
 QScrollBar:vertical{background:transparent;width:10px;margin:2px;}QScrollBar::handle:vertical{background:%5;border-radius:4px;min-height:24px;}
 QScrollBar:horizontal{background:transparent;height:10px;margin:2px;}QScrollBar::handle:horizontal{background:%5;border-radius:4px;min-width:24px;}
 QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}
 QProgressBar{background:%2;border:0;border-radius:4px;text-align:center;min-height:10px;}QProgressBar::chunk{background:%4;border-radius:4px;}
 QCheckBox{spacing:9px;padding:4px;}QCheckBox::indicator{width:16px;height:16px;border:1px solid %5;border-radius:4px;background:%1;}QCheckBox::indicator:checked{background:%4;border-color:%4;}
 QTabWidget::pane{border:1px solid %5;border-radius:6px;}QTabBar::tab{background:%1;color:%3;padding:10px 12px;border-bottom:2px solid transparent;}QTabBar::tab:selected{color:%4;border-bottom:2px solid %4;}
 QGroupBox{border:1px solid %5;border-radius:7px;margin-top:14px;padding:12px;}QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 6px;color:%4;}
 QToolTip{background:%2;color:%3;border:1px solid %5;padding:6px;}
 )CSS").arg(base, panel, ink, accent, edge, QString::number(activeFontSize_)));
}
void Desktop::beginBoot() {
    boot_ = new QWidget;
    boot_->setStyleSheet("background:#0b1421;color:#70e0cd;font-family:DejaVu Sans Mono");
    auto *l = new QVBoxLayout(boot_);
    l->setContentsMargins(70, 50, 70, 40);
    auto *brand = new QLabel("A U R O R A   /   8 6");
    QFont large = brand->font();
    large.setPixelSize(26);
    brand->setFont(large);
    brand->setStyleSheet("font-size:32px;font-weight:600;color:#dce9f2;background:#0b1421");
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
                         "[ SYS ] AURORA 1.6 / kernel ready",
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
        auto *a = menu->addAction(appIcon(id, QColor("#70e0cd")), label);
        connect(a, &QAction::triggered, this, [this, id] { launch(id); });
    };
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    auto *systemMenu = bar->addMenu("AURORA / 86");
    auto *launcherAction = systemMenu->addAction("Launch an application    Ctrl+Space");
    connect(launcherAction, &QAction::triggered, this, &Desktop::showLauncher);
    systemMenu->addSeparator();
    action(systemMenu, "About this station", "about");
    action(systemMenu, "Control Panel", "settings");
    auto *personalize = systemMenu->addAction("Wallpaper...");
    connect(personalize, &QAction::triggered, this, [this] { launch("settings", "wallpaper"); });
    action(systemMenu, "System Center", "monitor");
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
    auto *capture = fileMenu->addAction("Capture screen    Print Screen");
    connect(capture, &QAction::triggered, this, [this] { captureVirtualScreen(); });
    auto *apps = bar->addMenu("Applications");
    for (const auto &a : applications)
        if (QStringList{"terminal", "files", "editor", "calc", "image", "browser", "packages", "lua-studio"}
                .contains(a.id))
            action(apps, QString::fromUtf8(a.title), a.id);
    auto *games = bar->addMenu("Games");
    action(games, "DOOM / original engine", "doom");
    action(games, "Games Room", "games");
    games->addSeparator();
    action(games, "Prism", "prism");
    action(games, "Orbital Garden", "garden");
    action(games, "Echo", "echo");
    action(games, "Circuit", "snake");
    action(games, "Orbital Watch", "blocks");
    games->addSeparator();
    action(games, "HELLGATE / corridor 13", "hellgate");
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
    auto *help = bar->addMenu("Help");
    action(help, "Station handbook    F1", "handbook");
    auto *reference = help->addAction("Keyboard reference    Shift+F1");
    connect(reference, &QAction::triggered, this, &Desktop::showKeyboardReference);
    action(help, "About this station", "about");
    clock_ = new QLabel;
    clock_->setContentsMargins(12, 0, 12, 0);
    bar->setCornerWidget(clock_, Qt::TopRightCorner);
    meter_ = new QLabel;
    auto *launcherButton = new QToolButton;
    launcherButton->setObjectName("launcherButton");
    launcherButton->setText("◈  Launch");
    launcherButton->setToolTip("Find an application · Ctrl+Space");
    connect(launcherButton, &QToolButton::clicked, this, &Desktop::showLauncher);
    statusBar()->addWidget(launcherButton);
    taskbar_ = new QWidget;
    taskbar_->setObjectName("taskbar");
    taskbarLayout_ = new QHBoxLayout(taskbar_);
    taskbarLayout_->setContentsMargins(8, 0, 8, 0);
    taskbarLayout_->setSpacing(4);
    statusBar()->addWidget(taskbar_, 1);
    statusBar()->addPermanentWidget(meter_);
    refreshTaskbar();
    statusBar()->setSizeGripEnabled(false);
    const QStringList desktopIds = {"terminal", "files", "browser", "monitor", "doom", "settings"};
    const QStringList names = {"Terminal", "Disk 0", "Network", "System", "DOOM", "Control"};
    for (int i = 0; i < desktopIds.size(); ++i) {
        auto *b = new QToolButton(area_->viewport());
        b->setObjectName("desktopIcon");
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        const QList<QColor> colors{QColor("#70e0cd"), QColor("#f0c789"), QColor("#9dbbfa"),
                                  QColor("#8ed5ee"), QColor("#eea197"), QColor("#b7a4e6")};
        b->setIcon(appIcon(desktopIds[i], colors[i]));
        b->setIconSize({32, 32});
        b->setText(names[i]);
        b->setGeometry(20, 85 + i * 73, 106, 65);
        b->setStyleSheet("QToolButton{color:#dce9f2;background:rgba(18,32,49,190);border:1px solid "
                         "#2e4357;border-radius:10px;padding:5px;font-size:12px}"
                         "QToolButton:hover{background:#213a4c;border-color:#70e0cd}"
                         "QToolButton:pressed{background:#305265}");
        connect(b, &QToolButton::clicked, this, [this, id = desktopIds[i]] { launch(id); });
        b->show();
    }
    connect(area_, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow *active) {
        for (auto *window : area_->subWindowList()) {
            if (auto *bar = window->findChild<QWidget *>("windowBar")) {
                bar->setProperty("active", window == active);
                bar->style()->unpolish(bar);
                bar->style()->polish(bar);
            }
        }
        taskbarSignature_.clear();
        refreshTaskbar();
    });
    area_->viewport()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(area_->viewport(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto *launchAction = menu->addAction("Launch an application...");
        connect(launchAction, &QAction::triggered, this, &Desktop::showLauncher);
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
void Desktop::showLauncher() {
    if (!started_ || locked_)
        return;
    auto *dialog = new QDialog(this, Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setObjectName("appLauncher");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setFixedSize(590, 475);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);
    auto *title = new QLabel("Launch an application");
    title->setStyleSheet("font-size:21px;font-weight:600");
    layout->addWidget(title);
    auto *search = new QLineEdit;
    search->setObjectName("launcherSearch");
    search->setPlaceholderText("Search apps, tools and games…");
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *results = new QListWidget;
    results->setObjectName("launcherResults");
    results->setIconSize(QSize(26, 26));
    results->setSpacing(3);
    for (const auto &app : applications) {
        const QString id(app.id);
        if (QStringList{"lua", "pager", "htop"}.contains(id))
            continue;
        auto *item = new QListWidgetItem(appIcon(id, QColor("#70e0cd")), QString::fromUtf8(app.title), results);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, 42));
    }
    results->setCurrentRow(0);
    layout->addWidget(results, 1);
    auto *hint = new QLabel("Enter to open  ·  ↑ ↓ to browse  ·  Esc to close");
    hint->setStyleSheet("font-size:12px;color:palette(mid)");
    layout->addWidget(hint);
    connect(search, &QLineEdit::textChanged, dialog, [results](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < results->count(); ++i) {
            auto *item = results->item(i);
            const bool matches = item->text().contains(query, Qt::CaseInsensitive) ||
                                 item->data(Qt::UserRole).toString().contains(query, Qt::CaseInsensitive);
            item->setHidden(!matches);
            if (matches && !first)
                first = item;
        }
        results->setCurrentItem(first);
    });
    const auto activate = [this, dialog, results] {
        auto *item = results->currentItem();
        if (!item || item->isHidden())
            return;
        const QString id = item->data(Qt::UserRole).toString();
        dialog->accept();
        launch(id);
    };
    connect(search, &QLineEdit::returnPressed, dialog, activate);
    connect(results, &QListWidget::itemActivated, dialog, [activate](QListWidgetItem *) { activate(); });
    auto *down = new QShortcut(QKeySequence(Qt::Key_Down), search);
    down->setContext(Qt::WidgetShortcut);
    connect(down, &QShortcut::activated, dialog, [results] { results->setFocus(); });
    dialog->move(mapToGlobal(rect().center()) - QPoint(dialog->width() / 2, dialog->height() / 2));
    dialog->show();
    search->setFocus();
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
    if (id == "capture") {
        captureVirtualScreen();
        return;
    }
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
    else if (id == "hellgate")
        content = createHellgateGame(system_);
    else if (id == "doom")
        content = createDoom(system_);
    else if (id == "games")
        content = createGameLibrary(system_, open);
    else if (id == "handbook")
        content = createHandbook(open);
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
void Desktop::refreshTaskbar()
{
    if (!taskbarLayout_ || !area_)
        return;
    QStringList titles;
    for (auto *window : area_->subWindowList())
        if (window->isVisible())
            titles << window->windowTitle();
    const QString signature = titles.join('|') + QString::number(quintptr(area_->activeSubWindow()));
    if (signature == taskbarSignature_)
        return;
    taskbarSignature_ = signature;
    while (QLayoutItem *item = taskbarLayout_->takeAt(0)) {
        if (auto *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    if (titles.isEmpty()) {
        auto *hint = new QLabel("NO WINDOWS  /  Ctrl+Alt+T opens the terminal");
        hint->setObjectName("taskbarHint");
        taskbarLayout_->addWidget(hint);
        return;
    }
    for (auto *window : area_->subWindowList()) {
        if (!window->isVisible())
            continue;
        auto *button = new QToolButton(taskbar_);
        button->setObjectName("taskbarButton");
        button->setText(window->windowTitle().section(" / ", 0, 0));
        button->setIcon(window->windowIcon());
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setCheckable(true);
        button->setChecked(window == area_->activeSubWindow());
        button->setToolTip("Restore " + window->windowTitle());
        button->setAutoRaise(true);
        button->setMinimumWidth(34);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setMaximumWidth(180);
        connect(button, &QToolButton::clicked, this, [this, window] {
            window->showNormal();
            area_->setActiveSubWindow(window);
            window->raise();
        });
        taskbarLayout_->addWidget(button);
    }
}

void Desktop::noteActivity() { lastActivity_ = QDateTime::currentMSecsSinceEpoch(); }

void Desktop::checkIdle()
{
    if (!started_ || locked_ || !isVisible())
        return;
    const int minutes = qBound(0, system_->setting("screensaverMinutes", 5).toInt(), 240);
    if (minutes <= 0) {
        if (saver_ && saver_->running())
            saver_->stop();
        return;
    }
    if (saver_ && saver_->running())
        return;
    if (lastActivity_ == 0)
        lastActivity_ = QDateTime::currentMSecsSinceEpoch();
    if (QDateTime::currentMSecsSinceEpoch() - lastActivity_ < qint64(minutes) * 60000)
        return;
    if (!saver_) {
        saver_ = new ScreenSaver(this);
        connect(saver_, &ScreenSaver::dismissed, this, [this] {
            if (saver_)
                saver_->stop();
            noteActivity();
            if (system_->setting("lockOnWake", false).toBool())
                lockSession();
        });
    }
    saver_->start();
}

void Desktop::showKeyboardReference()
{
    QMessageBox box(this);
    box.setWindowTitle("AURORA / keyboard reference");
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<b>Session</b><br>"
        "F11 — fullscreen &nbsp;&nbsp; Ctrl+Alt+L — lock &nbsp;&nbsp; Alt+F4 — close window<br>"
        "Ctrl+Alt+T — terminal &nbsp;&nbsp; Ctrl+Space — launcher &nbsp;&nbsp; PrtSc — capture screen<br>"
        "F1 — station handbook &nbsp;&nbsp; Shift+F1 — this list<br><br>"
        "<b>ASTER shell</b><br>"
        "<code>help</code> lists every command.<br>"
        "Pipes: <code>ls | grep txt</code> &nbsp; Sequences: <code>cmd ; cmd</code>, <code>cmd &amp;&amp; cmd</code><br>"
        "Variables: <code>$HOME</code>, <code>$USER</code>, <code>$?</code> &nbsp; "
        "<code>export NAME=value</code>, <code>alias ll=ls -l</code><br>"
        "Wildcards: <code>*.txt</code>, <code>docs/*</code> &nbsp; History: <code>history</code><br>"
        "Printing: <code>lpr FILE</code>, <code>lpq</code>, <code>lprm all</code><br>"
        "Archives: <code>pack APP.ARC FILE...</code>, <code>arc -l APP.ARC</code>, "
        "<code>unpack APP.ARC DIR</code><br>"
        "Live system files: <code>cat /proc/meminfo</code>, <code>/proc/cpuinfo</code>, "
        "<code>/proc/uptime</code><br><br>"
        "<b>HELLGATE</b><br>"
        "<code>W A S D</code> move, <code>Shift</code> run, arrows turn, <code>Space</code> fires, "
        "<code>E</code> opens a gate, <code>P</code> pauses, <code>R</code> restarts, "
        "<code>T</code> is practice mode.<br><br>"
        "<b>A16 BASIC</b><br>"
        "Type <code>basic</code> in the terminal. Store lines with numbers, then <code>RUN</code>, "
        "<code>LIST</code>, <code>SAVE NAME.BAS</code> and <code>SYSTEM</code> to return.");
    box.exec();
}

void Desktop::updateMeters() {
    if (!clock_)
        return;
    clock_->setText(simulatedTime(system_).toString("ddd d MMM   HH:mm"));
    clock_->setStyleSheet("font-size:12px;color:palette(text)");
    refreshTaskbar();
    meter_->setText(QString("%1 KiB free   ·   %2 tasks  ")
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
    QColor base = mode == "amber" ? QColor("#231c27") : mode == "paper" ? QColor("#cbdad9") : QColor("#0b1726");
    const QColor accent = mode == "amber" ? QColor("#eabd83") : mode == "paper" ? QColor("#427c76") : QColor("#70e0cd");
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
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, size.width(), size.height());
    sky.setColorAt(0, base);
    sky.setColorAt(.65, mode == "amber" ? QColor("#3d2c3a") : mode == "paper" ? QColor("#adcbc8") : QColor("#183b4b"));
    sky.setColorAt(1, base);
    p.fillRect(map.rect(), sky);
    // A quiet, procedural aurora keeps the desktop crisp at every virtual resolution.
    for (int i = 4; i >= 0; --i) {
        QPainterPath ribbon;
        const qreal y = size.height() * (.25 + i * .035);
        ribbon.moveTo(size.width() * .35, -30);
        ribbon.cubicTo(size.width() * .55, y + 130, size.width() * .82, y - 150, size.width() + 40, y + 80);
        QColor glow = accent;
        glow.setAlpha(11 + i * 5);
        p.setPen(QPen(glow, 35 + i * 20, Qt::SolidLine, Qt::RoundCap));
        p.drawPath(ribbon);
    }
    p.setPen(QColor(accent.red(), accent.green(), accent.blue(), 40));
    for (int i = 0; i < 46; ++i) {
        const int x = (i * 157 + 193) % size.width();
        const int y = (i * 73 + 31) % qMax(1, size.height() * 2 / 3);
        p.drawPoint(x, y);
    }
    const int horizon = size.height() * .73;
    for (int layer = 0; layer < 3; ++layer) {
        QPainterPath hills;
        const qreal y = horizon + layer * 47;
        hills.moveTo(0, y + 40);
        hills.cubicTo(size.width() * .18, y - 85, size.width() * .25, y + 65, size.width() * .43, y - 25);
        hills.cubicTo(size.width() * .68, y - 160 + layer * 30, size.width() * .8, y + 110, size.width(), y - 5);
        hills.lineTo(size.width(), size.height());
        hills.lineTo(0, size.height());
        hills.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(mode == "paper" ? QColor(77, 122, 126, 75 + layer * 30) : QColor(7, 18, 30, 110 + layer * 45));
        p.drawPath(hills);
    }
    QFont label("DejaVu Sans");
    label.setPixelSize(13);
    label.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    p.setFont(label);
    p.setPen(accent);
    p.drawText(26, 39, "A / 86");
    p.setPen(mode == "paper" ? QColor("#345b60") : QColor("#c7dce3"));
    label.setPixelSize(33);
    label.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    label.setWeight(QFont::Light);
    p.setFont(label);
    p.drawText(161, 59, "Your workspace. Your world.");
    label.setPixelSize(12);
    p.setFont(label);
    p.setPen(mode == "paper" ? QColor("#537779") : QColor("#87a5b5"));
    p.drawText(163, 84, "AURORA PERSONAL SYSTEM   /   A16 WORKSTATION");
    p.drawText(size.width() - 340, size.height() - 40, "A SMALL STATION. AN ENTIRE WORLD.");
    p.drawText(size.width() - 340, size.height() - 21, "1986  /  BUILT FOR EXPLORATION");
    if (activeScanlines_) {
        p.setPen(QColor(0, 0, 0, 18));
        for (int y = 0; y < size.height(); y += 3)
            p.drawLine(0, y, size.width(), y);
    }
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
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove || event->type() == QEvent::Wheel)
        noteActivity();
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
QString Desktop::captureVirtualScreen() {
    if (locked_ || !area_)
        return {};
    QString path;
    for (int i = 1; i <= 9999; ++i) {
        const QString candidate = QString("/home/guest/images/snap-%1.png").arg(i, 4, 10, QChar('0'));
        if (!system_->exists(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty()) {
        statusBar()->showMessage("Screenshot failed: all 9999 capture names are in use.", 10000);
        return {};
    }
    QImage image = grab().toImage();
    if (image.width() > 1600 || image.height() > 1000)
        image = image.scaled(1600, 1000, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (image.isNull() || !image.save(&buffer, "PNG")) {
        statusBar()->showMessage("Screenshot failed: could not encode the picture.", 10000);
        return {};
    }
    if (!system_->write(path, bytes)) {
        speaker_->play("error");
        statusBar()->showMessage("Screenshot failed: " + system_->lastError(), 10000);
        return {};
    }
    statusBar()->showMessage("Saved screenshot: " + path, 10000);
    launch("image", path);
    return path;
}

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
