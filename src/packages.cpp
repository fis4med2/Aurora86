#include "packages.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMap>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
const QString Registry = "/home/guest/.aurora-packages.json";
const QList<AuroraPackage> &repository() {
    static const QList<AuroraPackage> packages = {
        {"coreutils", "1.0", "Text and disk tools: wc, head, tail, sort, uniq, base64 and du.", {}, 12288},
        {"sysutils", "1.0", "Virtual machine uptime and session diagnostics.", {}, 3072},
        {"fastfetch",
         "1.0",
         "Display A16 hardware, memory, disk, uptime and package information.",
         {"sysutils"},
         8192},
        {"neofetch", "1.0", "An alternative ASCII system summary powered by fastfetch.", {"fastfetch"}, 4096},
        {"htop",
         "1.0",
         "Interactive virtual process viewer with sorting and process termination.",
         {"sysutils"},
         12288},
        {"tree", "1.0", "Draw a directory tree from the virtual filesystem.", {}, 6144},
        {"grep", "1.0", "Search virtual text files using regular expressions.", {}, 6144},
        {"findutils", "1.0", "Find virtual files by wildcard name, type and depth.", {}, 6144},
        {"hexutils", "1.0", "Inspect virtual binary files using xxd and hexdump.", {}, 4096},
        {"fortune", "1.0", "Read original short messages from the Aurora workshop.", {}, 3072},
        {"cowsay", "1.0", "Let an ASCII cow say a message or a workshop fortune.", {"fortune"}, 4096},
        {"figlet", "1.0", "Render words as large, seven-row ASCII lettering.", {}, 6144},
        {"cal", "1.0", "Show month and year calendars using the virtual clock.", {}, 3072},
        {"nano", "1.0", "Open a virtual document in Writer, the native text editor.", {}, 8192},
        {"less", "1.0", "Read virtual text files in an interactive terminal pager.", {}, 4096},
        {"manuals", "1.0", "Read installed utility manuals with man.", {}, 8192},
    };
    return packages;
}
const QMap<QString, QString> &commands() {
    static const QMap<QString, QString> map = {
        {"wc", "coreutils"},        {"head", "coreutils"},    {"tail", "coreutils"}, {"sort", "coreutils"},
        {"uniq", "coreutils"},      {"base64", "coreutils"},  {"du", "coreutils"},   {"uptime", "sysutils"},
        {"fastfetch", "fastfetch"}, {"neofetch", "neofetch"}, {"htop", "htop"},      {"tree", "tree"},
        {"grep", "grep"},           {"find", "findutils"},    {"xxd", "hexutils"},   {"hexdump", "hexutils"},
        {"fortune", "fortune"},     {"cowsay", "cowsay"},     {"figlet", "figlet"},  {"cal", "cal"},
        {"nano", "nano"},           {"less", "less"},         {"man", "manuals"},
    };
    return map;
}
QString packageName(const QString &name) { return commands().value(name, name); }
const AuroraPackage *package(const QString &name) {
    const QString resolved = packageName(name);
    for (const auto &p : repository())
        if (p.name == resolved)
            return &p;
    return nullptr;
}
QString error(const QString &message) { return "error: " + message; }
QString usage(const QString &message) { return "error: usage: " + message; }
QByteArray payload(const AuroraPackage &p) {
    QByteArray bytes = ("AURORA/86 simulated package\nName: " + p.name + "\nVersion: " + p.version + "\n" +
                        p.description + "\nUse man COMMAND for help.\n")
                           .toUtf8();
    bytes.append(QByteArray(qMax(0, p.installedBytes - int(bytes.size())), '.'));
    return bytes;
}
bool load(SimSystem *system, QMap<QString, QJsonObject> &installed, QString &message) {
    installed.clear();
    message.clear();
    if (!system->exists(Registry)) {
        message = system->lastError();
        return message.isEmpty();
    }
    const QByteArray bytes = system->read(Registry);
    message = system->lastError();
    if (!message.isEmpty())
        return false;
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    const auto fail = [&] {
        message = "Package database is invalid; the original file was preserved. "
                  "Restore " +
                  Registry + " from a backup or rename it to start a new package database.";
        return false;
    };
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail();
    const auto root = doc.object();
    if (root.value("format").toInt() != 1 || !root.value("packages").isArray())
        return fail();
    for (const auto &value : root.value("packages").toArray()) {
        if (!value.isObject())
            return fail();
        const auto entry = value.toObject();
        const QString name = entry.value("name").toString();
        const auto *p = package(name);
        if (!p || name != p->name || installed.contains(name) ||
            entry.value("version").toString() != p->version || !entry.value("payload").isString() ||
            entry.value("payload").toString().toUtf8() != payload(*p))
            return fail();
        installed.insert(name, entry);
    }
    for (const auto &p : repository())
        if (installed.contains(p.name))
            for (const auto &dependency : p.dependencies)
                if (!installed.contains(dependency))
                    return fail();
    return true;
}
bool save(SimSystem *system, const QMap<QString, QJsonObject> &installed, QString &message) {
    QJsonArray entries;
    for (const auto &entry : installed)
        entries.append(entry);
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{"format", 1}, {"packages", entries}}).toJson(QJsonDocument::Compact);
    if (!system->write(Registry, bytes)) {
        message = system->lastError();
        return false;
    }
    return true;
}
QStringList commandList(const QString &name) {
    QStringList result;
    for (auto it = commands().cbegin(); it != commands().cend(); ++it)
        if (it.value() == name)
            result << it.key();
    return result;
}
QString manual(const QString &command) {
    static const QMap<QString, QString> help = {
        {"aurora", "AURORA(1) — virtual package manager\n"
                   "aurora -i PACKAGE...   install packages and dependencies\n"
                   "aurora -r PACKAGE...   remove packages (refuse required dependencies)\n"
                   "aurora -s TEXT         search the offline catalog\n"
                   "aurora -l              list installed packages\n"
                   "aurora --info PACKAGE  show package details\n"
                   "aurora --update        refresh the built-in offline catalog\n"
                   "Command names such as wc and xxd also select their owning package.\n"
                   "Packages use the virtual disk only; no Linux programs are installed."},
        {"fastfetch", "FASTFETCH(1)\nfastfetch [--logo none]\nShow live simulated hardware, memory, disk, "
                      "packages and uptime."},
        {"neofetch",
         "NEOFETCH(1)\nneofetch [--logo none]\nShow a system summary with an alternative ASCII logo."},
        {"htop", "HTOP(1)\nhtop\nOpen the live virtual process viewer. Sort, filter and terminate user "
                 "processes.\nThe AURORA kernel and desktop are protected."},
        {"tree", "TREE(1)\ntree [-a] [-L DEPTH] [PATH]\nList the virtual directory tree. -a includes hidden "
                 "files; depth is limited to 32."},
        {"grep", "GREP(1)\ngrep [-i] [-n] [-v] [-c] PATTERN FILE...\nSearch regular expressions. Ignore "
                 "case, number lines, invert matches or count matches."},
        {"wc", "WC(1)\nwc [-lwmc] FILE...\nCount newlines, words, Unicode characters and bytes. Default: "
               "lines, words, bytes."},
        {"head", "HEAD(1)\nhead [-n COUNT] FILE\nPrint the first COUNT lines (default 10)."},
        {"tail", "TAIL(1)\ntail [-n COUNT] FILE\nPrint the last COUNT lines (default 10)."},
        {"find", "FIND(1)\nfind [PATH] [-name WILDCARD] [-type f|d] [-maxdepth COUNT]\nSearch the virtual "
                 "filesystem. The starting path is included."},
        {"sort",
         "SORT(1)\nsort [-nru] FILE\nSort lines; -n sorts numbers, -r reverses and -u removes duplicates."},
        {"uniq", "UNIQ(1)\nuniq [-c] FILE\nRemove adjacent duplicate lines. -c prefixes occurrence counts."},
        {"base64", "BASE64(1)\nbase64 [-d|--decode] FILE [OUTPUT]\nEncode or decode virtual files. OUTPUT "
                   "writes exact bytes to the virtual disk."},
        {"du", "DU(1)\ndu [-a] [PATH]\nShow virtual storage use in KiB, including entry metadata. -a "
               "includes each file."},
        {"xxd", "XXD(1)\nxxd [-l BYTES] FILE\nShow offsets, hexadecimal bytes and printable ASCII."},
        {"hexdump", "HEXDUMP(1)\nhexdump [-C] [-n BYTES] FILE\nShow hexadecimal bytes and ASCII. -C selects "
                    "the canonical view."},
        {"fortune", "FORTUNE(1)\nfortune [-s]\nRead an original short message from the Aurora workshop."},
        {"cowsay", "COWSAY(1)\ncowsay [WORDS...]\nAn ASCII cow says your words. Without words it reads a "
                   "workshop fortune."},
        {"figlet", "FIGLET(1)\nfiglet WORDS...\nDraw up to 32 characters in a seven-row ASCII font."},
        {"cal", "CAL(1)\ncal [MONTH YEAR]\ncal YEAR\nShow the virtual current month, a chosen month or all "
                "twelve months of a year."},
        {"uptime",
         "UPTIME(1)\nuptime\nShow elapsed simulated-computer session time and active process count."},
        {"nano", "NANO(1)\nnano [FILE]\nOpen Writer. Ctrl+S saves. File paths are always virtual."},
        {"less", "LESS(1)\nless FILE\nOpen a terminal pager. Use arrows/Page Up/Page Down and Q to leave."},
        {"man", "MAN(1)\nman COMMAND\nRead a utility manual. Manual text is bundled with AURORA packages."},
    };
    return help.value(command);
}
QDateTime clockTime(SimSystem *system) {
    const auto now = QDateTime::currentDateTime();
    auto vintage = now;
    vintage.setDate(QDate(1986, now.date().month(),
                          qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
    return now.addSecs(system->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong());
}
QString duration(qint64 seconds) {
    return QString("%1d %2:%3:%4")
        .arg(seconds / 86400)
        .arg((seconds / 3600) % 24, 2, 10, QChar('0'))
        .arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
QList<QByteArray> fileLines(const QByteArray &data) {
    auto result = data.split('\n');
    if (data.endsWith('\n') || data.isEmpty())
        result.removeLast();
    return result;
}
QString fortune() {
    static const QStringList messages = {
        "A small machine can hold a large idea.",
        "Save your work before chasing a comet.",
        "The best shortcut is a command you understand.",
        "Every directory began as an empty space.",
        "A quiet terminal is waiting for a good question.",
        "One mirror can change the direction of a whole journey.",
        "A backup is a letter to your future self.",
        "Measure the memory, then imagine beyond it.",
        "The garden grows one careful move at a time.",
        "An error message is a clue, not a verdict.",
        "Clear names make small programs feel spacious.",
        "The stars are patient; your unsaved document is not.",
        "A useful tool does one thing and tells you when it cannot.",
        "Read the manual. Then write your own experiment.",
        "Your virtual cow has requested another floppy disk.",
        "A new session is a new chance to organize that folder.",
    };
    return messages[QRandomGenerator::global()->bounded(messages.size())];
}
QString calendar(int month, int year) {
    const QDate first(year, month, 1);
    if (!first.isValid())
        return {};
    QString out =
        QString("%1 %2\nSu Mo Tu We Th Fr Sa\n").arg(QLocale(QLocale::English).monthName(month)).arg(year);
    int weekday = first.dayOfWeek() % 7;
    out += QString(weekday * 3, ' ');
    for (int day = 1; day <= first.daysInMonth(); ++day) {
        out += QString::number(day).rightJustified(2, ' ');
        if ((weekday + day) % 7 == 0)
            out += '\n';
        else if (day < first.daysInMonth())
            out += ' ';
    }
    if (!out.endsWith('\n'))
        out += '\n';
    return out;
}
} // namespace

PackageManager::PackageManager(SimSystem *system) : system_(system) {}
QList<AuroraPackage> PackageManager::catalog() const { return repository(); }
bool PackageManager::ownsCommand(const QString &command) const { return commands().contains(command); }
QStringList PackageManager::installed() const {
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return {};
    return packages.keys();
}
bool PackageManager::isInstalled(const QString &name) const {
    return installed().contains(packageName(name));
}
QString PackageManager::search(const QString &query) const {
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return error(message);
    QStringList rows;
    for (const auto &p : repository()) {
        const auto names = commandList(p.name);
        if (p.name.contains(query, Qt::CaseInsensitive) ||
            p.description.contains(query, Qt::CaseInsensitive) ||
            names.join(' ').contains(query, Qt::CaseInsensitive))
            rows << QString("%1 %2 [%3]\n  %4\n  commands: %5")
                        .arg(p.name, p.version, packages.contains(p.name) ? "installed" : "available",
                             p.description, names.join(", "));
    }
    return rows.isEmpty() ? "No packages match '" + query + "'." : rows.join('\n');
}
QString PackageManager::info(const QString &name) const {
    const auto *p = package(name);
    if (!p)
        return error("Unknown package: " + name);
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return error(message);
    return QString("Package: %1\nVersion: %2\nState: %3\nPayload: %4 bytes on virtual disk\nDependencies: "
                   "%5\nCommands: %6\n%7")
        .arg(p->name, p->version, packages.contains(p->name) ? "installed" : "available")
        .arg(p->installedBytes)
        .arg(p->dependencies.isEmpty() ? "none" : p->dependencies.join(", "), commandList(p->name).join(", "),
             p->description);
}
QString PackageManager::install(const QStringList &names) {
    if (names.isEmpty())
        return usage("aurora -i PACKAGE...");
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return error(message);
    QStringList added;
    QSet<QString> visiting;
    std::function<bool(const QString &)> resolve = [&](const QString &name) {
        const auto *p = package(name);
        if (!p) {
            message = "Unknown package: " + name + ". Use aurora -s TEXT to search.";
            return false;
        }
        if (packages.contains(p->name))
            return true;
        if (visiting.contains(p->name)) {
            message = "Dependency cycle in " + p->name;
            return false;
        }
        visiting.insert(p->name);
        for (const auto &dependency : p->dependencies)
            if (!resolve(dependency))
                return false;
        visiting.remove(p->name);
        packages.insert(p->name, {{"name", p->name},
                                  {"version", p->version},
                                  {"description", p->description},
                                  {"payload", QString::fromUtf8(payload(*p))}});
        added << p->name;
        return true;
    };
    for (const auto &name : names)
        if (!resolve(name))
            return error(message);
    if (added.isEmpty())
        return "aurora: requested packages are already installed.";
    if (!save(system_, packages, message))
        return error("Installation rolled back: " + message);
    QStringList out{"aurora: using built-in offline repository", "aurora: dependencies resolved"};
    for (const auto &name : added)
        out << "[ OK ] installed " + name + " " + package(name)->version;
    out << QString("%1 package(s) installed. Type man COMMAND for usage after installing manuals.")
               .arg(added.size());
    return out.join('\n');
}
QString PackageManager::remove(const QStringList &names) {
    if (names.isEmpty())
        return usage("aurora -r PACKAGE...");
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return error(message);
    QSet<QString> removed;
    for (const auto &name : names) {
        const auto *p = package(name);
        if (!p)
            return error("Unknown package: " + name);
        if (!packages.contains(p->name))
            return error("Package is not installed: " + p->name);
        removed.insert(p->name);
    }
    for (const auto &p : repository())
        if (packages.contains(p.name) && !removed.contains(p.name))
            for (const auto &dependency : p.dependencies)
                if (removed.contains(dependency))
                    return error("Cannot remove " + dependency + "; it is required by " + p.name +
                                 ". Remove both explicitly.");
    for (const auto &name : removed)
        packages.remove(name);
    if (!save(system_, packages, message))
        return error("Removal rolled back: " + message);
    QStringList ordered = removed.values();
    ordered.sort();
    return "aurora: removed " + ordered.join(", ") + ".";
}

QString PackageManager::run(const QString &command, const QStringList &arguments, const QString &cwd,
                            const QString &standardInput) {
    const QString owner = commands().value(command);
    if (owner.isEmpty())
        return error("Unknown packaged command: " + command);
    QMap<QString, QJsonObject> packages;
    QString message;
    if (!load(system_, packages, message))
        return error(message);
    if (!packages.contains(owner))
        return error(command + " is not installed. Run: aurora -i " + owner);
    auto args = arguments;
    if (args == QStringList{"--help"})
        return manual(command);
    const auto path = [this, &cwd](const QString &value) { return system_->normalize(value, cwd); };
    const auto read = [this, &path, &message](const QString &name, QByteArray &data) {
        data = system_->read(path(name));
        message = system_->lastError();
        return message.isEmpty();
    };
    const auto noArguments = [&] { return args.isEmpty(); };
    // A non-null standardInput means the shell connected this command to a pipe.
    const bool piped = !standardInput.isNull();
    if (command == "man") {
        if (args.size() != 1)
            return usage("man COMMAND");
        const QString text = manual(args.first());
        if (text.isEmpty())
            return error("No manual entry for " + args.first());
        return text;
    }
    if (command == "fastfetch" || command == "neofetch") {
        if (!args.isEmpty() && args != QStringList{"--logo", "none"})
            return usage(command + " [--logo none]");
        const QString username = system_->setting("username", "guest").toString();
        const int width = system_->setting("resolutionWidth", 960).toInt();
        const int height = system_->setting("resolutionHeight", 640).toInt();
        QStringList info = {
            (username.isEmpty() ? QString("guest") : username) + "@aurora",
            "---------------------------",
            "OS: AURORA/86 1.5 / ASTER shell",
            "Host: A16 personal workstation",
            "CPU: A16 / 8 MHz / 16-bit",
            QString("Display: LUME / %1 x %2 / 4 tones")
                .arg(width > 0 ? width : 960)
                .arg(height > 0 ? height : 640),
            QString("Memory: %1 / %2 KiB").arg(system_->usedMemoryKiB()).arg(SimSystem::MemoryKiB),
            QString("Disk: %1 / %2 KiB").arg(system_->usedBytes() / 1024).arg(SimSystem::DiskBytes / 1024),
            QString("Processes: %1").arg(system_->processes().size()),
            QString("Packages: %1 (aurora)").arg(packages.size()),
            "Uptime: " + duration(system_->uptimeSeconds()),
            "Clock: " + clockTime(system_).toString("yyyy-MM-dd HH:mm:ss")};
        const QStringList logo =
            command == "neofetch"
                ? QStringList{"       /\\       ",  "      /  \\      ", "     / /\\ \\     ",
                              "    / /__\\ \\    ", "   /  ____  \\   ", "  /__/    \\__\\  ",
                              "     AURORA     "}
                : QStringList{"      /\\       ",  "     /++\\      ",  "    /+  +\\     ",
                              "   /+ /\\ +\\    ", "  /+ /__\\ +\\   ", " /++++++++++\\  ",
                              "/__AURORA_86__\\ "};
        QStringList rows;
        for (int i = 0; i < info.size(); ++i)
            rows << (args.isEmpty() ? (i < logo.size() ? logo[i] : QString()).leftJustified(18, ' ')
                                    : QString()) +
                        info[i];
        return rows.join('\n');
    }
    if (command == "uptime") {
        if (!noArguments())
            return usage("uptime");
        return QString("up %1, %2 processes, %3 KiB memory in use")
            .arg(duration(system_->uptimeSeconds()))
            .arg(system_->processes().size())
            .arg(system_->usedMemoryKiB());
    }
    if (command == "htop") {
        if (!noArguments())
            return usage("htop");
        QString out = QString("htop / A16  RAM %1/%2 KiB  DISK %3/%4 KiB\nPID  MEMORY  PROCESS\n")
                          .arg(system_->usedMemoryKiB())
                          .arg(SimSystem::MemoryKiB)
                          .arg(system_->usedBytes() / 1024)
                          .arg(SimSystem::DiskBytes / 1024);
        for (const auto &p : system_->processes())
            out += QString("%1 %2 KiB  %3\n").arg(p.pid, 4).arg(p.memoryKiB, 5).arg(p.name);
        return out;
    }
    if (command == "nano") {
        if (args.size() > 1)
            return usage("nano [FILE]");
        return "nano: open Writer for " + path(args.isEmpty() ? "docs/untitled.txt" : args.first());
    }
    if (command == "less") {
        if (args.size() != 1)
            return usage("less FILE");
        QByteArray data;
        return read(args.first(), data) ? QString::fromUtf8(data) : error(message);
    }
    if (command == "fortune") {
        if (!args.isEmpty() && args != QStringList{"-s"})
            return usage("fortune [-s]");
        return fortune();
    }
    if (command == "cowsay") {
        QString text = args.isEmpty() ? fortune() : args.join(' ');
        if (text.size() > 512)
            return error("cowsay messages are limited to 512 characters.");
        QStringList lines;
        while (!text.isEmpty()) {
            int end = qMin(40, int(text.size()));
            if (end < text.size()) {
                int space = text.lastIndexOf(' ', end);
                if (space > 0)
                    end = space;
            }
            lines << text.left(end);
            text = text.mid(end).trimmed();
        }
        if (lines.isEmpty())
            lines << "";
        int width = 0;
        for (const auto &line : lines)
            width = qMax(width, int(line.size()));
        QString out = " " + QString(width + 2, '_') + '\n';
        for (int i = 0; i < lines.size(); ++i)
            out += (lines.size() == 1       ? "< "
                    : i == 0                ? "/ "
                    : i == lines.size() - 1 ? "\\ "
                                            : "| ") +
                   lines[i].leftJustified(width, ' ') +
                   (lines.size() == 1       ? " >"
                    : i == 0                ? " \\"
                    : i == lines.size() - 1 ? " /"
                                            : " |") +
                   '\n';
        out += " " + QString(width + 2, '-') + R"COW(
        \   ^__^
         \  (oo)\_______
            (__)\       )\/\
                ||----w |
                ||     ||)COW";
        return out;
    }
    if (command == "cal") {
        const auto today = clockTime(system_).date();
        int month = today.month(), year = today.year();
        bool ok = true;
        if (args.size() == 1) {
            year = args[0].toInt(&ok);
            if (!ok || year < 1 || year > 9999)
                return usage("cal [MONTH YEAR] or cal YEAR");
            QString out;
            for (int m = 1; m <= 12; ++m)
                out += calendar(m, year) + '\n';
            return out.trimmed();
        }
        if (args.size() == 2) {
            bool monthOk;
            month = args[0].toInt(&monthOk);
            year = args[1].toInt(&ok);
            ok = ok && monthOk;
        } else if (!args.isEmpty())
            return usage("cal [MONTH YEAR]");
        if (!ok || !QDate(year, month, 1).isValid())
            return error("Invalid calendar month or year.");
        return calendar(month, year).trimmed();
    }
    if (command == "tree" || command == "find" || command == "du") {
        QString root = cwd, pattern;
        QString type;
        int maxDepth = command == "du" ? 1024 : 32;
        bool all = false;
        QStringList positional;
        for (int i = 0; i < args.size(); ++i) {
            const QString a = args[i];
            if (a == "-a" && (command == "tree" || command == "du")) {
                all = true;
                continue;
            }
            if ((a == "-L" && command == "tree") || (a == "-maxdepth" && command == "find")) {
                if (++i >= args.size())
                    return usage(command + " [PATH] [depth option COUNT]");
                bool ok;
                maxDepth = args[i].toInt(&ok);
                if (!ok || maxDepth < 0 || maxDepth > 32)
                    return error("Depth must be between 0 and 32.");
                continue;
            }
            if (a == "-name" && command == "find") {
                if (++i >= args.size())
                    return usage("find [PATH] [-name WILDCARD]");
                pattern = args[i];
                continue;
            }
            if (a == "-type" && command == "find") {
                if (++i >= args.size() || (args[i] != "f" && args[i] != "d"))
                    return usage("find [PATH] [-type f|d]");
                type = args[i];
                continue;
            }
            if (a.startsWith('-'))
                return error("Unknown option: " + a);
            positional << a;
        }
        if (positional.size() > 1)
            return usage(command + " [PATH]");
        if (!positional.isEmpty())
            root = path(positional[0]);
        if (!system_->exists(root))
            return error(system_->lastError().isEmpty() ? "Path not found: " + root : system_->lastError());
        QStringList rows;
        int files = 0, dirs = 0, visited = 0;
        const QRegularExpression match(
            pattern.isEmpty() ? ".*" : QRegularExpression::wildcardToRegularExpression(pattern));
        if (command == "tree")
            rows << root;
        std::function<qint64(const QString &, int, const QString &)> walk =
            [&](const QString &current, int depth, const QString &indent) -> qint64 {
            if (++visited > 8192)
                return 0;
            bool dir = system_->isDirectory(current);
            qint64 bytes = current.toUtf8().size() + 64;
            if (!dir) {
                QByteArray data;
                if (!read(current, data)) {
                    rows << error(message);
                    return 0;
                }
                bytes += data.size();
            }
            const QString name = current.section('/', -1).isEmpty() ? "/" : current.section('/', -1);
            if (command == "find" && (type.isEmpty() || (type == "d") == dir) && match.match(name).hasMatch())
                rows << current;
            if (dir && depth < maxDepth) {
                const auto entries = system_->list(current);
                const QString listError = system_->lastError();
                if (!listError.isEmpty())
                    rows << error(listError);
                QList<VEntry> visible;
                for (const auto &entry : entries)
                    if (command != "tree" || all || !entry.path.section('/', -1).startsWith('.'))
                        visible << entry;
                for (int i = 0; i < visible.size(); ++i) {
                    const auto &entry = visible[i];
                    if (entry.directory)
                        ++dirs;
                    else
                        ++files;
                    if (command == "tree")
                        rows << indent + (i == visible.size() - 1 ? "`-- " : "|-- ") +
                                    entry.path.section('/', -1) + (entry.directory ? "/" : "");
                    bytes +=
                        walk(entry.path, depth + 1, indent + (i == visible.size() - 1 ? "    " : "|   "));
                }
            }
            if (command == "du" && (dir || all || depth == 0))
                rows << QString("%1\t%2").arg((bytes + 1023) / 1024).arg(current);
            return bytes;
        };
        walk(root, 0, {});
        if (visited > 8192)
            rows << "[output limited to 8192 entries]";
        if (command == "tree")
            rows << QString("\n%1 directories, %2 files").arg(dirs).arg(files);
        return rows.join('\n');
    }
    if (command == "grep") {
        bool insensitive = false, numbers = false, invert = false, count = false;
        int i = 0;
        for (; i < args.size() && args[i].startsWith('-'); ++i) {
            if (args[i] == "--") {
                ++i;
                break;
            }
            for (QChar flag : args[i].mid(1)) {
                if (flag == 'i')
                    insensitive = true;
                else if (flag == 'n')
                    numbers = true;
                else if (flag == 'v')
                    invert = true;
                else if (flag == 'c')
                    count = true;
                else
                    return error("Unknown grep option: " + QString(flag));
            }
        }
        if (args.size() - i < 2 && !(piped && args.size() - i == 1))
            return usage("grep [-invc] PATTERN FILE...");
        QRegularExpression regex(args[i++], insensitive ? QRegularExpression::CaseInsensitiveOption
                                                        : QRegularExpression::NoPatternOption);
        if (!regex.isValid())
            return error("Invalid regular expression: " + regex.errorString());
        QVector<QPair<QString, QByteArray>> sources;
        for (; i < args.size(); ++i) {
            QByteArray data;
            if (!read(args[i], data))
                return error(message);
            sources.append({args[i], data});
        }
        if (sources.isEmpty())
            sources.append({QString(), standardInput.toUtf8()});
        const bool multiple = sources.size() > 1;
        QStringList out;
        for (const auto &source : sources) {
            int matches = 0, lineNo = 0;
            for (const auto &line : fileLines(source.second)) {
                ++lineNo;
                const QString text = QString::fromUtf8(line);
                if (regex.match(text).hasMatch() == invert)
                    continue;
                ++matches;
                if (!count)
                    out << (multiple ? source.first + ":" : "") +
                               (numbers ? QString::number(lineNo) + ":" : "") + text;
            }
            if (count)
                out << (multiple ? source.first + ":" : "") + QString::number(matches);
        }
        return out.join('\n');
    }
    if (command == "wc") {
        QString selected;
        QStringList names;
        for (const auto &a : args) {
            if (a.startsWith('-')) {
                for (QChar flag : a.mid(1)) {
                    if (!QString("lwmc").contains(flag))
                        return error("Unknown wc option: " + QString(flag));
                    if (!selected.contains(flag))
                        selected += flag;
                }
            } else
                names << a;
        }
        if (names.isEmpty()) {
            if (piped)
                names << QString();
            else
                return usage("wc [-lwmc] FILE...");
        }
        if (selected.isEmpty())
            selected = "lwc";
        QStringList out;
        qint64 totalLines = 0, totalWords = 0, totalCharacters = 0, totalBytes = 0;
        auto format = [&](qint64 lines, qint64 words, qint64 characters, qint64 bytes, const QString &name) {
            QStringList values;
            for (QChar flag : selected)
                values << QString::number(flag == 'l'   ? lines
                                          : flag == 'w' ? words
                                          : flag == 'm' ? characters
                                                        : bytes);
            return values.join(" ") + " " + name;
        };
        for (const auto &name : names) {
            QByteArray data;
            if (name.isEmpty())
                data = standardInput.toUtf8();
            else if (!read(name, data))
                return error(message);
            const QString text = QString::fromUtf8(data);
            qint64 words = 0;
            auto matches = QRegularExpression("\\S+").globalMatch(text);
            while (matches.hasNext()) {
                matches.next();
                ++words;
            }
            const qint64 lines = data.count('\n'), characters = text.toUcs4().size(), bytes = data.size();
            const QString row = format(lines, words, characters, bytes, name);
            out << (name.isEmpty() ? row.trimmed() : row);
            totalLines += lines;
            totalWords += words;
            totalCharacters += characters;
            totalBytes += bytes;
        }
        if (names.size() > 1)
            out << format(totalLines, totalWords, totalCharacters, totalBytes, "total");
        return out.join('\n');
    }
    if (command == "head" || command == "tail") {
        int count = 10;
        if (args.size() >= 2 && args[0] == "-n") {
            bool ok;
            count = args[1].toInt(&ok);
            if (!ok || count < 0)
                return error("Line count must be a non-negative integer.");
            args.removeFirst();
            args.removeFirst();
        }
        if (args.size() != 1 && !(args.isEmpty() && piped))
            return usage(command + " [-n COUNT] FILE");
        QByteArray data;
        if (args.isEmpty())
            data = standardInput.toUtf8();
        else if (!read(args[0], data))
            return error(message);
        const auto lines = fileLines(data);
        const int start = command == "head" ? 0 : qMax(0, int(lines.size()) - count);
        const int end = qMin(int(lines.size()), start + count);
        QByteArray out;
        for (int i = start; i < end; ++i) {
            out += lines[i];
            if (i < int(lines.size()) - 1 || data.endsWith('\n'))
                out += '\n';
        }
        return QString::fromUtf8(out);
    }
    if (command == "sort" || command == "uniq") {
        bool numeric = false, reverse = false, unique = false, count = false;
        QStringList names;
        for (const auto &a : args) {
            if (a.startsWith('-'))
                for (QChar flag : a.mid(1)) {
                    if (command == "sort" && flag == 'n')
                        numeric = true;
                    else if (command == "sort" && flag == 'r')
                        reverse = true;
                    else if (command == "sort" && flag == 'u')
                        unique = true;
                    else if (command == "uniq" && flag == 'c')
                        count = true;
                    else
                        return error("Unknown " + command + " option: " + QString(flag));
                }
            else
                names << a;
        }
        if (names.size() != 1 && !(names.isEmpty() && piped))
            return usage(command + " [OPTIONS] FILE");
        QByteArray data;
        if (names.isEmpty())
            data = standardInput.toUtf8();
        else if (!read(names[0], data))
            return error(message);
        auto lines = fileLines(data);
        if (command == "sort") {
            std::stable_sort(lines.begin(), lines.end(), [&](const QByteArray &a, const QByteArray &b) {
                if (numeric) {
                    double av = a.trimmed().toDouble(), bv = b.trimmed().toDouble();
                    if (!std::isfinite(av))
                        av = 0;
                    if (!std::isfinite(bv))
                        bv = 0;
                    return reverse ? av > bv : av < bv;
                }
                return reverse ? a > b : a < b;
            });
            if (unique)
                lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            return QString::fromUtf8(lines.join('\n')) + (lines.isEmpty() ? QString() : "\n");
        }
        QStringList out;
        for (int i = 0; i < lines.size();) {
            int end = i + 1;
            while (end < lines.size() && lines[end] == lines[i])
                ++end;
            out << (count ? QString::number(end - i) + " " : "") + QString::fromUtf8(lines[i]);
            i = end;
        }
        return out.join('\n') + (out.isEmpty() ? QString() : "\n");
    }
    if (command == "base64") {
        bool decode = false;
        if (!args.isEmpty() && (args[0] == "-d" || args[0] == "--decode")) {
            decode = true;
            args.removeFirst();
        }
        if ((args.isEmpty() && !piped) || args.size() > 2)
            return usage("base64 [-d] FILE [OUTPUT]");
        QByteArray data;
        if (args.isEmpty())
            data = standardInput.toUtf8();
        else if (!read(args[0], data))
            return error(message);
        QByteArray output;
        if (decode) {
            const auto decoded = QByteArray::fromBase64Encoding(data.simplified().replace(" ", ""),
                                                                QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded)
                return error("Invalid base64 input.");
            output = decoded.decoded;
        } else {
            const auto encoded = data.toBase64();
            for (int i = 0; i < encoded.size(); i += 76)
                output += encoded.mid(i, 76) + '\n';
        }
        if (args.size() == 2)
            return system_->write(path(args[1]), output) ? "base64: wrote " + path(args[1])
                                                         : error(system_->lastError());
        return QString::fromUtf8(output);
    }
    if (command == "xxd" || command == "hexdump") {
        int limit = 4096;
        QStringList names;
        for (int i = 0; i < args.size(); ++i) {
            if (args[i] == "-C" && command == "hexdump")
                continue;
            if (args[i] == "-l" || args[i] == "-n") {
                if (++i >= args.size())
                    return usage(command + " [-l BYTES] FILE");
                bool ok;
                limit = args[i].toInt(&ok);
                if (!ok || limit < 0 || limit > 65536)
                    return error("Byte limit must be between 0 and 65536.");
            } else if (args[i].startsWith('-'))
                return error("Unknown option: " + args[i]);
            else
                names << args[i];
        }
        if (names.size() != 1 && !(names.isEmpty() && piped))
            return usage(command + " FILE");
        QByteArray data;
        if (names.isEmpty())
            data = standardInput.toUtf8();
        else if (!read(names[0], data))
            return error(message);
        data = data.left(limit);
        QStringList out;
        for (int i = 0; i < data.size(); i += 16) {
            const auto chunk = data.mid(i, 16);
            QString hex, ascii;
            for (unsigned char byte : chunk) {
                hex += QString("%1 ").arg(byte, 2, 16, QChar('0'));
                ascii += (byte >= 32 && byte < 127) ? QChar(byte) : QChar('.');
            }
            out << QString("%1  %2 |%3|").arg(i, 8, 16, QChar('0')).arg(hex.leftJustified(48, ' '), ascii);
        }
        return out.join('\n');
    }
    if (command == "figlet") {
        if (args.isEmpty())
            return usage("figlet WORDS...");
        const QString text = args.join(' ').toUpper();
        if (text.size() > 32)
            return error("figlet accepts at most 32 characters.");
        static const QMap<QChar, QString> glyphs = {{'A', "01110/10001/10001/11111/10001/10001/10001"},
                                                    {'B', "11110/10001/10001/11110/10001/10001/11110"},
                                                    {'C', "01111/10000/10000/10000/10000/10000/01111"},
                                                    {'D', "11110/10001/10001/10001/10001/10001/11110"},
                                                    {'E', "11111/10000/10000/11110/10000/10000/11111"},
                                                    {'F', "11111/10000/10000/11110/10000/10000/10000"},
                                                    {'G', "01111/10000/10000/10111/10001/10001/01111"},
                                                    {'H', "10001/10001/10001/11111/10001/10001/10001"},
                                                    {'I', "11111/00100/00100/00100/00100/00100/11111"},
                                                    {'J', "00111/00010/00010/00010/10010/10010/01100"},
                                                    {'K', "10001/10010/10100/11000/10100/10010/10001"},
                                                    {'L', "10000/10000/10000/10000/10000/10000/11111"},
                                                    {'M', "10001/11011/10101/10101/10001/10001/10001"},
                                                    {'N', "10001/11001/10101/10011/10001/10001/10001"},
                                                    {'O', "01110/10001/10001/10001/10001/10001/01110"},
                                                    {'P', "11110/10001/10001/11110/10000/10000/10000"},
                                                    {'Q', "01110/10001/10001/10001/10101/10010/01101"},
                                                    {'R', "11110/10001/10001/11110/10100/10010/10001"},
                                                    {'S', "01111/10000/10000/01110/00001/00001/11110"},
                                                    {'T', "11111/00100/00100/00100/00100/00100/00100"},
                                                    {'U', "10001/10001/10001/10001/10001/10001/01110"},
                                                    {'V', "10001/10001/10001/10001/10001/01010/00100"},
                                                    {'W', "10001/10001/10001/10101/10101/10101/01010"},
                                                    {'X', "10001/10001/01010/00100/01010/10001/10001"},
                                                    {'Y', "10001/10001/01010/00100/00100/00100/00100"},
                                                    {'Z', "11111/00001/00010/00100/01000/10000/11111"},
                                                    {'0', "01110/10001/10011/10101/11001/10001/01110"},
                                                    {'1', "00100/01100/00100/00100/00100/00100/01110"},
                                                    {'2', "01110/10001/00001/00010/00100/01000/11111"},
                                                    {'3', "11110/00001/00001/01110/00001/00001/11110"},
                                                    {'4', "00010/00110/01010/10010/11111/00010/00010"},
                                                    {'5', "11111/10000/10000/11110/00001/00001/11110"},
                                                    {'6', "01110/10000/10000/11110/10001/10001/01110"},
                                                    {'7', "11111/00001/00010/00100/01000/01000/01000"},
                                                    {'8', "01110/10001/10001/01110/10001/10001/01110"},
                                                    {'9', "01110/10001/10001/01111/00001/00001/01110"},
                                                    {' ', "00000/00000/00000/00000/00000/00000/00000"},
                                                    {'?', "01110/10001/00001/00010/00100/00000/00100"},
                                                    {'!', "00100/00100/00100/00100/00100/00000/00100"},
                                                    {'-', "00000/00000/00000/11111/00000/00000/00000"},
                                                    {'.', "00000/00000/00000/00000/00000/00100/00100"}};
        QStringList output;
        for (int row = 0; row < 7; ++row) {
            QString line;
            for (QChar ch : text) {
                QString bits = glyphs.value(ch, glyphs['?']).section('/', row, row);
                for (QChar bit : bits)
                    line += bit == '1' ? '#' : ' ';
                line += ' ';
            }
            output << line;
        }
        return output.join('\n');
    }
    return error("No handler for installed command: " + command);
}
