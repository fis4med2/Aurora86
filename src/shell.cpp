#include "shell.h"
#include "mods.h"
#include "packages.h"

#include <QDateTime>
#include <QRegularExpression>

namespace {
QString mode(int permissions, bool directory) {
    QString result = directory ? "d" : "-";
    const char *letters = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        result += permissions & (1 << (8 - i)) ? QChar(letters[i]) : QChar('-');
    return result;
}
QStringList tokenize(const QString &line, QString &error, QList<int> &operators) {
    QStringList tokens;
    QString token;
    QChar quote;
    bool escaped = false, started = false;
    const auto flush = [&] {
        if (started)
            tokens.append(token);
        token.clear();
        started = false;
    };
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            token += ch;
            escaped = false;
            started = true;
            continue;
        }
        if (ch == '\\' && quote != '\'') {
            escaped = true;
            started = true;
            continue;
        }
        if (!quote.isNull()) {
            if (ch == quote)
                quote = {};
            else
                token += ch;
            started = true;
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            started = true;
            continue;
        }
        if (ch.isSpace()) {
            flush();
            continue;
        }
        if (ch == '>') {
            flush();
            operators.append(tokens.size());
            if (i + 1 < line.size() && line[i + 1] == '>') {
                tokens.append(">>");
                ++i;
            } else
                tokens.append(">");
            continue;
        }
        token += ch;
        started = true;
    }
    if (!quote.isNull() || escaped) {
        error = "Unclosed quote or escape.";
        return {};
    }
    flush();
    return tokens;
}
} // namespace

Shell::Shell(SimSystem *system, QObject *parent) : QObject(parent), m_system(system) {}
QString Shell::cwd() const { return m_cwd; }
QString Shell::prompt() const {
    const QString user = m_system->setting("username", "guest").toString();
    return (user.isEmpty() ? QString("guest") : user) +
           "@aurora:" + (m_cwd.startsWith("/home/guest") ? "~" + m_cwd.mid(11) : m_cwd) + "$ ";
}

QString Shell::execute(const QString &line) {
    QString error;
    QList<int> operators;
    auto args = tokenize(line, error, operators);
    if (!error.isEmpty())
        return "error: " + error;
    if (args.isEmpty())
        return {};
    const QString command = args.takeFirst();
    for (int &index : operators)
        --index;
    const auto path = [this](const QString &input) { return m_system->normalize(input, m_cwd); };
    const auto failure = [this] { return "error: " + m_system->lastError(); };
    const auto usage = [](const QString &value) { return "usage: " + value; };
    if (operators.size() > 1 || (!operators.isEmpty() && operators.first() < 0))
        return "error: invalid redirection.";
    const int redirect = !operators.isEmpty() && args[operators.first()] == ">" ? operators.first() : -1;
    const int append = !operators.isEmpty() && args[operators.first()] == ">>" ? operators.first() : -1;
    if (command != "echo" && (redirect >= 0 || append >= 0))
        return "error: redirection is available only with echo.";
    if (command == "help")
        return "ASTER shell / AURORA 1.2\n"
               "ls [-l] [directory]   cd [directory]   pwd\n"
               "cat file              echo text [> or >> file]\n"
               "mkdir directory       touch file        rm [-r] path\n"
               "mv source target      cp source target  chmod 644 file\n"
               "stat path             df                free\n"
               "ps                    kill PID          date\n"
               "uname                 whoami            clear\n"
               "lock                  lock auroraSYNC session\n"
               "edit [file]           files [directory] calc\n"
               "browser [URL]         games             packages\n"
               "prism                 garden            eco\n"
               "snake                 blocks            about\n"
               "aurora -i PACKAGE...   install optional packages\n"
               "aurora --list         browse repository\n"
               "lua FILE.lua          run a Lua program\n"
               "mods                  Lua Studio / local mods\n"
               "exit                  close this terminal\n"
               "Quoted paths, command history and Tab completion are supported.\n"
               "All commands and packages run inside the simulated computer.";
    PackageManager manager(m_system);
    auto builtinPackage = [&manager](const QString &name) {
        if (manager.ownsCommand(name))
            return true;
        for (const auto &p : manager.catalog())
            if (p.name == name)
                return true;
        return false;
    };
    auto modListing = [this](bool installed, const QString &query) {
        QString error;
        const auto mods = installed ? installedMods(m_system, &error) : availableMods(m_system, &error);
        QStringList lines;
        for (const auto &mod : mods)
            if (query.isEmpty() || mod.name.contains(query, Qt::CaseInsensitive) ||
                mod.description.contains(query, Qt::CaseInsensitive))
                lines << QString("%1 %2 [%3] %4")
                             .arg(mod.name, mod.version,
                                  isModInstalled(m_system, mod.name) ? "installed" : "local",
                                  mod.description);
        if (!error.isEmpty())
            lines << "note: " + error;
        return lines.join('\n');
    };
    if (command == "mods") {
        emit launchRequested("lua-studio", {});
        return {};
    }
    if (command == "lua") {
        if (args.size() != 1)
            return "usage: lua FILE.lua";
        const QString target = path(args.first());
        if (!m_system->exists(target) || m_system->isDirectory(target))
            return "error: Lua program not found: " + target;
        emit launchRequested("lua", target);
        return {};
    }
    if (command == "aurora") {
        const QString help = "AURORA Package Manager / local repository\n"
                             "aurora -i PACKAGE...      Install packages and dependencies\n"
                             "aurora -r PACKAGE...      Remove packages\n"
                             "aurora -s TEXT            Search repository\n"
                             "aurora --list             List all available packages\n"
                             "aurora -l                 List installed packages\n"
                             "aurora --info PACKAGE     Package information\n"
                             "aurora --update           Refresh bundled repository index\n"
                             "Try: aurora -i fastfetch htop tree grep coreutils\n"
                             "Lua mods: put name.lua in mods/, then aurora -i name. Run: name\n"
                             "Open Lua Studio with mods. Lua is included; no compiler is needed.\n"
                             "Offline packages and mods use the virtual disk; the host is not modified.";
        if (args.isEmpty() || args.first() == "--help" || args.first() == "-h")
            return help;
        const QString action = args.takeFirst();
        if (action == "-i" || action == "install") {
            if (args.isEmpty())
                return "usage: aurora -i PACKAGE...";
            QStringList native, local;
            QString problem;
            const auto candidates = availableMods(m_system, &problem);
            for (const QString &name : args) {
                if (builtinPackage(name)) {
                    native << name;
                    continue;
                }
                if (isModNameReserved(m_system, name))
                    return "error: reserved command name: " + name;
                bool found = isModInstalled(m_system, name);
                for (const auto &m : candidates)
                    if (m.name == name)
                        found = true;
                if (!found)
                    return "error: unknown package or local mod: " + name + ". Put " + name +
                           ".lua in mods/.";
                local << name;
            }
            QStringList output;
            if (!native.isEmpty()) {
                QString result = manager.install(native);
                output << result;
                if (result.startsWith("error:"))
                    return output.join('\n');
            }
            for (const auto &name : local) {
                bool localSource = false;
                for (const auto &mod : candidates)
                    if (mod.name == name)
                        localSource = true;
                QString result = (!localSource && isModInstalled(m_system, name))
                                     ? "Already installed local Lua mod " + name + "."
                                     : installMod(m_system, name);
                output << result;
                if (result.startsWith("error:"))
                    break;
            }
            return output.join('\n');
        }
        if (action == "-r" || action == "remove") {
            if (args.isEmpty())
                return "usage: aurora -r PACKAGE...";
            QStringList native, output;
            for (const auto &name : args)
                if (isModInstalled(m_system, name))
                    output << removeMod(m_system, name);
                else
                    native << name;
            if (!native.isEmpty())
                output << manager.remove(native);
            return output.join('\n');
        }
        if (action == "-s" || action == "search")
            return manager.search(args.join(' ')) + "\n\nLocal Lua mods:\n" +
                   modListing(false, args.join(' '));
        if (action == "--list" || action == "list")
            return args.isEmpty() ? manager.search({}) + "\n\nLocal Lua mods:\n" + modListing(false, {})
                                  : "usage: aurora --list";
        if (action == "--info" || action == "info") {
            if (args.size() != 1)
                return "usage: aurora --info PACKAGE";
            if (builtinPackage(args.first()))
                return manager.info(args.first());
            auto choices = availableMods(m_system);
            choices.append(installedMods(m_system));
            for (const auto &mod : choices)
                if (mod.name == args.first())
                    return QString("%1 %2 / Lua mod\n%3\nSource: %4 bytes\nState: %5")
                        .arg(mod.name, mod.version, mod.description)
                        .arg(mod.source.size())
                        .arg(isModInstalled(m_system, mod.name) ? "installed" : "available");
            return "error: unknown package or Lua mod: " + args.first();
        }
        if (action == "--update" || action == "update")
            return args.isEmpty()
                       ? QString("Repository index is current: %1 bundled packages. No network required.")
                             .arg(manager.catalog().size())
                       : "usage: aurora --update";
        if (action == "-l" || action == "--installed") {
            if (!args.isEmpty())
                return "usage: aurora -l";
            const auto installed = manager.installed();
            return (installed.isEmpty()
                        ? QString("No native packages installed. Try: aurora -i fastfetch htop")
                        : "Installed packages:\n" + installed.join('\n')) +
                   "\n\nInstalled Lua mods:\n" + modListing(true, {});
        }
        return "error: unknown aurora option: " + action + "\n" + help;
    }
    if (manager.ownsCommand(command)) {
        if (!manager.isInstalled(command))
            return manager.run(command, args, m_cwd);
        if (command == "htop") {
            if (!args.isEmpty() && args != QStringList{"--help"})
                return "usage: htop";
            if (!args.isEmpty())
                return "htop: F2 sort, F3 filter, F9 terminate, F10 quit. Simulated processes only.";
            emit launchRequested("htop", {});
            return {};
        }
        if (command == "nano" || command == "less") {
            if (args.size() > 1 || (command == "less" && args.isEmpty()))
                return "usage: " + command + " [file]";
            emit launchRequested(command == "nano" ? "editor" : "pager",
                                 args.isEmpty() ? QString() : path(args.first()));
            return {};
        }
        return manager.run(command, args, m_cwd);
    }
    if (command == "pwd")
        return m_cwd;
    if (command == "whoami")
        return m_system->setting("username", "guest").toString();
    if (command == "lock") {
        emit launchRequested("lock", {});
        return "Session locked.";
    }
    if (command == "uname")
        return "AURORA/86 1.2  A16  8MHz  4096KiB  October1986";
    if (command == "date") {
        const auto now = QDateTime::currentDateTime();
        auto vintage = now;
        vintage.setDate(QDate(1986, now.date().month(),
                              qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
        return now.addSecs(m_system->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong())
            .toString("dd/MM/yyyy HH:mm:ss");
    }
    if (QStringList{"prisma", "jardim", "prism", "garden", "eco", "games", "packages"}.contains(command)) {
        emit launchRequested((command == "prisma" || command == "prism")    ? "prism"
                             : (command == "jardim" || command == "garden") ? "garden"
                             : command == "eco"                             ? "echo"
                             : command == "packages"                        ? "packages"
                                                                            : "games",
                             {});
        return {};
    }
    if (command == "clear") {
        emit clearRequested();
        return {};
    }
    if (command == "exit") {
        emit launchRequested("close", {});
        return {};
    }
    if (command == "shutdown") {
        emit shutdownRequested();
        return "Synchronizing volumes...";
    }
    if (command == "cd") {
        if (args.size() > 1)
            return usage("cd [directory]");
        const auto target = path(args.isEmpty() ? "~" : args.first());
        if (!m_system->isDirectory(target))
            return "error: directory unavailable: " + target;
        m_system->list(target);
        if (!m_system->lastError().isEmpty())
            return failure();
        m_cwd = target;
        return {};
    }
    if (command == "ls") {
        const bool detailed = args.removeAll("-l") > 0;
        if (args.size() > 1)
            return usage("ls [-l] [directory]");
        const auto entries = m_system->list(path(args.isEmpty() ? m_cwd : args.first()));
        if (!m_system->lastError().isEmpty())
            return failure();
        QStringList rows;
        for (const auto &entry : entries) {
            const auto name = entry.path.section('/', -1) + (entry.directory ? "/" : "");
            rows << (detailed ? QString("%1 guest %2 %3")
                                    .arg(mode(entry.permissions, entry.directory))
                                    .arg(entry.data.size(), 7)
                                    .arg(name)
                              : name);
        }
        return rows.join(detailed ? "\n" : "  ");
    }
    if (command == "cat") {
        if (args.size() != 1)
            return usage("cat file");
        const auto data = m_system->read(path(args.first()));
        return m_system->lastError().isEmpty() ? QString::fromUtf8(data) : failure();
    }
    if (command == "echo") {
        const int index = redirect >= 0 ? redirect : append;
        if (index < 0)
            return args.join(' ');
        if (index != args.size() - 2 || (redirect >= 0 && append >= 0))
            return usage("echo text > file");
        const auto target = path(args.last());
        QByteArray data;
        if (append >= 0 && m_system->exists(target)) {
            data = m_system->read(target);
            if (!m_system->lastError().isEmpty())
                return failure();
        }
        data += args.mid(0, index).join(' ').toUtf8() + '\n';
        return m_system->write(target, data) ? QString() : failure();
    }
    if (command == "mkdir" || command == "touch") {
        if (args.size() != 1)
            return usage(command + " path");
        const auto target = path(args.first());
        if (command == "touch" && m_system->exists(target))
            return {};
        return (command == "mkdir" ? m_system->mkdir(target) : m_system->write(target, {})) ? QString()
                                                                                            : failure();
    }
    if (command == "rm") {
        const bool recursive = args.removeAll("-r") > 0;
        if (args.size() != 1)
            return usage("rm [-r] path");
        return m_system->remove(path(args.first()), recursive) ? QString() : failure();
    }
    if (command == "mv" || command == "cp") {
        if (args.size() != 2)
            return usage(command + " source destination");
        return (command == "mv" ? m_system->move(path(args[0]), path(args[1]))
                                : m_system->copy(path(args[0]), path(args[1])))
                   ? QString()
                   : failure();
    }
    if (command == "chmod") {
        if (args.size() != 2 || !QRegularExpression("^[0-7]{3}$").match(args[0]).hasMatch())
            return usage("chmod 644 file");
        return m_system->chmod(path(args[1]), args[0].toInt(nullptr, 8)) ? QString() : failure();
    }
    if (command == "stat") {
        if (args.size() != 1)
            return usage("stat path");
        const auto target = path(args.first());
        if (!m_system->exists(target))
            return "error: file unavailable.";
        if (target == "/")
            return "/  protected directory";
        for (const auto &entry :
             m_system->list(target.section('/', 0, -2).isEmpty() ? "/" : target.section('/', 0, -2)))
            if (entry.path == target)
                return QString("%1\n%2 guest  %3 bytes")
                    .arg(target, mode(entry.permissions, entry.directory))
                    .arg(entry.data.size());
        return failure();
    }
    if (command == "df")
        return QString("Volume    Capacity    Used      Free\nAURORA-0  %1 KiB    %2 KiB   %3 KiB")
            .arg(SimSystem::DiskBytes / 1024)
            .arg((m_system->usedBytes() + 1023) / 1024)
            .arg((SimSystem::DiskBytes - m_system->usedBytes()) / 1024);
    if (command == "free")
        return QString("RAM      Total       Used       Free\n         %1 KiB   %2 KiB    %3 KiB")
            .arg(SimSystem::MemoryKiB)
            .arg(m_system->usedMemoryKiB())
            .arg(SimSystem::MemoryKiB - m_system->usedMemoryKiB());
    if (command == "ps") {
        QString result = "PID   RAM/KiB  COMMAND";
        for (const auto &process : m_system->processes())
            result += QString("\n%1 %2  %3").arg(process.pid, 4).arg(process.memoryKiB, 7).arg(process.name);
        return result;
    }
    if (command == "kill") {
        bool ok = false;
        const int pid = args.size() == 1 ? args.first().toInt(&ok) : -1;
        if (!ok)
            return usage("kill PID");
        return m_system->stopProcess(pid) ? QString() : failure();
    }
    if (QStringList{"edit", "files", "calc", "browser", "snake", "blocks", "about"}.contains(command)) {
        if (args.size() > 1)
            return usage(command + " [path]");
        QString target = args.isEmpty() ? QString() : args.first();
        if (command == "edit" || command == "files")
            target = path(target.isEmpty() && command == "files" ? m_cwd : target);
        if (command == "edit" && args.isEmpty())
            target.clear();
        emit launchRequested(command, target);
        return {};
    }
    if (isModInstalled(m_system, command)) {
        if (!args.isEmpty())
            return "usage: " + command + " (this Lua application takes no command-line arguments)";
        emit launchRequested("lua", "/home/guest/.mods/" + command + ".lua");
        return {};
    }
    return "error: unknown command: " + command + "\nType help to list commands.";
}
