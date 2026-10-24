#include "apps.h"
#include "display.h"
#include "login.h"
#include "shell.h"
#include "wallpaper.h"
#if AURORA_WITH_WEBENGINE
#include <QWebEngineDownloadRequest>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#endif
#include <QtWidgets>
#include <cmath>
#include <stdexcept>

namespace {
QVBoxLayout *column(QWidget *w) {
    auto *l = new QVBoxLayout(w);
    l->setContentsMargins(10, 10, 10, 10);
    l->setSpacing(7);
    return l;
}
QPushButton *button(const QString &text, QHBoxLayout *row, std::function<void()> fn) {
    auto *b = new QPushButton(text);
    row->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, std::move(fn));
    return b;
}
void failure(QWidget *w, SimSystem *s) {
    QMessageBox::warning(w, "AURORA / operation failed", s->lastError());
}
class Terminal final : public QWidget {
    SimSystem *system_;
    Speaker *speaker_;
    Shell *shell_;
    QPlainTextEdit *output_;
    QLineEdit *input_;
    QLabel *prompt_;
    QStringList history_;
    int cursor_ = 0;

  public:
    Terminal(SimSystem *s, Speaker *speaker, AppLauncher launch, QWidget *parent)
        : QWidget(parent), system_(s), speaker_(speaker) {
        setObjectName("terminal");
        auto *l = column(this);
        shell_ = new Shell(s, this);
        output_ = new QPlainTextEdit;
        output_->setObjectName("terminalOutput");
        output_->setReadOnly(true);
        output_->setMaximumBlockCount(1600);
        output_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        l->addWidget(output_, 1);
        auto *row = new QHBoxLayout;
        prompt_ = new QLabel(shell_->prompt());
        input_ = new QLineEdit;
        input_->setObjectName("terminalInput");
        input_->setFrame(false);
        row->addWidget(prompt_);
        row->addWidget(input_, 1);
        l->addLayout(row);
        output_->setPlainText(QString("AURORA/86  •  local session / tty01\nA16 processor / 4096 KiB / disk "
                                      "2048 KiB\n\nWelcome, %1 (%2). Type help to explore.\n")
                                  .arg(system_->setting("displayName", "Guest").toString(),
                                       system_->setting("username", "guest").toString()));
        input_->installEventFilter(this);
        setFocusProxy(input_);
        connect(input_, &QLineEdit::returnPressed, this, [this] {
            const QString line = input_->text();
            if (line.trimmed().isEmpty())
                return;
            output_->appendPlainText(shell_->prompt() + " " + line);
            history_ << line;
            if (history_.size() > 500)
                history_.removeFirst();
            cursor_ = history_.size();
            input_->clear();
            const QString result = shell_->execute(line);
            if (!result.isEmpty())
                output_->appendPlainText(result);
            prompt_->setText(shell_->prompt());
            speaker_->play(result.startsWith("error:") || result.startsWith("usage:") ? "error"
                                                                                      : "operation");
            output_->verticalScrollBar()->setValue(output_->verticalScrollBar()->maximum());
        });
        connect(shell_, &Shell::clearRequested, output_, &QPlainTextEdit::clear);
        connect(shell_, &Shell::launchRequested, this,
                [launch](const QString &app, const QString &path) { launch(app, path); });
        connect(shell_, &Shell::shutdownRequested, this, [this] { window()->close(); });
        connect(shell_, &Shell::soundRequested, this,
                [this](const QString &event) { speaker_->play(event); });
        connect(system_, &SimSystem::settingsChanged, this, [this] { prompt_->setText(shell_->prompt()); });
    }
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == input_ && e->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(e);
            if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
                cursor_ = qBound(0, cursor_ + (key->key() == Qt::Key_Up ? -1 : 1), int(history_.size()));
                input_->setText(cursor_ < history_.size() ? history_[cursor_] : QString());
                return true;
            }
            if (key->key() == Qt::Key_L && key->modifiers() & Qt::ControlModifier) {
                output_->clear();
                return true;
            }
            if (key->key() == Qt::Key_C && key->modifiers() & Qt::ControlModifier &&
                !input_->hasSelectedText()) {
                input_->clear();
                output_->appendPlainText("^C");
                return true;
            }
            if (key->key() == Qt::Key_Tab) {
                QString text = input_->text();
                int start = text.lastIndexOf(' ') + 1;
                QString token = text.mid(start);
                QString dir = shell_->cwd(), prefix = token;
                const int slash = token.lastIndexOf('/');
                if (slash >= 0) {
                    dir = system_->normalize(token.left(slash + 1), dir);
                    prefix = token.mid(slash + 1);
                }
                QStringList matches;
                for (const auto &v : system_->list(dir)) {
                    QString name = v.path.section('/', -1);
                    if (name.startsWith(prefix))
                        matches << name + (v.directory ? "/" : "");
                }
                if (matches.size() == 1)
                    input_->setText(text.left(start) + (slash >= 0 ? token.left(slash + 1) : QString()) +
                                    matches[0]);
                else if (matches.size() > 1)
                    output_->appendPlainText(matches.join("  "));
                return true;
            }
        }
        return QWidget::eventFilter(o, e);
    }
};
class Editor final : public QWidget {
    SimSystem *system_;
    Speaker *speaker_;
    QLineEdit *path_;
    QPlainTextEdit *text_;
    QLabel *status_;
    QString original_, loadError_;

  public:
    Editor(SimSystem *s, Speaker *speaker, QString path, QWidget *parent)
        : QWidget(parent), system_(s), speaker_(speaker) {
        auto *l = column(this);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel("FILE"));
        path_ = new QLineEdit(path.isEmpty() ? "/home/guest/docs/untitled.txt" : path);
        row->addWidget(path_, 1);
        auto *saveButton = button("Save", row, [this] { save(); });
        l->addLayout(row);
        text_ = new QPlainTextEdit;
        text_->setObjectName("editorText");
        text_->setTabStopDistance(24);
        l->addWidget(text_, 1);
        status_ = new QLabel;
        l->addWidget(status_);
        if (!path.isEmpty() && s->exists(path) && !s->isDirectory(path)) {
            const QByteArray data = s->read(path);
            loadError_ = s->lastError();
            if (loadError_.isEmpty()) {
                text_->setPlainText(QString::fromUtf8(data));
                original_ = s->normalize(path);
            } else {
                text_->setPlainText("Could not open this document.\n" + loadError_);
                text_->setReadOnly(true);
                path_->setReadOnly(true);
                saveButton->setEnabled(false);
            }
        }
        text_->document()->setModified(false);
        connect(text_, &QPlainTextEdit::textChanged, this, [this] { update(); });
        auto *saveShortcut = new QShortcut(QKeySequence::Save, this);
        connect(saveShortcut, &QShortcut::activated, this, [this] { save(); });
        setFocusProxy(text_);
        update();
    }
    bool mayClose() {
        if (!text_->document()->isModified())
            return true;
        const auto r = QMessageBox::question(this, "Unsaved document", "Save changes before closing?",
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                                             QMessageBox::Save);
        return r == QMessageBox::Discard || (r == QMessageBox::Save && save());
    }
    bool save() {
        if (!loadError_.isEmpty())
            return false;
        const QString path = system_->normalize(path_->text());
        if (path != original_ && system_->exists(path) &&
            QMessageBox::question(this, "Replace file", path + " already exists. Replace it?",
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return false;
        if (!system_->write(path, text_->toPlainText().toUtf8())) {
            failure(this, system_);
            speaker_->play("error");
            return false;
        }
        original_ = path;
        path_->setText(path);
        text_->document()->setModified(false);
        speaker_->play("operation");
        update();
        return true;
    }
    void update() {
        if (!loadError_.isEmpty()) {
            status_->setText(loadError_);
            return;
        }
        status_->setText(QString("%1 bytes  |  UTF-8  |  %2")
                             .arg(text_->toPlainText().toUtf8().size())
                             .arg(text_->document()->isModified() ? "modified / Ctrl+S saves" : "saved"));
    }
};
class Expression {
    QString source_;
    qsizetype i_ = 0;
    void spaces() {
        while (i_ < source_.size() && source_[i_].isSpace())
            ++i_;
    }
    bool take(QChar ch) {
        spaces();
        if (i_ < source_.size() && source_[i_] == ch) {
            ++i_;
            return true;
        }
        return false;
    }
    double number() {
        spaces();
        if (take('+'))
            return number();
        if (take('-'))
            return -number();
        if (take('(')) {
            double n = sum();
            if (!take(')'))
                throw std::runtime_error("Missing closing parenthesis");
            return n;
        }
        qsizetype start = i_;
        while (i_ < source_.size() && (source_[i_].isDigit() || source_[i_] == '.'))
            ++i_;
        bool ok = false;
        double n = source_.mid(start, i_ - start).toDouble(&ok);
        if (!ok)
            throw std::runtime_error("Invalid expression");
        return n;
    }
    double product() {
        double n = number();
        for (;;) {
            if (take('*'))
                n *= number();
            else if (take('/')) {
                double d = number();
                if (d == 0)
                    throw std::runtime_error("Division by zero");
                n /= d;
            } else
                return n;
        }
    }
    double sum() {
        double n = product();
        for (;;) {
            if (take('+'))
                n += product();
            else if (take('-'))
                n -= product();
            else
                return n;
        }
    }

  public:
    explicit Expression(QString source) : source_(std::move(source)) {}
    double evaluate() {
        double n = sum();
        spaces();
        if (i_ != source_.size() || !std::isfinite(n))
            throw std::runtime_error("Invalid expression or result out of range");
        return n;
    }
};
#if AURORA_WITH_WEBENGINE
class NetworkFence : public QWebEngineUrlRequestInterceptor {
  public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto scheme = info.requestUrl().scheme();
        if (scheme != "https" && scheme != "http" && scheme != "data" && scheme != "blob" &&
            scheme != "about")
            info.block(true);
    }
};
class BrowserPage : public QWebEnginePage {
  public:
    BrowserPage(QWebEngineProfile *profile, QObject *parent) : QWebEnginePage(profile, parent) {}

  protected:
    QStringList chooseFiles(FileSelectionMode, const QStringList &, const QStringList &) override {
        return {};
    }
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool) override {
        return QStringList{"http", "https", "about", "data", "blob"}.contains(url.scheme());
    }
    QWebEnginePage *createWindow(WebWindowType) override { return nullptr; }
};
#endif // AURORA_WITH_WEBENGINE
} // namespace

QWidget *createTerminal(SimSystem *s, Speaker *a, AppLauncher l, QWidget *p) {
    return new Terminal(s, a, std::move(l), p);
}
QWidget *createEditor(SimSystem *s, Speaker *a, const QString &path, QWidget *p) {
    return new Editor(s, a, path, p);
}
bool editorMayClose(QWidget *w) {
    auto *editor = dynamic_cast<Editor *>(w);
    return !editor || editor->mayClose();
}
QWidget *createFiles(SimSystem *s, Speaker *speaker, AppLauncher launch, const QString &initial,
                     QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *nav = new QHBoxLayout;
    auto *path = new QLineEdit(initial.isEmpty() ? "/home/guest" : s->normalize(initial));
    nav->addWidget(new QLabel("DISK 0:"));
    nav->addWidget(path, 1);
    l->addLayout(nav);
    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({"NAME", "TYPE", "BYTES", "MODE"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setObjectName("fileTable");
    l->addWidget(table, 1);
    auto *status = new QLabel;
    l->addWidget(status);
    auto refresh = [s, path, table, status] {
        table->setRowCount(0);
        for (const auto &v : s->list(s->normalize(path->text()))) {
            int row = table->rowCount();
            table->insertRow(row);
            auto *name = new QTableWidgetItem(v.path.section('/', -1));
            name->setData(Qt::UserRole, v.path);
            name->setIcon(appIcon(v.directory ? "files" : "editor", QColor("#35574b")));
            table->setItem(row, 0, name);
            table->setItem(row, 1, new QTableWidgetItem(v.directory ? "DIR" : "DOC"));
            table->setItem(row, 2, new QTableWidgetItem(v.directory ? "-" : QString::number(v.data.size())));
            table->setItem(row, 3, new QTableWidgetItem(QString::number(v.permissions, 8)));
        }
        status->setText(QString("%1 entries  /  %2 KiB free")
                            .arg(table->rowCount())
                            .arg((SimSystem::DiskBytes - s->usedBytes()) / 1024));
    };
    auto selected = [table]() -> QString {
        return table->currentRow() < 0 ? QString()
                                       : table->item(table->currentRow(), 0)->data(Qt::UserRole).toString();
    };
    auto open = [selected, s, path, launch, refresh] {
        QString file = selected();
        if (file.isEmpty())
            return;
        if (s->isDirectory(file)) {
            path->setText(file);
            refresh();
        } else {
            QString ext = file.section('.', -1).toLower();
            launch(QStringList{"ppm", "png", "jpg", "jpeg", "bmp", "gif"}.contains(ext) ? "image" : "editor",
                   file);
        }
    };
    auto *row = new QHBoxLayout;
    l->addLayout(row);
    button("Up", nav, [path, s, refresh] {
        path->setText(s->normalize(path->text() + "/.."));
        refresh();
    });
    button("Open", row, open);
    button("New", row, [w, s, path, launch] {
        bool ok = false;
        QString name = QInputDialog::getText(w, "New document", "Name:", QLineEdit::Normal, "new.txt", &ok);
        if (!ok || name.isEmpty())
            return;
        QString target = s->normalize(name, path->text());
        if (s->exists(target)) {
            QMessageBox::warning(w, "New document", "The file already exists.");
            return;
        }
        if (s->write(target, {}))
            launch("editor", target);
        else
            failure(w, s);
    });
    button("Folder", row, [w, s, path] {
        bool ok = false;
        QString name = QInputDialog::getText(w, "New folder", "Name:", QLineEdit::Normal, "new-folder", &ok);
        if (ok && !name.isEmpty() && !s->mkdir(s->normalize(name, path->text())))
            failure(w, s);
    });
    button("Move", row, [w, s, selected] {
        QString source = selected();
        if (source.isEmpty())
            return;
        bool ok = false;
        QString dest =
            QInputDialog::getText(w, "Move / rename", "New virtual path:", QLineEdit::Normal, source, &ok);
        if (ok && !dest.isEmpty() && !s->move(source, s->normalize(dest, source.section('/', 0, -2))))
            failure(w, s);
    });
    button("Delete", row, [w, s, speaker, selected] {
        QString file = selected();
        if (file.isEmpty())
            return;
        if (QMessageBox::question(w, "Delete", "Delete " + file + " and its contents?",
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            if (!s->remove(file, true))
                failure(w, s);
            else
                speaker->play("operation");
        }
    });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, w, [open](int, int) { open(); });
    QObject::connect(path, &QLineEdit::returnPressed, w, [w, s, path, refresh] {
        QString p = s->normalize(path->text());
        if (!s->isDirectory(p)) {
            QMessageBox::warning(w, "Directory", "The virtual directory does not exist.");
            return;
        }
        path->setText(p);
        refresh();
    });
    QObject::connect(s, &SimSystem::changed, w, refresh);
    refresh();
    return w;
}
QWidget *createCalculator(QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *display = new QLineEdit;
    display->setObjectName("calculatorInput");
    display->setAlignment(Qt::AlignRight);
    display->setPlaceholderText("0");
    display->setMinimumHeight(42);
    l->addWidget(display);
    auto *result = new QLabel("ARITHMETIC / DOUBLE PRECISION");
    result->setWordWrap(true);
    l->addWidget(result);
    auto eval = [display, result] {
        try {
            const QString text = QString::number(Expression(display->text()).evaluate(), 'g', 12);
            display->setText(text);
            result->setText("OK");
        } catch (const std::exception &e) {
            result->setText(QString::fromUtf8(e.what()));
        }
    };
    auto *grid = new QGridLayout;
    l->addLayout(grid);
    const QStringList keys = {"7", "8", "9", "/", "4", "5", "6", "*", "1",   "2",
                              "3", "-", "0", ".", "=", "+", "(", ")", "DEL", "C"};
    for (int i = 0; i < keys.size(); ++i) {
        auto *b = new QPushButton(keys[i]);
        b->setMinimumSize(50, 36);
        grid->addWidget(b, i / 4, i % 4);
        QObject::connect(b, &QPushButton::clicked, w, [display, k = keys[i], eval] {
            if (k == "=")
                eval();
            else if (k == "C")
                display->clear();
            else if (k == "DEL")
                display->backspace();
            else
                display->insert(k);
        });
    }
    QObject::connect(display, &QLineEdit::returnPressed, w, eval);
    w->setFocusProxy(display);
    return w;
}
QWidget *createImageViewer(SimSystem *s, const QString &initial, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *row = new QHBoxLayout;
    auto *path = new QLineEdit(initial.isEmpty() ? "/home/guest/images/aurora.ppm" : initial);
    row->addWidget(path, 1);
    auto *image = new QLabel;
    image->setAlignment(Qt::AlignCenter);
    image->setMinimumSize(320, 220);
    image->setStyleSheet("background:#172b2a; color:#b6cc95");
    auto *info = new QLabel;
    l->addLayout(row);
    l->addWidget(image, 1);
    l->addWidget(info);
    auto load = [w, s, path, image, info] {
        QByteArray data = s->read(s->normalize(path->text()));
        QImage pic = QImage::fromData(data);
        if (pic.isNull()) {
            image->clear();
            info->setText(s->lastError().isEmpty() ? "Image missing or unsupported format." : s->lastError());
            return;
        }
        info->setText(QString("%1 x %2 pixels / %3 bytes / nearest-neighbor scaling")
                          .arg(pic.width())
                          .arg(pic.height())
                          .arg(data.size()));
        image->setPixmap(
            QPixmap::fromImage(pic).scaled(500, 300, Qt::KeepAspectRatio, Qt::FastTransformation));
        Q_UNUSED(w);
    };
    button("Load", row, load);
    path->setObjectName("imagePath");
    auto *exportButton = new QPushButton("Export to Linux...");
    exportButton->setObjectName("imageExport");
    row->addWidget(exportButton);
    QObject::connect(exportButton, &QPushButton::clicked, w, [w, s, path, info] {
        const QByteArray data = s->read(s->normalize(path->text()));
        if (!s->lastError().isEmpty() || QImage::fromData(data).isNull()) {
            info->setText("Load a readable virtual image before exporting.");
            return;
        }
        const QString target = QFileDialog::getSaveFileName(
            w, "Export image to Linux", QFileInfo(path->text()).fileName(), "Images (*);;All files (*)");
        if (target.isEmpty())
            return;
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            info->setText("Could not export image: " + file.errorString());
        else
            info->setText("Exported image to the selected Linux file.");
    });
    QObject::connect(path, &QLineEdit::returnPressed, w, load);
    load();
    return w;
}
QWidget *createSettings(SimSystem *s, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *outer = column(w);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("controlTabs");
    outer->addWidget(tabs);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *general = new QWidget;
    auto *l = column(general);
    scroll->setWidget(general);
    tabs->addTab(scroll, "System and text");
    tabs->addTab(createWallpaperPanel(s), "Wallpaper");
    tabs->addTab(createIdentityPanel(s), "auroraSYNC");
    tabs->addTab(createDisplayPanel(s), "Display");
    auto *title = new QLabel("CONTROL PANEL / AURORA 1.4");
    l->addWidget(title);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    l->addLayout(form);
    auto *theme = new QComboBox;
    theme->addItem("Green phosphor", "green");
    theme->addItem("Amber", "amber");
    theme->addItem("Paper / monochrome", "paper");
    theme->setCurrentIndex(qMax(0, theme->findData(s->setting("theme", "green"))));
    form->addRow("Monitor", theme);
    auto *size = new QComboBox;
    size->setObjectName("fontSize");
    for (int n : {14, 16, 18, 20})
        size->addItem(QString::number(n) + " px" + (n == 16 ? " / default" : ""), n);
    size->setCurrentIndex(qMax(0, size->findData(s->setting("fontSize", 16))));
    form->addRow("Text size", size);
    auto *sample = new QLabel("Clear text: Aa Bb 0123456789");
    sample->setObjectName("fontPreview");
    sample->setWordWrap(true);
    form->addRow("Preview", sample);
    auto *sound = new QCheckBox("Internal speaker");
    sound->setChecked(s->setting("sound", true).toBool());
    form->addRow("Audio", sound);
    auto *key = new QCheckBox("Key sounds");
    key->setChecked(s->setting("keySound", true).toBool());
    form->addRow("Keyboard", key);
    auto *lines = new QCheckBox("Desktop scanlines");
    lines->setChecked(s->setting("scanlines", true).toBool());
    form->addRow("Video", lines);
    auto *boot = new QCheckBox("Show startup self-test");
    boot->setChecked(s->setting("boot", true).toBool());
    form->addRow("Startup", boot);
    QDateTime now = QDateTime::currentDateTime(), vintage = now;
    vintage.setDate(QDate(1986, now.date().month(),
                          qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
    auto *clock =
        new QDateTimeEdit(now.addSecs(s->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong()));
    clock->setDisplayFormat("dd/MM/yyyy HH:mm:ss");
    form->addRow("Virtual clock", clock);
    QObject::connect(theme, &QComboBox::currentIndexChanged, w,
                     [s, theme] { s->setSetting("theme", theme->currentData()); });
    QObject::connect(size, &QComboBox::currentIndexChanged, w,
                     [s, size] { s->setSetting("fontSize", size->currentData()); });
    QObject::connect(sound, &QCheckBox::toggled, w, [s](bool b) { s->setSetting("sound", b); });
    QObject::connect(key, &QCheckBox::toggled, w, [s](bool b) { s->setSetting("keySound", b); });
    QObject::connect(lines, &QCheckBox::toggled, w, [s](bool b) { s->setSetting("scanlines", b); });
    QObject::connect(boot, &QCheckBox::toggled, w, [s](bool b) { s->setSetting("boot", b); });
    auto *row = new QHBoxLayout;
    l->addLayout(row);
    button("Set clock", row, [s, clock] {
        s->setSetting("clockOffsetSeconds", QDateTime::currentDateTime().secsTo(clock->dateTime()));
    });
    auto *note =
        new QLabel("Larger text is applied immediately.\nPreferences and wallpaper "
                   "are saved automatically.\nF11 toggles fullscreen; Alt+F4 closes the active window.");
    note->setWordWrap(true);
    l->addWidget(note);
    l->addStretch();
    return w;
}
QWidget *createGameLibrary(SimSystem *s, AppLauncher launch, QWidget *parent) {
    Q_UNUSED(s);
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *heading = new QLabel("GAMES ROOM / AURORA 1.4");
    l->addWidget(heading);
    auto *intro = new QLabel("Six experiences. Pick one to begin.");
    intro->setWordWrap(true);
    l->addWidget(intro);
    struct Choice {
        const char *id;
        const char *title;
        const char *description;
    };
    const Choice choices[] = {{"prism", "Prism", "Rotate mirrors and guide the laser to its receiver."},
                              {"garden", "Orbital Garden", "Plan the irrigation to make the garden bloom."},
                              {"echo", "Echo", "Watch, listen and repeat the signal sequence."},
                              {"snake", "Circuit", "Route a signal without crossing its own trail."},
                              {"blocks", "Orbital Watch", "Defend the station from a meteor shower."},
                              {"hellgate", "HELLGATE", "Cross corridor 13: a textured 3D maze with drones and gates."}};
    for (const auto &choice : choices) {
        auto *frame = new QFrame;
        auto *row = new QHBoxLayout(frame);
        auto *play = new QPushButton(choice.title);
        play->setObjectName(QString("play_") + choice.id);
        play->setIcon(appIcon(choice.id, QColor("#35574b")));
        play->setIconSize(QSize(32, 32));
        play->setMinimumWidth(210);
        row->addWidget(play);
        auto *description = new QLabel(choice.description);
        description->setWordWrap(true);
        row->addWidget(description, 1);
        l->addWidget(frame);
        QObject::connect(play, &QPushButton::clicked, w,
                         [launch, id = QString(choice.id)] { launch(id, {}); });
    }
    l->addStretch();
    return w;
}
QWidget *createMonitor(SimSystem *s, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *summary = new QLabel;
    summary->setWordWrap(true);
    l->addWidget(summary);
    auto *ram = new QProgressBar;
    ram->setRange(0, SimSystem::MemoryKiB);
    l->addWidget(ram);
    auto *disk = new QProgressBar;
    disk->setRange(0, SimSystem::DiskBytes);
    l->addWidget(disk);
    auto *table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({"PID", "PROCESS", "KiB"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l->addWidget(table, 1);
    auto refresh = [s, summary, ram, disk, table] {
        summary->setText(QString("A16 / 8 MHz  |  RAM %1/4096 KiB  |  DISK %2/2048 KiB")
                             .arg(s->usedMemoryKiB())
                             .arg(s->usedBytes() / 1024));
        ram->setValue(s->usedMemoryKiB());
        ram->setFormat("RAM  %v / %m KiB");
        disk->setValue(s->usedBytes());
        disk->setFormat("DISK  %p%");
        table->setRowCount(0);
        for (const auto &p : s->processes()) {
            int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(QString::number(p.pid)));
            table->setItem(r, 1, new QTableWidgetItem(p.name));
            table->setItem(r, 2, new QTableWidgetItem(QString::number(p.memoryKiB)));
        }
    };
    auto *row = new QHBoxLayout;
    l->addLayout(row);
    button("End process", row, [w, s, table] {
        if (table->currentRow() < 0)
            return;
        int pid = table->item(table->currentRow(), 0)->text().toInt();
        if (!s->stopProcess(pid))
            failure(w, s);
    });
    QObject::connect(s, &SimSystem::changed, w, refresh);
    QObject::connect(s, &SimSystem::processesChanged, w, refresh);
    refresh();
    return w;
}
QWidget *createBrowser(SimSystem *s, Speaker *speaker, const QString &initial, QWidget *parent) {
#if !AURORA_WITH_WEBENGINE
    // Portable build: no Chromium available, so the station shows a local notice page.
    Q_UNUSED(s);
    Q_UNUSED(speaker);
    Q_UNUSED(initial);
    auto *w = new QWidget(parent);
    auto *l = column(w);
    l->addWidget(new QLabel("N E T W O R K   /   O F F L I N E"));
    auto *text = new QLabel("This AURORA/86 build was compiled without Qt WebEngine.\n\n"
                            "The rest of the station works normally: terminal, A16 BASIC,\n"
                            "Disk 0:, Writer, packages, Lua programs, games and /proc.\n\n"
                            "Build again with Qt WebEngineWidgets available to browse\n"
                            "the real internet from this window.");
    text->setWordWrap(true);
    l->addWidget(text);
    l->addStretch();
    return w;
#else
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *row = new QHBoxLayout;
    auto *url = new QLineEdit;
    url->setPlaceholderText("https://...");
    auto *view = new QWebEngineView;
    view->setObjectName("browserView");
    auto *profile = new QWebEngineProfile;
    profile->setHttpCacheType(QWebEngineProfile::MemoryHttpCache);
    profile->setHttpCacheMaximumSize(8 * 1024 * 1024);
    profile->setPersistentCookiesPolicy(QWebEngineProfile::NoPersistentCookies);
    profile->setUrlRequestInterceptor(new NetworkFence(profile));
    auto *page = new BrowserPage(profile, view);
    view->setPage(page);
    auto *settings = page->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    settings->setAttribute(QWebEngineSettings::WebRTCPublicInterfacesOnly, true);
    settings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);
    settings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, false);
    settings->setAttribute(QWebEngineSettings::HyperlinkAuditingEnabled, false);
    view->setContextMenuPolicy(Qt::NoContextMenu);
    QObject::connect(page, &QWebEnginePage::permissionRequested, w,
                     [](QWebEnginePermission permission) { permission.deny(); });
    QObject::connect(profile, &QWebEngineProfile::downloadRequested, w, [w](QWebEngineDownloadRequest *d) {
        d->cancel();
        QMessageBox::information(
            w, "Download",
            "Use Save to store the page on the virtual disk. Downloads to Linux are disabled.");
    });
    auto home = [view] {
        view->setHtml(
            QString::fromUtf8(
                R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><style>body{background:#efe5bd;color:#243d34;font:16px monospace;margin:36px;line-height:1.6}h1{font-size:38px;letter-spacing:-2px;border-bottom:3px solid;padding-bottom:14px}a{color:#35574b}small{letter-spacing:3px}li{margin:10px 0}footer{border-top:1px solid;margin-top:30px;padding-top:12px;font-size:13px}</style><small>DATA NETWORK / TERMINAL 01</small><h1>A world on a wire.</h1><p>Welcome to AURORA Network Mail.<br>Enter an HTTP or HTTPS address in the bar above.</p><ul><li><a href="https://example.com">Example / connection test</a></li><li><a href="https://en.wikipedia.org">Encyclopedia / Wikipedia</a></li><li><a href="https://info.cern.ch/hypertext/WWW/TheProject.html">Archive / the first website</a></li></ul><footer>1986 INTERFACE • MODERN WEB ENGINE<br>Real internet. Documents saved to the virtual disk.</footer></html>)HTML"),
            QUrl("about:blank"));
    };
    button("<", row, [view] { view->back(); });
    button(">", row, [view] { view->forward(); });
    button("Reload", row, [view] { view->reload(); });
    button("Home", row, home);
    row->addWidget(url, 1);
    auto go = [url, view, w] {
        QUrl target = QUrl::fromUserInput(url->text());
        if (target.scheme() != "https" && target.scheme() != "http") {
            QMessageBox::warning(w, "Address", "Only HTTP and HTTPS addresses are allowed.");
            return;
        }
        view->load(target);
    };
    button("Go", row, go);
    l->addLayout(row);
    l->addWidget(view, 1);
    profile->setParent(w); // The view and page are destroyed before their profile.
    auto *bottom = new QHBoxLayout;
    auto *status = new QLabel("NETWORK READY");
    bottom->addWidget(status, 1);
    button("Stop", bottom, [view] { view->stop(); });
    button("Save", bottom, [w, view, s, speaker] {
        bool ok = false;
        QString path = QInputDialog::getText(w, "Save page", "Virtual disk file:", QLineEdit::Normal,
                                             "/home/guest/docs/page.html", &ok);
        if (!ok || path.isEmpty())
            return;
        path = s->normalize(path);
        if (s->exists(path) && QMessageBox::question(w, "Replace", path + " already exists. Replace it?",
                                                     QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        QPointer<QWidget> guard = w;
        view->page()->toHtml([guard, s, speaker, path](const QString &html) {
            if (!guard)
                return;
            if (!s->write(path, html.toUtf8()))
                failure(guard, s);
            else
                speaker->play("operation");
        });
    });
    l->addLayout(bottom);
    QObject::connect(url, &QLineEdit::returnPressed, w, go);
    QObject::connect(view, &QWebEngineView::urlChanged, w, [url](const QUrl &u) {
        if (u.scheme() == "http" || u.scheme() == "https")
            url->setText(u.toString());
    });
    QObject::connect(view, &QWebEngineView::loadProgress, w,
                     [status](int n) { status->setText(QString("CONNECTING... %1%").arg(n)); });
    QObject::connect(view, &QWebEngineView::loadFinished, w, [status, speaker, view](bool ok) {
        if (ok)
            view->page()->runJavaScript(
                "(()=>{if(document.getElementById('aurora-screen'))return;const "
                "s=document.createElement('style');s.id='aurora-screen';s.textContent='html{filter:grayscale("
                "1) sepia(.45) "
                "hue-rotate(30deg)}body,input,button,select,textarea{font-family:monospace!important}*{"
                "border-radius:0!important;box-shadow:none!important}img{image-rendering:pixelated}';"
                "document.head.appendChild(s)})()");
        status->setText(ok ? "TRANSFER COMPLETE" : "CONNECTION FAILED / check the network and address");
        if (!ok)
            speaker->play("error");
    });
    if (initial.isEmpty())
        home();
    else {
        url->setText(initial);
        go();
    }
    return w;
#endif // AURORA_WITH_WEBENGINE
}
QWidget *createAbout(QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *text = new QLabel("A U R O R A  /  8 6\n\nPERSONAL RESEARCH WORKSTATION\n"
                            "A16 series • 8 MHz • 4096 KiB\n2048 KiB magnetic disk\n\n"
                            "AURORA system 1.4 / October 1986\nA small station. An entire world.\n\n"
                            "Hardware and processes are simulated.\nApplications built with C++ / Qt.\n"
                            "Browsing uses Chromium and the real internet.\n\n"
                            "F11  fullscreen      Ctrl+S  save\nTab  complete        ↑ / ↓   history");
    text->setWordWrap(true);
    l->addWidget(text);
    l->addStretch();
    return w;
}
QWidget *createHandbook(AppLauncher launch, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *layout = column(w);
    layout->addWidget(new QLabel("STATION HANDBOOK / F1"));
    auto *tabs = new QTabWidget;
    tabs->setObjectName("handbookTabs");
    auto page = [tabs](const QString &title, const QString &text) {
        auto *body = new QPlainTextEdit(text);
        body->setReadOnly(true);
        tabs->addTab(body, title);
    };
    page("Keyboard", "F1              Station handbook\n"
                     "Shift+F1        Keyboard reference card\n"
                     "F11             Fullscreen / window\n"
                     "Print Screen    Save this station's picture to Disk 0\n"
                     "Ctrl+Alt+T      Open Terminal\n"
                     "Ctrl+Alt+L      Lock auroraSYNC\n"
                     "Alt+F4          Close the active application\n"
                     "Ctrl+S          Save in Writer\n"
                     "W A S D         Move in HELLGATE (A/D strafe, Shift runs)\n"
                     "Arrows, Space   Turn and fire in HELLGATE\n"
                     "E / P / R       Gate, pause and restart in HELLGATE\n"
                     "Tab             Complete a path in Terminal\n"
                     "Up / Down       Terminal command history\n\n"
                     "Move windows by their title bars. Resize at the lower-right corner.\n"
                     "Restore minimized applications from the Windows menu.\n\n"
                     "Screenshots stay in /home/guest/images/snap-NNNN.png.\n"
                     "Atelier opens the picture. Export to Linux asks you for a destination.\n"
                     "Your Linux desktop and other applications are never captured.");
    page("Terminal", "Start here:\n\nhelp\nls -l\ncat README.txt\n"
                     "echo Hello > docs/hello.txt\nedit docs/hello.txt\n\n"
                     "Install useful tools:\n\naurora -i fastfetch htop tree grep coreutils manuals\n"
                     "fastfetch\nhtop\nman grep\n\n"
                     "Live virtual hardware:\n\ncat /proc/meminfo\ncat /proc/cpuinfo\nls /proc\n\n"
                     "Commands use simulated files and processes. No Linux commands are executed.\n"
                     "Disk capacity is 2048 KiB; RAM capacity is 4096 KiB.");
    page("HELLGATE", "HELLGATE / corridor 13\n\n"
                     "Open it from Games -> HELLGATE or type hellgate.\n\n"
                     "W / S           walk forward and back\n"
                     "A / D           strafe left and right\n"
                     "Left / Right    turn\n"
                     "Shift           run\n"
                     "Space           fire (each shot spends one round)\n"
                     "E               open or close a gate you are facing\n"
                     "P               pause        R restart after the end\n\n"
                     "Find health packs and ammo crates, retire the five drones and\n"
                     "reach the EXIT portal. Wall columns, drones and gates are drawn\n"
                     "by the simulated A16: no GPU, no texture files on disk.\n"
                     "Your best crossing time is kept in the station settings.");
    page("Lua", "No compiler needed. Open Lua Studio, then choose Open folder.\n"
                "Put a UTF-8 file named my-app.lua in that mods/ folder.\n\n"
                "Install and run it in Terminal:\n\naurora -i my-app\nmy-app\n\n"
                "Try the included programs:\n\naurora -i hello notepad todo theme-switcher\nnotepad\n\n"
                "Use Lua Studio's New program to start writing.\n"
                "Edit the installed copy in Writer, save, then use Reload / run.\n\n"
                "aurora.window creates text, inputs and buttons.\n"
                "aurora.input reads or updates an input field.\n"
                "aurora.read / write / list / mkdir use Disk 0.\n"
                "aurora.theme / setting customize supported preferences.\n\n"
                "Lua programs have bounded memory and execution time.\n"
                "They cannot access Linux files, start native programs or load DLLs.");
    layout->addWidget(tabs, 1);
    auto *actions = new QHBoxLayout;
    button("Terminal", actions, [launch] { launch("terminal", {}); });
    button("Package Center", actions, [launch] { launch("packages", {}); });
    button("Lua Studio", actions, [launch] { launch("lua-studio", {}); });
    layout->addLayout(actions);
    return w;
}
QIcon appIcon(const QString &kind, QColor ink) {
    QPixmap map(32, 32);
    map.fill(Qt::transparent);
    QPainter p(&map);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(ink, 2));
    p.setBrush(Qt::NoBrush);
    if (kind == "terminal") {
        p.drawRect(2, 4, 27, 22);
        p.drawLine(7, 10, 11, 14);
        p.drawLine(11, 14, 7, 18);
        p.drawLine(15, 19, 23, 19);
        p.drawLine(10, 29, 23, 29);
    } else if (kind == "files") {
        p.drawRect(2, 10, 28, 18);
        p.drawRect(3, 5, 12, 5);
        p.drawLine(5, 14, 27, 14);
    } else if (kind == "packages") {
        p.drawRect(3, 7, 26, 22);
        p.drawLine(3, 13, 29, 13);
        p.drawLine(16, 7, 16, 29);
        p.drawLine(8, 3, 24, 3);
    } else if (kind == "editor") {
        p.drawRect(6, 2, 19, 27);
        for (int y = 9; y < 24; y += 5)
            p.drawLine(10, y, 21, y);
    } else if (kind == "browser") {
        p.drawEllipse(3, 3, 26, 26);
        p.drawEllipse(10, 3, 12, 26);
        p.drawLine(3, 16, 29, 16);
        p.drawLine(6, 9, 26, 9);
        p.drawLine(6, 23, 26, 23);
    } else if (kind == "calc") {
        p.drawRect(5, 2, 23, 28);
        p.drawRect(8, 5, 17, 6);
        for (int y = 15; y < 28; y += 5)
            for (int x = 9; x < 26; x += 6)
                p.fillRect(x, y, 3, 3, ink);
    } else if (kind == "image") {
        p.drawRect(2, 4, 28, 24);
        p.drawEllipse(20, 8, 4, 4);
        p.drawPolyline(QPolygon({{4, 25}, {12, 14}, {19, 23}, {23, 18}, {29, 25}}));
    } else if (kind == "settings") {
        for (int y = 7; y <= 25; y += 9)
            p.drawLine(3, y, 29, y);
        p.fillRect(9, 3, 5, 8, ink);
        p.fillRect(20, 12, 5, 8, ink);
        p.fillRect(8, 21, 5, 8, ink);
    } else if (kind == "monitor" || kind == "htop") {
        p.drawRect(2, 4, 28, 23);
        p.drawPolyline(QPolygon({{4, 20}, {9, 20}, {12, 10}, {16, 23}, {21, 14}, {28, 14}}));
    } else if (kind == "snake" || kind == "circuit") {
        p.drawPolyline(QPolygon({{3, 25}, {12, 25}, {12, 15}, {24, 15}, {24, 5}, {17, 5}}));
        p.drawRect(14, 2, 5, 5);
        p.fillRect(5, 7, 4, 4, ink);
    } else if (kind == "prism") {
        p.drawPolygon(QPolygon({{16, 2}, {29, 16}, {16, 30}, {3, 16}}));
        p.drawLine(0, 16, 12, 16);
        p.drawLine(12, 16, 24, 5);
    } else if (kind == "garden") {
        p.drawLine(16, 29, 16, 12);
        p.drawEllipse(4, 6, 12, 8);
        p.drawEllipse(16, 4, 12, 8);
        p.drawLine(5, 29, 27, 29);
    } else if (kind == "echo") {
        for (int y : {3, 18})
            for (int x : {3, 18})
                p.drawRect(x, y, 10, 10);
    } else if (kind == "hellgate") {
        p.drawRect(3, 6, 26, 20);
        p.drawLine(3, 19, 29, 19);
        p.drawLine(8, 6, 8, 13);
        p.drawLine(16, 6, 16, 13);
        p.drawLine(24, 6, 24, 13);
        p.drawLine(12, 26, 12, 30);
        p.drawLine(20, 26, 20, 30);
    } else if (kind == "games") {
        p.drawRect(2, 8, 28, 20);
        p.drawLine(7, 17, 17, 17);
        p.drawLine(12, 12, 12, 22);
        p.drawRect(22, 13, 3, 3);
        p.drawRect(25, 20, 3, 3);
    } else if (kind == "meteor" || kind == "blocks") {
        p.drawPolyline(QPolygon({{9, 28}, {16, 10}, {23, 28}, {16, 23}, {9, 28}}));
        p.drawRect(3, 3, 5, 5);
        p.drawRect(24, 10, 4, 4);
    } else {
        p.drawEllipse(4, 4, 24, 24);
        p.drawLine(16, 10, 16, 19);
        p.drawPoint(16, 24);
    }
    return QIcon(map);
}
