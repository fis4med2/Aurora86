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
#include <memory>
#include <atomic>

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
class MemoryHistory final : public QWidget {
    QList<int> samples_;
  public:
    explicit MemoryHistory(QWidget *parent = nullptr) : QWidget(parent) { setMinimumHeight(140); }
    void sample(int value) {
        samples_.append(value);
        if (samples_.size() > 60) samples_.removeFirst();
        update();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF plot = QRectF(rect()).adjusted(10, 12, -10, -12);
        painter.fillRect(rect(), palette().base());
        QColor grid = palette().text().color();
        grid.setAlpha(24);
        painter.setPen(grid);
        for (int i = 0; i <= 4; ++i)
            painter.drawLine(QPointF(plot.left(), plot.top() + plot.height()*i/4),
                             QPointF(plot.right(), plot.top() + plot.height()*i/4));
        if (samples_.isEmpty()) return;
        QPainterPath line;
        for (int i = 0; i < samples_.size(); ++i) {
            const QPointF point(plot.left() + plot.width() * (60 - samples_.size() + i) / 59,
                                plot.bottom() - plot.height() * samples_[i] / SimSystem::MemoryKiB);
            if (!i) line.moveTo(point); else line.lineTo(point);
        }
        QColor accent = palette().highlight().color();
        painter.setPen(QPen(accent, 2.5));
        painter.drawPath(line);
        QPainterPath area(line);
        area.lineTo(plot.bottomRight());
        area.lineTo(line.elementAt(0).x, plot.bottom());
        area.closeSubpath();
        QLinearGradient fill(plot.topLeft(), plot.bottomLeft());
        accent.setAlpha(80);
        fill.setColorAt(0, accent);
        accent.setAlpha(4);
        fill.setColorAt(1, accent);
        painter.fillPath(area, fill);
    }
};
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
    std::atomic_bool enabled{true};
    void interceptRequest(QWebEngineUrlRequestInfo &info) override {
        const auto scheme = info.requestUrl().scheme();
        if ((!enabled && (scheme == "http" || scheme == "https")) ||
            (scheme != "https" && scheme != "http" && scheme != "data" && scheme != "blob" && scheme != "about"))
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
    auto *back = new QPushButton("←");
    back->setObjectName("filesBack");
    back->setToolTip("Back · Alt+Left");
    back->setFixedWidth(40);
    nav->addWidget(back);
    auto *up = new QPushButton("↑");
    up->setToolTip("Parent folder · Alt+Up");
    up->setFixedWidth(40);
    nav->addWidget(up);
    QString first = initial.isEmpty() ? "/home/guest" : s->normalize(initial);
    if (!s->isDirectory(first))
        first = "/home/guest";
    auto *path = new QLineEdit(first);
    path->setObjectName("filesPath");
    path->setToolTip("Virtual disk address · Ctrl+L");
    nav->addWidget(path, 1);
    l->addLayout(nav);
    auto *body = new QHBoxLayout;
    auto *places = new QListWidget;
    places->setObjectName("filesPlaces");
    places->setFixedWidth(160);
    places->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    places->setSpacing(5);
    const QList<QPair<QString, QString>> shortcuts = {{"Home", "/home/guest"},
        {"Documents", "/home/guest/docs"}, {"Images", "/home/guest/images"},
        {"Disk 0", "/"}, {"System", "/etc"}, {"Processes", "/proc"}, {"Temporary", "/tmp"}};
    for (const auto &[title, target] : shortcuts) {
        auto *item = new QListWidgetItem(appIcon(target == "/proc" ? "monitor" : "files"), title, places);
        item->setData(Qt::UserRole, target);
    }
    body->addWidget(places);
    auto *files = new QVBoxLayout;
    auto *filter = new QLineEdit;
    filter->setObjectName("filesSearch");
    filter->setPlaceholderText("Filter this folder…");
    filter->setClearButtonEnabled(true);
    files->addWidget(filter);
    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({"Name", "Kind", "Size", "Permissions"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int index : {1, 2, 3})
        table->horizontalHeader()->setSectionResizeMode(index, QHeaderView::ResizeToContents);
    table->verticalHeader()->hide();
    table->verticalHeader()->setDefaultSectionSize(38);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setObjectName("fileTable");
    files->addWidget(table, 1);
    body->addLayout(files, 1);
    l->addLayout(body, 1);
    auto *status = new QLabel;
    status->setObjectName("filesStatus");
    status->setWordWrap(true);
    struct Navigation { QStringList history; int position = 0; QString copied; };
    auto navigation = std::make_shared<Navigation>();
    navigation->history << first;
    auto selected = [table]() -> QString {
        auto *item = table->item(table->currentRow(), 0);
        return item ? item->data(Qt::UserRole).toString() : QString();
    };
    auto refresh = [s, path, table, status, filter, back, navigation, selected] {
        const QString previous = selected();
        table->setRowCount(0);
        int count = 0;
        for (const auto &v : s->list(s->normalize(path->text()))) {
            ++count;
            const QString name = v.path.section('/', -1);
            if (!name.contains(filter->text(), Qt::CaseInsensitive))
                continue;
            const int row = table->rowCount();
            table->insertRow(row);
            auto *item = new QTableWidgetItem(name);
            item->setData(Qt::UserRole, v.path);
            item->setIcon(appIcon(v.directory ? "files" : "editor"));
            table->setItem(row, 0, item);
            const QString extension = name.section('.', -1).toUpper();
            table->setItem(row, 1, new QTableWidgetItem(v.directory ? "Folder" :
                (name.contains('.') ? extension + " file" : "Document")));
            table->setItem(row, 2, new QTableWidgetItem(v.directory ? "—" :
                (v.data.size() < 1024 ? QString::number(v.data.size()) + " B" :
                 QString::number(v.data.size() / 1024.0, 'f', 1) + " KiB")));
            table->setItem(row, 3, new QTableWidgetItem(QString::number(v.permissions, 8)));
            if (v.path == previous)
                table->selectRow(row);
        }
        status->setText(QString("%1 of %2 items    ·    %3 KiB available on Disk 0")
                            .arg(table->rowCount()).arg(count)
                            .arg((SimSystem::DiskBytes - s->usedBytes()) / 1024));
        back->setEnabled(navigation->position > 0);
    };
    auto navigate = [s, path, filter, navigation, refresh, w](const QString &target, bool record = true) {
        const QString destination = s->normalize(target);
        if (!s->isDirectory(destination)) {
            QMessageBox::warning(w, "Folder", "This virtual folder is unavailable.");
            path->setText(navigation->history.at(navigation->position));
            return;
        }
        if (record && destination != navigation->history.at(navigation->position)) {
            while (navigation->history.size() > navigation->position + 1)
                navigation->history.removeLast();
            navigation->history << destination;
            navigation->position = navigation->history.size() - 1;
        }
        path->setText(destination);
        filter->clear();
        refresh();
    };
    auto open = [selected, s, navigate, launch] {
        const QString file = selected();
        if (file.isEmpty()) return;
        if (s->isDirectory(file)) navigate(file);
        else launch(QStringList{"ppm", "png", "jpg", "jpeg", "bmp", "gif"}
                .contains(file.section('.', -1).toLower()) ? "image" : "editor", file);
    };
    auto *row = new QHBoxLayout;
    l->addLayout(row);
    button("Open", row, open);
    button("New file", row, [w, s, path, launch] {
        bool ok = false;
        const QString name = QInputDialog::getText(w, "New document", "Name:", QLineEdit::Normal, "new.txt", &ok);
        if (!ok || name.isEmpty()) return;
        const QString target = s->normalize(name, path->text());
        if (s->exists(target)) {
            QMessageBox::warning(w, "New document", "The file already exists.");
            return;
        }
        if (s->write(target, {})) launch("editor", target);
        else failure(w, s);
    });
    button("Folder", row, [w, s, path] {
        bool ok = false;
        const QString name = QInputDialog::getText(w, "New folder", "Name:", QLineEdit::Normal, "new-folder", &ok);
        if (ok && !name.isEmpty() && !s->mkdir(s->normalize(name, path->text()))) failure(w, s);
    });
    button("Rename", row, [w, s, selected] {
        const QString source = selected();
        if (source.isEmpty()) return;
        bool ok = false;
        const QString destination = QInputDialog::getText(w, "Move / rename", "New virtual path:", QLineEdit::Normal, source, &ok);
        if (ok && !destination.isEmpty() && !s->move(source, s->normalize(destination, source.section('/', 0, -2))))
            failure(w, s);
    });
    auto *copy = button("Copy", row, [selected, navigation, status] {
        navigation->copied = selected();
        if (!navigation->copied.isEmpty()) status->setText("Copied " + navigation->copied + " · Choose a folder and Paste.");
    });
    auto *paste = button("Paste", row, [w, s, path, navigation, refresh] {
        if (navigation->copied.isEmpty()) return;
        const QString target = s->normalize(navigation->copied.section('/', -1), path->text());
        if (!s->copy(navigation->copied, target)) failure(w, s);
        else refresh();
    });
    auto *remove = button("Delete", row, [w, s, speaker, selected] {
        const QString file = selected();
        if (file.isEmpty()) return;
        if (QMessageBox::question(w, "Delete", "Delete " + file + " and its contents?", QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            if (!s->remove(file, true)) failure(w, s);
            else speaker->play("operation");
        }
    });
    l->addWidget(status);
    QObject::connect(back, &QPushButton::clicked, w, [navigation, navigate] {
        if (navigation->position > 0) navigate(navigation->history.at(--navigation->position), false);
    });
    QObject::connect(up, &QPushButton::clicked, w, [path, navigate] { navigate(path->text() + "/.."); });
    QObject::connect(places, &QListWidget::itemClicked, w, [navigate](QListWidgetItem *item) { navigate(item->data(Qt::UserRole).toString()); });
    QObject::connect(table, &QTableWidget::cellDoubleClicked, w, [open](int, int) { open(); });
    QObject::connect(path, &QLineEdit::returnPressed, w, [path, navigate] { navigate(path->text()); });
    QObject::connect(filter, &QLineEdit::textChanged, w, refresh);
    QObject::connect(s, &SimSystem::changed, w, refresh);
    for (const auto &[key, target] : QList<QPair<QKeySequence, QPushButton *>>{
             {QKeySequence("Alt+Left"), back}, {QKeySequence("Alt+Up"), up},
             {QKeySequence::Copy, copy}, {QKeySequence::Paste, paste}, {QKeySequence::Delete, remove}}) {
        auto *shortcut = new QShortcut(key, table);
        shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(shortcut, &QShortcut::activated, target, &QPushButton::click);
    }
    auto *address = new QShortcut(QKeySequence("Ctrl+L"), w);
    address->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(address, &QShortcut::activated, path, [path] { path->setFocus(); path->selectAll(); });
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
    auto *title = new QLabel("CONTROL PANEL / AURORA 1.6");
    l->addWidget(title);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    l->addLayout(form);
    auto *theme = new QComboBox;
    theme->addItem("Aurora / midnight", "green");
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
    lines->setChecked(s->setting("scanlines", false).toBool());
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
    auto *heading = new QLabel("After hours.");
    heading->setStyleSheet("font-size:28px;font-weight:600;background:transparent");
    l->addWidget(heading);
    auto *intro = new QLabel("Seven worlds, one workstation. Choose your next session.");
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *library = new QWidget;
    auto *grid = new QGridLayout(library);
    grid->setSpacing(12);
    struct Choice { const char *id; const char *title; const char *description; };
    const Choice choices[] = {
        {"doom", "DOOM / Original engine", "Real Doom engine. Freedoom included. Bring your Doom or Doom II WAD."},
        {"hellgate", "HELLGATE", "Two sectors, a shooting boss, practice mode and best times."},
        {"prism", "Prism", "Rotate mirrors. Bend light. Find the perfect path."},
        {"garden", "Orbital Garden", "Build irrigation and bring the station garden to life."},
        {"echo", "Echo", "Watch the lights, hear the pattern and repeat the signal."},
        {"snake", "Circuit", "Route a signal without crossing its own trail."},
        {"blocks", "Orbital Watch", "Protect the station from an incoming meteor storm."}};
    for (int i = 0; i < int(std::size(choices)); ++i) {
        const auto &choice = choices[i];
        auto *card = new QFrame;
        card->setMinimumHeight(150);
        card->setObjectName("gameCard");
        card->setStyleSheet("QFrame#gameCard{background:palette(alternate-base);border:1px solid palette(mid);border-radius:9px}");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        auto *play = new QPushButton(choice.title);
        play->setObjectName(QString("play_") + choice.id);
        play->setIcon(appIcon(choice.id));
        play->setIconSize(QSize(26, 26));
        layout->addWidget(play);
        auto *description = new QLabel(choice.description);
        description->setWordWrap(true);
        layout->addWidget(description, 1);
        grid->addWidget(card, i / 2, i % 2);
        QObject::connect(play, &QPushButton::clicked, w, [launch, id = QString(choice.id)] { launch(id, {}); });
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    scroll->setWidget(library);
    l->addWidget(scroll, 1);
    return w;
}
QWidget *createMonitor(SimSystem *s, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = column(w);
    auto *heading = new QLabel("System Center");
    heading->setStyleSheet("font-size:24px;font-weight:600;background:transparent");
    l->addWidget(heading);
    auto *summary = new QLabel;
    summary->setObjectName("systemSummary");
    l->addWidget(summary);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("systemTabs");
    l->addWidget(tabs, 1);
    auto *overview = new QWidget;
    auto *overviewLayout = column(overview);
    auto *cards = new QHBoxLayout;
    auto metric = [cards](const QString &title, const QString &unit) {
        auto *frame = new QFrame;
        frame->setObjectName("resourceCard");
        frame->setStyleSheet("QFrame#resourceCard{background:palette(base);border:1px solid palette(mid);border-radius:8px}");
        auto *layout = new QVBoxLayout(frame);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->addWidget(new QLabel(title));
        auto *value = new QLabel;
        value->setStyleSheet("font-size:26px;font-weight:600;background:transparent");
        layout->addWidget(value);
        layout->addWidget(new QLabel(unit));
        cards->addWidget(frame, 1);
        return value;
    };
    auto *memoryValue = metric("MEMORY", "of 4,096 KiB");
    auto *diskValue = metric("DISK 0", "of 2,048 KiB");
    auto *processValue = metric("PROCESSES", "virtual scheduler");
    overviewLayout->addLayout(cards);
    auto *ram = new QProgressBar;
    ram->setRange(0, SimSystem::MemoryKiB);
    auto *disk = new QProgressBar;
    disk->setRange(0, SimSystem::DiskBytes);
    overviewLayout->addWidget(ram);
    overviewLayout->addWidget(disk);
    overviewLayout->addWidget(new QLabel("MEMORY HISTORY  /  last 60 seconds"));
    auto *history = new MemoryHistory;
    overviewLayout->addWidget(history, 1);
    auto *hardware = new QLabel("A16 CPU · 8 MHz · 16-bit bus    /    LUME display adapter\n"
                               "Persistent virtual disk · Services controlled from the Services tab");
    hardware->setWordWrap(true);
    overviewLayout->addWidget(hardware);
    auto *overviewScroll = new QScrollArea;
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setFrameShape(QFrame::NoFrame);
    overviewScroll->setWidget(overview);
    tabs->addTab(overviewScroll, "Overview");
    auto *processPage = new QWidget;
    auto *processLayout = column(processPage);
    auto makeTable = [](const QStringList &headers) {
        auto *table = new QTableWidget(0, headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->verticalHeader()->hide();
        table->verticalHeader()->setDefaultSectionSize(36);
        table->setShowGrid(false);
        table->setAlternatingRowColors(true);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        return table;
    };
    auto *table = makeTable({"PID", "Process", "Memory / KiB"});
    table->setObjectName("systemProcesses");
    processLayout->addWidget(table, 1);
    auto *processActions = new QHBoxLayout;
    processLayout->addLayout(processActions);
    button("End process", processActions, [w, s, table] {
        if (table->currentRow() < 0) return;
        const int pid = table->item(table->currentRow(), 0)->text().toInt();
        if (!s->stopProcess(pid)) failure(w, s);
    });
    processActions->addStretch();
    tabs->addTab(processPage, "Processes");
    auto *servicePage = new QWidget;
    auto *serviceLayout = column(servicePage);
    auto *serviceNote = new QLabel("Services belong to the virtual station. Stopping the print spooler pauses new print jobs.");
    serviceNote->setWordWrap(true);
    serviceLayout->addWidget(serviceNote);
    auto *services = makeTable({"Service", "Description", "State"});
    services->setObjectName("systemServices");
    services->setColumnWidth(0, 120);
    serviceLayout->addWidget(services, 1);
    auto *serviceActions = new QHBoxLayout;
    serviceLayout->addLayout(serviceActions);
    for (bool running : {true, false}) {
        auto *control = button(running ? "Start service" : "Stop service", serviceActions, [w, s, services, running] {
            auto *item = services->item(services->currentRow(), 0);
            if (item && !s->setServiceRunning(item->text(), running)) failure(w, s);
        });
        control->setObjectName(running ? "serviceStart" : "serviceStop");
    }
    serviceActions->addStretch();
    tabs->addTab(servicePage, "Services");
    auto *journal = new QPlainTextEdit;
    journal->setObjectName("systemJournal");
    journal->setReadOnly(true);
    journal->setMaximumBlockCount(128);
    journal->setStyleSheet("font-family:'DejaVu Sans Mono';font-size:13px");
    tabs->addTab(journal, "Event log");
    auto refresh = [s, summary, memoryValue, diskValue, processValue, ram, disk, table] {
        summary->setText(QString("%1  /  AURORA 1.6    ·    Uptime %2h %3m %4s")
            .arg(s->hostname()).arg(s->uptimeSeconds()/3600).arg(s->uptimeSeconds()/60%60).arg(s->uptimeSeconds()%60));
        memoryValue->setText(QString::number(s->usedMemoryKiB()) + " KiB");
        diskValue->setText(QString::number(s->usedBytes()/1024) + " KiB");
        processValue->setText(QString::number(s->processes().size()));
        ram->setValue(s->usedMemoryKiB());
        ram->setFormat("Memory allocated   %p%");
        disk->setValue(s->usedBytes());
        disk->setFormat("Disk used   %p%");
        const int selectedPid = table->currentRow() < 0 ? -1 : table->item(table->currentRow(), 0)->text().toInt();
        table->setRowCount(0);
        for (const auto &process : s->processes()) {
            const int row = table->rowCount();
            table->insertRow(row);
            table->setItem(row, 0, new QTableWidgetItem(QString::number(process.pid)));
            table->setItem(row, 1, new QTableWidgetItem(process.name));
            table->setItem(row, 2, new QTableWidgetItem(QString::number(process.memoryKiB)));
            if (process.pid == selectedPid) table->selectRow(row);
        }
    };
    auto refreshServices = [s, services] {
        const QString selected = services->currentRow() < 0 ? QString() : services->item(services->currentRow(), 0)->text();
        services->setRowCount(0);
        for (const auto &service : s->services()) {
            const int row = services->rowCount();
            services->insertRow(row);
            services->setItem(row, 0, new QTableWidgetItem(service.name));
            services->setItem(row, 1, new QTableWidgetItem(service.description));
            auto *state = new QTableWidgetItem(service.running ? "Running" : "Stopped");
            state->setForeground(service.running ? services->palette().highlight().color() :
                (services->palette().base().color().lightness() > 128 ? QColor("#945820") : QColor("#e4ac79")));
            services->setItem(row, 2, state);
            if (service.name == selected) services->selectRow(row);
        }
    };
    auto refreshJournal = [s, journal] {
        const QString text = s->journalEntries(128).join('\n');
        if (text != journal->toPlainText()) {
            journal->setPlainText(text);
            journal->verticalScrollBar()->setValue(journal->verticalScrollBar()->maximum());
        }
    };
    auto *timer = new QTimer(w);
    timer->setInterval(1000);
    QObject::connect(timer, &QTimer::timeout, w, [refresh, history, s] { refresh(); history->sample(s->usedMemoryKiB()); });
    QObject::connect(s, &SimSystem::changed, w, refresh);
    QObject::connect(s, &SimSystem::processesChanged, w, refresh);
    QObject::connect(s, &SimSystem::settingsChanged, w, refresh);
    QObject::connect(s, &SimSystem::servicesChanged, w, refreshServices);
    QObject::connect(s, &SimSystem::journalChanged, w, refreshJournal);
    refresh();
    refreshServices();
    refreshJournal();
    history->sample(s->usedMemoryKiB());
    timer->start();
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
    auto *fence = new NetworkFence(profile);
    for (const auto &service : s->services())
        if (service.name == "network") fence->enabled = service.running;
    profile->setUrlRequestInterceptor(fence);
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
                R"HTML(<!doctype html><html lang="en"><meta charset="utf-8"><style>body{background:#0d1725;color:#dbe9f0;font:16px system-ui,sans-serif;margin:0;padding:52px;line-height:1.6}small{color:#69ddcf;letter-spacing:3px}h1{font-size:clamp(32px,6vw,56px);line-height:1.1;font-weight:500;margin:24px 0}p{color:#9aabbc;max-width:480px}a{display:block;background:#152235;border:1px solid #2e4357;border-radius:10px;color:#dbe9f0;padding:16px 20px;text-decoration:none;margin:12px 0;max-width:530px}a:hover{border-color:#69ddcf;color:#69ddcf}a span{float:right;color:#69ddcf}footer{color:#8095ab;font-size:13px;margin-top:32px}</style><small>AURORA NETWORK</small><h1>A world on a wire.</h1><p>Your workstation, connected. Enter a web address above or start somewhere familiar.</p><a href="https://en.wikipedia.org">Explore the encyclopedia <span>↗</span></a><a href="https://example.com">Test your connection <span>↗</span></a><a href="https://info.cern.ch/hypertext/WWW/TheProject.html">Visit the first website <span>↗</span></a><footer>Chromium browser · Save pages to your virtual disk</footer></html>)HTML"),
            QUrl("about:blank"));
    };
    button("<", row, [view] { view->back(); });
    button(">", row, [view] { view->forward(); });
    button("Reload", row, [view] { view->reload(); });
    button("Home", row, home);
    row->addWidget(url, 1);
    auto go = [url, view, w, fence] {
        if (!fence->enabled) {
            QMessageBox::information(w, "Network offline", "Start the network service in System Center or run: systemctl start network");
            return;
        }
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
    QObject::connect(view, &QWebEngineView::loadFinished, w, [status, speaker](bool ok) {
        status->setText(ok ? "Page ready" : "Connection failed / check the network and address");
        if (!ok) speaker->play("error");
    });
    QObject::connect(s, &SimSystem::servicesChanged, w, [s, fence, view, status] {
        for (const auto &service : s->services()) {
            if (service.name != "network") continue;
            fence->enabled = service.running;
            if (!service.running) view->stop();
            status->setText(service.running ? "Network bridge connected" : "Network service stopped · System Center → Services");
        }
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
                            "AURORA system 1.6 / October 1986\nA small station. An entire world.\n\n"
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
                     "T               HELLGATE practice mode (drones hold fire)\n"
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
                     "P               pause        R restart after the end\n"
                     "T               practice mode, the drones hold their fire\n\n"
                     "CORRIDOR 13 is the way in: find health packs and ammo crates,\n"
                     "retire the five drones and step on the EXIT portal.\n\n"
                     "THE CORE is the second sector. Four gates lead into the Warden's\n"
                     "arena and the Warden shoots back. The last portal only opens\n"
                     "once it falls, so stock up before you go in.\n\n"
                     "Wall columns, drones and gates are drawn by the simulated A16:\n"
                     "no GPU, no texture files on disk. Best times for both sectors\n"
                     "are kept in the station settings.");
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
    QPixmap map(64, 64);
    map.setDevicePixelRatio(2);
    map.fill(Qt::transparent);
    QPainter p(&map);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(ink, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
    } else if (kind == "doom") {
        p.drawRoundedRect(QRectF(3, 3, 26, 26), 5, 5);
        p.drawLine(9, 8, 9, 24);
        p.drawPolyline(QPolygon({{9, 8}, {19, 8}, {24, 13}, {24, 19}, {19, 24}, {9, 24}}));
        p.drawLine(14, 12, 14, 20);
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
