#include "lua_apps.h"
#include "lua_runtime.h"
#include "mods.h"
#include "packages.h"
#include <QDesktopServices>
#include <QtWidgets>
#include <memory>
namespace {
const QByteArray Template = R"LUA(-- @description My first Aurora program
local clicks = 0
aurora.window {
    title = "My first program",
    text = "Hello from Lua. No compiler needed!",
    buttons = {
        { label = "Click me", action = function()
            clicks = clicks + 1
            aurora.text("Button clicks: " .. clicks)
        end }
    }
}
)LUA";
class Program final : public QWidget {
    SimSystem *system_;
    QString path_;
    LuaRuntime runtime_;
    QLabel *title_;
    QPlainTextEdit *body_, *console_;
    QWidget *buttons_;
    QGridLayout *grid_;
    void render(const LuaResult &result) {
        title_->setText(result.title.isEmpty() ? "Lua program" : result.title);
        body_->setPlainText(result.text);
        body_->setVisible(!result.text.isEmpty());
        console_->setPlainText(result.output +
                               (result.error.isEmpty() ? QString() : "\nerror: " + result.error));
        while (auto *item = grid_->takeAt(0)) {
            if (item->widget()) {
                item->widget()->hide();
                item->widget()->deleteLater();
            }
            delete item;
        }
        for (int i = 0; i < result.buttons.size(); ++i) {
            const QString label = result.buttons[i];
            auto *b = new QPushButton(fontMetrics().elidedText(label, Qt::ElideRight, 190));
            b->setToolTip(label);
            b->setObjectName("luaButton_" + QString::number(i));
            b->setEnabled(result.ok);
            grid_->addWidget(b, i / 3, i % 3);
            connect(b, &QPushButton::clicked, this, [this, i] { render(runtime_.click(i)); });
        }
        buttons_->setVisible(!result.buttons.isEmpty());
    }
    void run() {
        const QByteArray source = system_->read(path_);
        QString error = system_->lastError();
        if (!error.isEmpty()) {
            render({false, {}, error, {}, {}, {}});
            return;
        }
        render(runtime_.run(source, path_));
    }

  public:
    Program(SimSystem *s, const QString &path, QWidget *parent)
        : QWidget(parent), system_(s), path_(s->normalize(path)), runtime_(s) {
        auto *l = new QVBoxLayout(this);
        auto *top = new QHBoxLayout;
        title_ = new QLabel("Lua program");
        title_->setTextFormat(Qt::PlainText);
        title_->setObjectName("luaTitle");
        title_->setWordWrap(true);
        top->addWidget(title_, 1);
        auto *reload = new QPushButton("Reload / run");
        reload->setObjectName("luaReload");
        top->addWidget(reload);
        l->addLayout(top);
        auto *source = new QLabel(path_);
        source->setTextFormat(Qt::PlainText);
        source->setWordWrap(true);
        l->addWidget(source);
        body_ = new QPlainTextEdit;
        body_->setObjectName("luaText");
        body_->setReadOnly(true);
        body_->setMinimumHeight(90);
        l->addWidget(body_, 2);
        buttons_ = new QWidget;
        grid_ = new QGridLayout(buttons_);
        grid_->setContentsMargins(0, 0, 0, 0);
        l->addWidget(buttons_);
        console_ = new QPlainTextEdit;
        console_->setObjectName("luaOutput");
        console_->setReadOnly(true);
        console_->setMaximumBlockCount(1500);
        console_->setMinimumHeight(80);
        l->addWidget(console_, 1);
        auto *status = new QLabel("Lua 5.4 / virtual files only / bounded execution");
        status->setWordWrap(true);
        l->addWidget(status);
        connect(reload, &QPushButton::clicked, this, [this] { run(); });
        run();
    }
};
} // namespace
QWidget *createLuaProgram(SimSystem *s, const QString &path, QWidget *parent) {
    return new Program(s, path, parent);
}
QWidget *createLuaStudio(SimSystem *s, AppLauncher launch, QWidget *parent) {
    auto *w = new QWidget(parent);
    auto *l = new QVBoxLayout(w);
    auto *intro = new QLabel("LUA STUDIO / programs and system mods\nDrop a .lua file into mods/, install "
                             "with aurora -i name, then run name in Terminal.");
    intro->setWordWrap(true);
    l->addWidget(intro);
    auto *folderRow = new QHBoxLayout;
    auto *folder = new QLineEdit(s->setting("modsDirectory").toString());
    folder->setReadOnly(true);
    folder->setObjectName("modsDirectory");
    folderRow->addWidget(folder, 1);
    auto *choose = new QPushButton("Choose folder");
    folderRow->addWidget(choose);
    auto *open = new QPushButton("Open folder");
    folderRow->addWidget(open);
    l->addLayout(folderRow);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("modsTable");
    table->setHorizontalHeaderLabels({"NAME", "VERSION", "STATE", "DESCRIPTION"});
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->hide();
    l->addWidget(table, 1);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumHeight(90);
    log->setObjectName("modsLog");
    auto refresh = [s, folder, table, log] {
        folder->setText(s->setting("modsDirectory").toString());
        QString problem;
        auto available = availableMods(s, &problem);
        if (!problem.isEmpty())
            log->setPlainText(problem);
        auto installed = installedMods(s);
        QMap<QString, AuroraMod> all;
        for (const auto &m : available)
            all.insert(m.name, m);
        for (const auto &m : installed)
            all.insert(m.name, m);
        const QString selectedName =
            table->currentRow() < 0 ? QString() : table->item(table->currentRow(), 0)->text();
        table->setRowCount(0);
        int selectedRow = 0;
        for (const auto &m : all) {
            int row = table->rowCount();
            table->insertRow(row);
            if (m.name == selectedName)
                selectedRow = row;
            const QStringList cells = {m.name, m.version,
                                       isModInstalled(s, m.name) ? "installed" : "available", m.description};
            for (int col = 0; col < 4; ++col)
                table->setItem(row, col, new QTableWidgetItem(cells[col]));
        }
        if (table->rowCount())
            table->selectRow(selectedRow);
    };
    auto selected = [table] {
        return table->currentRow() < 0 ? QString() : table->item(table->currentRow(), 0)->text();
    };
    auto *actions = new QHBoxLayout;
    l->addLayout(actions);
    auto button = [w, actions](const QString &name, std::function<void()> fn) {
        auto *b = new QPushButton(name);
        actions->addWidget(b);
        QObject::connect(b, &QPushButton::clicked, w, std::move(fn));
        return b;
    };
    button("Refresh", refresh);
    button("Install", [s, selected, refresh, log] {
        QString name = selected();
        if (name.isEmpty())
            return;
        if (isModNameReserved(s, name)) {
            log->setPlainText("error: this name is reserved for an OS command.");
            return;
        }
        log->setPlainText(installMod(s, name));
        refresh();
    })->setObjectName("modsInstall");
    button("Run", [s, selected, launch, log] {
        QString name = selected();
        if (name.isEmpty())
            return;
        if (!isModInstalled(s, name)) {
            log->setPlainText("Install the mod first.");
            return;
        }
        launch("lua", "/home/guest/.mods/" + name + ".lua");
    })->setObjectName("modsRun");
    button("Edit", [s, selected, launch, log] {
        QString name = selected();
        if (!isModInstalled(s, name)) {
            log->setPlainText("Install the mod first, then edit its virtual copy.");
            return;
        }
        launch("editor", "/home/guest/.mods/" + name + ".lua");
    });
    button("Remove", [s, selected, refresh, log] {
        QString name = selected();
        if (!name.isEmpty()) {
            log->setPlainText(removeMod(s, name));
            refresh();
        }
    });
    auto *fileRow = new QHBoxLayout;
    auto *path = new QLineEdit("/home/guest/docs/my-program.lua");
    path->setObjectName("luaFilePath");
    fileRow->addWidget(path, 1);
    auto *runFile = new QPushButton("Run file");
    fileRow->addWidget(runFile);
    auto *newFile = new QPushButton("New program");
    fileRow->addWidget(newFile);
    l->addLayout(fileRow);
    l->addWidget(log);
    QObject::connect(choose, &QPushButton::clicked, w, [w, s, refresh] {
        QString path = QFileDialog::getExistingDirectory(w, "Choose your mods folder");
        if (!path.isEmpty()) {
            s->setSetting("modsDirectory", path);
            refresh();
        }
    });
    QObject::connect(open, &QPushButton::clicked, w, [s, log] {
        const QString path = s->setting("modsDirectory").toString();
        if (!QDir(path).exists() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
            log->setPlainText("Could not open this folder. Choose an existing mods folder.");
    });
    QObject::connect(runFile, &QPushButton::clicked, w,
                     [s, path, launch] { launch("lua", s->normalize(path->text())); });
    QObject::connect(newFile, &QPushButton::clicked, w, [w, s, path, launch, log] {
        QString target = s->normalize(path->text());
        if (s->exists(target) &&
            QMessageBox::question(w, "Replace Lua file", target + " already exists. Replace it?",
                                  QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!s->write(target, Template)) {
            log->setPlainText(s->lastError());
            return;
        }
        launch("editor", target);
    });
    QObject::connect(s, &SimSystem::changed, w, refresh);
    refresh();
    return w;
}
