#include "package_apps.h"
#include "packages.h"
#include <QtWidgets>
#include <algorithm>
#include <cmath>

namespace {
QVBoxLayout *column(QWidget *widget) {
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    return layout;
}
void closeApplication(QWidget *widget) {
    for (QWidget *ancestor = widget; ancestor; ancestor = ancestor->parentWidget())
        if (auto *window = qobject_cast<QMdiSubWindow *>(ancestor)) {
            window->close();
            return;
        }
    widget->close();
}
void bind(QWidget *widget, int key, std::function<void()> action) {
    auto *shortcut = new QShortcut(QKeySequence(key), widget);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, widget, std::move(action));
}
double cpuFor(const SimProcess &process, qint64 seconds) {
    const double activity = 0.6 + process.memoryKiB / 80.0;
    return qMin(42., activity * (0.7 + 0.3 * std::sin(seconds * .7 + process.pid)));
}
class Htop final : public QWidget {
    SimSystem *system_;
    QLabel *summary_, *hint_;
    QProgressBar *cpu_, *ram_;
    QLineEdit *filter_;
    QComboBox *sort_;
    QTableWidget *table_;
    QTimer *timer_;

  public:
    Htop(SimSystem *system, QWidget *parent) : QWidget(parent), system_(system) {
        setObjectName("htopPanel");
        auto *layout = column(this);
        auto *heading = new QLabel("htop / AURORA process viewer");
        QFont bold = heading->font();
        bold.setBold(true);
        heading->setFont(bold);
        layout->addWidget(heading);
        summary_ = new QLabel;
        summary_->setObjectName("htopSummary");
        summary_->setWordWrap(true);
        layout->addWidget(summary_);
        cpu_ = new QProgressBar;
        cpu_->setRange(0, 1000);
        layout->addWidget(cpu_);
        ram_ = new QProgressBar;
        ram_->setRange(0, SimSystem::MemoryKiB);
        layout->addWidget(ram_);
        auto *tools = new QHBoxLayout;
        tools->addWidget(new QLabel("Filter [F3]"));
        filter_ = new QLineEdit;
        filter_->setObjectName("htopFilter");
        filter_->setPlaceholderText("Process name or PID");
        tools->addWidget(filter_, 1);
        sort_ = new QComboBox;
        sort_->setObjectName("htopSort");
        sort_->addItems({"Memory / descending", "CPU / descending", "PID / ascending"});
        sort_->setItemData(0, "memory");
        sort_->setItemData(1, "cpu");
        sort_->setItemData(2, "pid");
        tools->addWidget(sort_);
        layout->addLayout(tools);
        table_ = new QTableWidget(0, 5);
        table_->setObjectName("htopTable");
        table_->setHorizontalHeaderLabels({"PID", "USER", "RAM KiB", "CPU % *", "COMMAND"});
        table_->verticalHeader()->hide();
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        table_->setColumnWidth(0, 65);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->setColumnWidth(2, 110);
        table_->setColumnWidth(3, 105);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(table_, 1);
        hint_ = new QLabel("* CPU activity is simulated. RAM and process lists follow the A16 system.");
        hint_->setWordWrap(true);
        layout->addWidget(hint_);
        auto *row = new QHBoxLayout;
        auto *sorting = new QPushButton("Sort [F2]");
        auto *terminate = new QPushButton("Terminate [F9]");
        auto *quit = new QPushButton("Quit [F10]");
        row->addWidget(sorting);
        row->addWidget(terminate);
        row->addWidget(quit);
        layout->addLayout(row);
        auto cycle = [this] { sort_->setCurrentIndex((sort_->currentIndex() + 1) % sort_->count()); };
        connect(sorting, &QPushButton::clicked, this, cycle);
        connect(terminate, &QPushButton::clicked, this, [this] { killSelected(); });
        connect(quit, &QPushButton::clicked, this, [this] { closeApplication(this); });
        bind(this, Qt::Key_F2, cycle);
        bind(this, Qt::Key_F3, [this] {
            filter_->setFocus();
            filter_->selectAll();
        });
        bind(this, Qt::Key_F9, [this] { killSelected(); });
        bind(this, Qt::Key_F10, [this] { closeApplication(this); });
        connect(filter_, &QLineEdit::textChanged, this, [this] { refresh(); });
        connect(sort_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
        connect(system_, &SimSystem::processesChanged, this, [this] { refresh(); });
        connect(system_, &SimSystem::changed, this, [this] { refresh(); });
        connect(system_, &SimSystem::settingsChanged, this, [this] { refresh(); });
        timer_ = new QTimer(this);
        timer_->setInterval(900);
        connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
        setFocusProxy(table_);
        refresh();
    }

  protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        timer_->start();
        refresh();
    }
    void hideEvent(QHideEvent *event) override {
        timer_->stop();
        QWidget::hideEvent(event);
    }

  private:
    void refresh() {
        int selected = -1;
        if (table_->currentRow() >= 0)
            selected = table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toInt();
        auto processes = system_->processes();
        const qint64 seconds = system_->uptimeSeconds();
        const int ordering = sort_->currentIndex();
        std::sort(processes.begin(), processes.end(),
                  [ordering, seconds](const SimProcess &a, const SimProcess &b) {
                      if (ordering == 0 && a.memoryKiB != b.memoryKiB)
                          return a.memoryKiB > b.memoryKiB;
                      if (ordering == 1 && cpuFor(a, seconds) != cpuFor(b, seconds))
                          return cpuFor(a, seconds) > cpuFor(b, seconds);
                      return a.pid < b.pid;
                  });
        double totalCpu = 0;
        for (const auto &process : processes)
            totalCpu += cpuFor(process, seconds);
        totalCpu = qMin(100., totalCpu);
        cpu_->setValue(qRound(totalCpu * 10));
        cpu_->setFormat(QString("A16 CPU (simulated)  %1%").arg(totalCpu, 0, 'f', 1));
        ram_->setValue(system_->usedMemoryKiB());
        ram_->setFormat("RAM  %v / %m KiB");
        summary_->setText(QString("Uptime %1:%2:%3 | %4 processes | Disk %5 / 2048 KiB")
                              .arg(seconds / 3600)
                              .arg((seconds / 60) % 60, 2, 10, QChar('0'))
                              .arg(seconds % 60, 2, 10, QChar('0'))
                              .arg(processes.size())
                              .arg((system_->usedBytes() + 1023) / 1024));
        table_->setRowCount(0);
        int selectedRow = -1;
        const QString query = filter_->text().trimmed();
        for (const auto &process : processes) {
            if (!query.isEmpty() && !process.name.contains(query, Qt::CaseInsensitive) &&
                !QString::number(process.pid).contains(query))
                continue;
            const int row = table_->rowCount();
            table_->insertRow(row);
            auto *pid = new QTableWidgetItem(QString::number(process.pid));
            pid->setData(Qt::UserRole, process.pid);
            table_->setItem(row, 0, pid);
            table_->setItem(row, 1,
                            new QTableWidgetItem(process.pid < 3
                                                     ? QString("system")
                                                     : system_->setting("username", "guest").toString()));
            table_->setItem(row, 2, new QTableWidgetItem(QString::number(process.memoryKiB)));
            table_->setItem(row, 3, new QTableWidgetItem(QString::number(cpuFor(process, seconds), 'f', 1)));
            table_->setItem(row, 4, new QTableWidgetItem(process.name));
            if (process.pid == selected)
                selectedRow = row;
        }
        if (table_->rowCount())
            table_->selectRow(selectedRow >= 0 ? selectedRow : 0);
    }
    void killSelected() {
        if (table_->currentRow() < 0)
            return;
        const int pid = table_->item(table_->currentRow(), 0)->data(Qt::UserRole).toInt();
        if (pid < 3) {
            hint_->setText("Essential system processes cannot be terminated.");
            return;
        }
        if (QMessageBox::question(
                this, "Terminate process",
                QString("Terminate simulated PID %1? Unsaved changes will be lost.").arg(pid),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        if (!system_->stopProcess(pid))
            hint_->setText(system_->lastError());
    }
};
} // namespace
QWidget *createHtop(SimSystem *system, QWidget *parent) { return new Htop(system, parent); }
QWidget *createTextPager(SimSystem *system, const QString &path, QWidget *parent) {
    auto *widget = new QWidget(parent);
    auto *layout = column(widget);
    auto *label = new QLabel(path);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto *text = new QPlainTextEdit;
    text->setObjectName("pagerText");
    text->setReadOnly(true);
    const QByteArray bytes = system->read(path);
    const QString error = system->lastError();
    text->setPlainText(error.isEmpty() ? QString::fromUtf8(bytes) : "error: " + error);
    layout->addWidget(text, 1);
    layout->addWidget(new QLabel("Space / Page Down: next page  |  Page Up: previous  |  Q: quit"));
    bind(widget, Qt::Key_Q, [widget] { closeApplication(widget); });
    bind(widget, Qt::Key_Space,
         [text] { text->verticalScrollBar()->triggerAction(QAbstractSlider::SliderPageStepAdd); });
    widget->setFocusProxy(text);
    return widget;
}
QWidget *createPackageManager(SimSystem *system, AppLauncher launch, QWidget *parent) {
    auto *widget = new QWidget(parent);
    auto *layout = column(widget);
    auto *intro = new QLabel("AURORA Package Center / offline repository\nInstall useful tools on your "
                             "virtual disk. All packages are simulated.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *row = new QHBoxLayout;
    auto *filter = new QLineEdit;
    filter->setPlaceholderText("Search package name or description");
    row->addWidget(filter, 1);
    layout->addLayout(row);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("packageTable");
    table->setHorizontalHeaderLabels({"PACKAGE", "VERSION", "SIZE", "STATE"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column : {1, 2, 3})
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);
    auto *info = new QLabel("Select a package to read its description.");
    info->setWordWrap(true);
    info->setMinimumHeight(55);
    layout->addWidget(info);
    auto selected = [table] {
        return table->currentRow() < 0 ? QString() : table->item(table->currentRow(), 0)->text();
    };
    auto refresh = [system, table, filter] {
        const QString old = table->currentRow() < 0 ? QString() : table->item(table->currentRow(), 0)->text();
        PackageManager manager(system);
        const auto installed = manager.installed();
        table->setRowCount(0);
        int current = -1;
        for (const auto &package : manager.catalog()) {
            if (!filter->text().isEmpty() &&
                !(package.name + " " + package.description).contains(filter->text(), Qt::CaseInsensitive))
                continue;
            int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(package.name));
            table->setItem(r, 1, new QTableWidgetItem(package.version));
            table->setItem(
                r, 2, new QTableWidgetItem(QString("%1 KiB").arg((package.installedBytes + 1023) / 1024)));
            table->setItem(
                r, 3, new QTableWidgetItem(installed.contains(package.name) ? "Installed" : "Available"));
            if (package.name == old)
                current = r;
        }
        if (table->rowCount())
            table->selectRow(current >= 0 ? current : 0);
    };
    auto *inputRow = new QHBoxLayout;
    auto *input = new QLineEdit;
    input->setObjectName("packageInput");
    input->setPlaceholderText("fastfetch htop tree");
    inputRow->addWidget(input, 1);
    auto *install = new QPushButton("Install");
    install->setObjectName("packageInstall");
    auto *remove = new QPushButton("Remove");
    auto *run = new QPushButton("Open / try");
    inputRow->addWidget(install);
    inputRow->addWidget(remove);
    inputRow->addWidget(run);
    layout->addLayout(inputRow);
    auto *log = new QPlainTextEdit;
    log->setObjectName("packageLog");
    log->setReadOnly(true);
    log->setMaximumBlockCount(400);
    log->setMaximumHeight(120);
    log->setPlainText("Terminal: aurora -i fastfetch htop\nUse aurora --list to explore the repository.");
    layout->addWidget(log);
    auto names = [input, selected] {
        QString text = input->text().trimmed();
        if (text.isEmpty())
            text = selected();
        return text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    };
    QObject::connect(install, &QPushButton::clicked, widget, [system, log, names] {
        PackageManager manager(system);
        log->appendPlainText(manager.install(names()));
    });
    QObject::connect(remove, &QPushButton::clicked, widget, [system, log, names] {
        PackageManager manager(system);
        log->appendPlainText(manager.remove(names()));
    });
    QObject::connect(table, &QTableWidget::itemSelectionChanged, widget, [system, selected, info] {
        PackageManager manager(system);
        const QString name = selected();
        if (!name.isEmpty())
            info->setText(manager.info(name));
    });
    QObject::connect(filter, &QLineEdit::textChanged, widget, [refresh] { refresh(); });
    QObject::connect(system, &SimSystem::changed, widget, refresh);
    QObject::connect(run, &QPushButton::clicked, widget, [system, selected, log, launch] {
        const QString name = selected();
        PackageManager manager(system);
        if (!manager.isInstalled(name)) {
            log->appendPlainText("Install the selected package first.");
            return;
        }
        if (name == "htop")
            launch("htop", {});
        else if (name == "nano")
            launch("editor", {});
        else if (name == "less")
            launch("pager", "/etc/release");
        else {
            const QMap<QString, QString> samples = {
                {"coreutils", "wc /etc/release"},  {"sysutils", "uptime"},
                {"findutils", "find /home/guest"}, {"hexutils", "xxd /etc/release"},
                {"manuals", "man aurora"},         {"grep", "grep AURORA /etc/release"},
                {"figlet", "figlet AURORA"},       {"cowsay", "cowsay Hello from AURORA"}};
            launch("terminal", samples.value(name, name));
        }
    });
    refresh();
    return widget;
}
