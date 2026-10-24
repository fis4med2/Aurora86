#include "shell.h"
#include "archive.h"
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

const QString ClsMarker = QStringLiteral("\x01" "CLS");

// Splits a command line on ';' and '&&' while respecting quotes.
QList<QPair<QString, QString>> splitSequence(const QString &line)
{
    QList<QPair<QString, QString>> segments;
    QString current;
    QChar quote;
    bool escaped = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            current += ch;
            escaped = false;
            continue;
        }
        if (!quote.isNull()) {
            current += ch;
            if (ch == quote)
                quote = {};
            continue;
        }
        if (ch == '\\') {
            current += ch;
            escaped = true;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            current += ch;
            continue;
        }
        if (ch == ';') {
            segments.append({current, ";"});
            current.clear();
            continue;
        }
        if (ch == '&' && i + 1 < line.size() && line[i + 1] == '&') {
            segments.append({current, "&&"});
            current.clear();
            ++i;
            continue;
        }
        current += ch;
    }
    segments.append({current, QString()});
    return segments;
}

QStringList splitPipeline(const QString &line)
{
    QStringList stages;
    QString current;
    QChar quote;
    bool escaped = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line[i];
        if (escaped) {
            current += ch;
            escaped = false;
            continue;
        }
        if (!quote.isNull()) {
            current += ch;
            if (ch == quote)
                quote = {};
            continue;
        }
        if (ch == '\\') {
            current += ch;
            escaped = true;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            quote = ch;
            current += ch;
            continue;
        }
        if (ch == '|') {
            stages << current;
            current.clear();
            continue;
        }
        current += ch;
    }
    stages << current;
    return stages;
}

QString wildcardPattern(const QString &wildcard)
{
    QString pattern = "^";
    for (qsizetype i = 0; i < wildcard.size(); ++i) {
        const QChar ch = wildcard[i];
        if (ch == '*')
            pattern += ".*";
        else if (ch == '?')
            pattern += ".";
        else if (ch == '[') {
            const qsizetype close = wildcard.indexOf(']', i + 1);
            if (close > i + 1) {
                pattern += wildcard.mid(i, close - i + 1);
                i = close;
            } else {
                pattern += "\\[";
            }
        } else
            pattern += QRegularExpression::escape(QString(ch));
    }
    pattern += "$";
    return pattern;
}

QStringList builtinCommands()
{
    return {"help",     "ls",     "cd",      "pwd",    "cat",      "echo",   "mkdir",  "touch",
            "rm",       "mv",     "cp",      "chmod",  "stat",     "df",     "free",   "ps",
            "kill",     "date",   "uname",   "whoami", "clear",    "exit",   "shutdown", "lock",
            "edit",     "files",  "calc",    "browser", "games",   "packages", "prism", "garden",
            "eco",      "snake",  "blocks",  "hellgate", "about",  "aurora",   "lua",    "mods",   "basic",
            "history",  "alias",  "unalias", "export", "unset",    "env",    "which",  "type",
            "true",     "false",  "lpr",     "lpq",    "lprm",     "pack",   "unpack", "arc",
            "screenshot"};
}

bool ensureDirectory(SimSystem *system, const QString &path)
{
    if (system->isDirectory(path))
        return true;
    const QString parent = path.section('/', 0, -2);
    if (!parent.isEmpty() && parent != "/" && !system->isDirectory(parent) && !ensureDirectory(system, parent))
        return false;
    return system->mkdir(path);
}
} // namespace

Shell::Shell(SimSystem *system, QObject *parent) : QObject(parent), m_system(system) { configureBasic(); }

void Shell::configureBasic()
{
    m_basic.setSoundHandler([this](const QString &event) { emit soundRequested(event); });
    m_basic.setFileReader([this](const QString &name) {
        return m_system->read(m_system->normalize(name, m_cwd));
    });
    m_basic.setFileWriter([this](const QString &name, const QByteArray &data) {
        return m_system->write(m_system->normalize(name, m_cwd), data);
    });
}

QString Shell::cwd() const { return m_cwd; }
QString Shell::prompt() const {
    if (m_inBasic)
        return "basic> ";
    const QString user = m_system->setting("username", "guest").toString();
    return (user.isEmpty() ? QString("guest") : user) +
           "@aurora:" + (m_cwd.startsWith("/home/guest") ? "~" + m_cwd.mid(11) : m_cwd) + "$ ";
}

QMap<QString, QString> Shell::aliases() const
{
    QMap<QString, QString> result;
    const auto stored = m_system->setting("shellAliases").toMap();
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it)
        result.insert(it.key(), it.value().toString());
    return result;
}

void Shell::storeAliases(const QMap<QString, QString> &map)
{
    QVariantMap stored;
    for (auto it = map.constBegin(); it != map.constEnd(); ++it)
        stored.insert(it.key(), it.value());
    m_system->setSetting("shellAliases", stored);
}

QString Shell::applyAlias(const QString &line) const
{
    const QString text = line.trimmed();
    if (text.isEmpty())
        return text;
    qsizetype split = 0;
    while (split < text.size() && !text[split].isSpace())
        ++split;
    const QString first = text.left(split);
    const auto map = aliases();
    if (!map.contains(first))
        return text;
    return map.value(first) + text.mid(split);
}

QString Shell::envValue(const QString &name) const
{
    if (name.isEmpty())
        return {};
    if (name == "?")
        return QString::number(m_status);
    const QString user = m_system->setting("username", "guest").toString();
    const QMap<QString, QString> defaults = {
        {"HOME", "/home/guest"},  {"USER", user.isEmpty() ? QString("guest") : user},
        {"LOGNAME", user.isEmpty() ? QString("guest") : user}, {"PWD", m_cwd},
        {"SHELL", "/bin/aster"},  {"PATH", "/bin"},  {"TERM", "a16"},
        {"HOSTNAME", "aurora"},   {"EDITOR", "edit"}};
    if (defaults.contains(name))
        return defaults.value(name);
    const auto stored = m_system->setting("shellEnv").toMap();
    return stored.value(name).toString();
}

QString Shell::expandVariables(const QString &text) const
{
    QString result;
    QChar quote;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (!quote.isNull()) {
            if (quote == '"' && ch == '$' && i + 1 < text.size()) {
                const qsizetype start = i;
                ++i;
                if (i < text.size() && text[i] == '?') {
                    result += envValue("?");
                    continue;
                }
                qsizetype end = i;
                if (end < text.size() && text[end] == '{') {
                    const qsizetype close = text.indexOf('}', end + 1);
                    if (close > end) {
                        result += envValue(text.mid(end + 1, close - end - 1));
                        i = close;
                        continue;
                    }
                }
                while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == '_'))
                    ++end;
                if (end > i)
                    result += envValue(text.mid(i, end - i));
                else
                    result += '$';
                i = end - 1;
                Q_UNUSED(start);
                continue;
            }
            result += ch;
            if (ch == quote)
                quote = {};
            continue;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
            result += ch;
            continue;
        }
        if (ch == '$' && i + 1 < text.size()) {
            if (text[i + 1] == '?') {
                result += envValue("?");
                ++i;
                continue;
            }
            if (text[i + 1] == '{') {
                const qsizetype close = text.indexOf('}', i + 2);
                if (close > i) {
                    result += envValue(text.mid(i + 2, close - i - 2));
                    i = close;
                    continue;
                }
            } else if (text[i + 1].isLetter() || text[i + 1] == '_' || text[i + 1] == '?') {
                qsizetype end = i + 1;
                while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == '_'))
                    ++end;
                result += envValue(text.mid(i + 1, end - i - 1));
                i = end - 1;
                continue;
            }
        }
        result += ch;
    }
    return result;
}

QStringList Shell::expandGlob(const QString &token) const
{
    if (!token.contains('*') && !token.contains('?') && !token.contains('['))
        return {token};
    QString directory = ".", pattern = token, prefix;
    const qsizetype slash = token.lastIndexOf('/');
    if (slash >= 0) {
        prefix = token.left(slash + 1);
        directory = slash == 0 ? QString("/") : token.left(slash);
        pattern = token.mid(slash + 1);
    }
    if (pattern.isEmpty())
        return {token};
    const QString path = m_system->normalize(directory, m_cwd);
    const auto entries = m_system->list(path);
    if (!m_system->lastError().isEmpty())
        return {token};
    const QRegularExpression regex(wildcardPattern(pattern));
    if (!regex.isValid())
        return {token};
    QStringList matches;
    for (const auto &entry : entries) {
        const QString name = entry.path.section('/', -1);
        if (regex.match(name).hasMatch())
            matches << prefix + name;
    }
    if (matches.isEmpty())
        return {token};
    matches.sort();
    return matches;
}

QString Shell::executeBasic(const QString &line)
{
    configureBasic();
    QString output = m_basic.submit(line);
    if (output.contains(ClsMarker)) {
        output.remove(ClsMarker);
        emit clearRequested();
    }
    if (m_basic.exited()) {
        m_inBasic = false;
        m_basic.clearExit();
        if (output.trimmed().isEmpty())
            output = "Back to ASTER.";
    }
    m_status = output.startsWith('?') ? 1 : 0;
    return output;
}

QString Shell::execute(const QString &line)
{
    if (m_inBasic)
        return executeBasic(line);
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty())
        return {};
    m_history.append(trimmed);
    if (m_history.size() > 500)
        m_history.removeFirst();

    QStringList outputs;
    QString last;
    const auto segments = splitSequence(trimmed);
    bool single = segments.size() == 1;
    for (const auto &segment : segments) {
        const QString expanded = applyAlias(expandVariables(segment.first));
        if (expanded.trimmed().isEmpty()) {
            m_status = 0;
            last.clear();
            continue;
        }
        QStringList stages = splitPipeline(expanded);
        QString input;
        bool piped = false;
        for (int stageIndex = 0; stageIndex < stages.size(); ++stageIndex) {
            const QString &stage = stages[stageIndex];
            if (stage.trimmed().isEmpty())
                continue;
            m_pipedOutput = stageIndex + 1 < stages.size();
            input = runSimple(stage, piped ? input : QString());
            m_pipedOutput = false;
            piped = true;
            if (input.startsWith("error:"))
                break;
        }
        last = input;
        m_status = m_commandStatus >= 0
                       ? m_commandStatus
                       : (last.startsWith("error:") || last.startsWith("usage:") ? 1 : 0);
        m_commandStatus = -1;
        if (!single && !last.trimmed().isEmpty())
            outputs << last;
        if (segment.second == "&&" && m_status != 0)
            break;
    }
    if (single)
        return last;
    return outputs.join('\n');
}

QString Shell::runSimple(const QString &line, const QString &standardInput) {
    m_commandStatus = -1;
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
    // Wildcards expand against the virtual disk. Redirection keeps the literal
    // arguments so that "echo x > file" stays unambiguous.
    if (operators.isEmpty()) {
        QStringList expanded;
        for (const auto &arg : args)
            expanded << expandGlob(arg);
        args = expanded;
    }
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
        return "ASTER shell / AURORA 1.5\n"
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
               "hellgate              corridor 13 + the core: 3D maze with drones\n"
               "aurora -i PACKAGE...   install optional packages\n"
               "aurora --list         browse repository\n"
               "lua FILE.lua          run a Lua program\n"
               "mods                  Lua Studio / local mods\n"
               "basic [FILE.BAS]      start the A16 BASIC interpreter\n"
               "lpr FILE   lpq   lprm all    print to the A16 printer\n"
               "pack ARC FILES...   unpack ARC [DIR]   arc -l ARC\n"
               "export NAME=VALUE   env   alias NAME=VALUE   unalias\n"
               "history [-c] [N]    which COMMAND   screenshot\n"
               "cmd1 | cmd2    cmd1 ; cmd2    cmd1 && cmd2    *.txt\n"
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
            return manager.run(command, args, m_cwd, standardInput);
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
        return manager.run(command, args, m_cwd, standardInput);
    }
    if (command == "true") {
        m_commandStatus = 0;
        return {};
    }
    if (command == "false") {
        m_commandStatus = 1;
        return {};
    }
    if (command == "history") {
        if (args == QStringList{"-c"}) {
            m_history.clear();
            return {};
        }
        int limit = m_history.size();
        if (args.size() == 1) {
            bool ok = false;
            const int count = args.first().toInt(&ok);
            if (!ok || count < 0)
                return usage("history [-c] [COUNT]");
            limit = qMin(limit, count);
        } else if (args.size() > 1) {
            return usage("history [-c] [COUNT]");
        }
        QStringList rows;
        const int start = m_history.size() - limit;
        for (int i = 0; i < limit; ++i)
            rows << QString("%1  %2").arg(start + i + 1, 4).arg(m_history.value(start + i));
        return rows.join('\n');
    }
    if (command == "env" || command == "export" || command == "unset") {
        const auto environment = [this] {
            QMap<QString, QString> values;
            for (const char *name :
                 {"HOME", "USER", "PWD", "SHELL", "PATH", "TERM", "HOSTNAME", "EDITOR"})
                values.insert(QString::fromLatin1(name), envValue(QString::fromLatin1(name)));
            const auto stored = m_system->setting("shellEnv").toMap();
            for (auto it = stored.constBegin(); it != stored.constEnd(); ++it)
                values.insert(it.key(), it.value().toString());
            return values;
        };
        const auto listing = [environment] {
            QStringList rows;
            const auto values = environment();
            for (auto it = values.constBegin(); it != values.constEnd(); ++it)
                rows << it.key() + "=" + it.value();
            return rows.join('\n');
        };
        if (command == "env") {
            if (!args.isEmpty())
                return usage("env");
            return listing();
        }
        if (args.isEmpty())
            return listing();
        QVariantMap stored = m_system->setting("shellEnv").toMap();
        QStringList rejected;
        for (const QString &item : args) {
            const int equals = item.indexOf('=');
            const QString name = equals < 0 ? item : item.left(equals);
            if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(name).hasMatch()) {
                rejected << item;
                continue;
            }
            if (command == "unset")
                stored.remove(name);
            else if (equals > 0)
                stored.insert(name, item.mid(equals + 1));
        }
        if (!rejected.isEmpty())
            return "error: invalid variable name: " + rejected.join(' ');
        m_system->setSetting("shellEnv", stored);
        if (m_system->setting("shellEnv").toMap() != stored)
            return "error: could not save the environment.";
        return {};
    }
    if (command == "alias" || command == "unalias") {
        auto map = aliases();
        if (command == "unalias") {
            if (args.isEmpty())
                return usage("unalias NAME...");
            for (const QString &name : args) {
                if (name == "-a")
                    map.clear();
                else
                    map.remove(name);
            }
            storeAliases(map);
            return {};
        }
        if (args.isEmpty()) {
            QStringList rows;
            for (auto it = map.constBegin(); it != map.constEnd(); ++it)
                rows << QString("alias %1='%2'").arg(it.key(), it.value());
            return rows.isEmpty() ? QString("No aliases defined.") : rows.join('\n');
        }
        QStringList missing;
        for (const QString &item : args) {
            const int equals = item.indexOf('=');
            if (equals <= 0) {
                if (map.contains(item))
                    return QString("alias %1='%2'").arg(item, map.value(item));
                missing << item;
                continue;
            }
            const QString name = item.left(equals);
            if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_-]*$").match(name).hasMatch()) {
                missing << item;
                continue;
            }
            map.insert(name, item.mid(equals + 1));
        }
        if (!missing.isEmpty())
            return "error: unknown alias: " + missing.join(' ');
        storeAliases(map);
        return {};
    }
    if (command == "which" || command == "type") {
        if (args.isEmpty())
            return usage(command + " COMMAND...");
        QStringList rows;
        for (const QString &name : args) {
            if (isModInstalled(m_system, name))
                rows << name + ": /home/guest/.mods/" + name + ".lua";
            else if (builtinCommands().contains(name))
                rows << name + ": shell built-in";
            else if (manager.ownsCommand(name))
                rows << name + (manager.isInstalled(name) ? ": /bin/" + name
                                                          : ": not installed (aurora -i " + name + ")");
            else
                rows << "error: " + name + ": not found";
        }
        return rows.join('\n');
    }
    if (command == "screenshot") {
        if (!args.isEmpty())
            return usage("screenshot");
        emit launchRequested("capture", {});
        return {};
    }
    if (command == "basic") {
        if (args.size() > 1)
            return usage("basic [program.bas]");
        m_inBasic = true;
        configureBasic();
        QString banner = "A16 BASIC v1.0  /  4096 KiB free\n"
                         "Type a line number to store a program, RUN to start, LIST to review.\n"
                         "SYSTEM returns to ASTER.";
        if (!args.isEmpty()) {
            const QString target = path(args.first());
            const QByteArray data = m_system->read(target);
            if (!m_system->lastError().isEmpty()) {
                m_inBasic = false;
                return failure();
            }
            QString problem;
            if (!m_basic.load(data, &problem)) {
                m_inBasic = false;
                return "error: " + problem;
            }
            banner += "\nLoaded " + target + " (" + QString::number(m_basic.lineCount()) +
                      " lines). Type RUN to start.";
        }
        return banner;
    }
    if (command == "lpr" || command == "lpq" || command == "lprm") {
        const QString spool = "/tmp/print";
        const auto entries = [&] { return m_system->list(spool); };
        if (command == "lpr") {
            if (args.size() != 1)
                return usage("lpr FILE");
            const QString target = path(args.first());
            const QByteArray data = m_system->read(target);
            if (!m_system->lastError().isEmpty())
                return failure();
            if (data.size() > 64 * 1024)
                return "error: job exceeds the 64 KiB printer buffer.";
            if (!ensureDirectory(m_system, spool))
                return failure();
            int next = 1;
            for (const auto &entry : entries()) {
                bool ok = false;
                const int number = entry.path.section('/', -1).section('-', 0, 0).toInt(&ok);
                if (ok)
                    next = qMax(next, number + 1);
            }
            const QString fileName = target.section('/', -1);
            const QString job = QString("%1").arg(next, 3, 10, QChar('0')) + "-" + fileName + ".prn";
            QByteArray payload = ("A16 PRINTER / LUME 80 COLUMNS\nJob " + QString::number(next) +
                                  "  User " + envValue("USER") + "  File " + fileName +
                                  "\n----------------------------------------\n")
                                     .toUtf8();
            payload += data;
            if (!m_system->write(spool + "/" + job, payload))
                return failure();
            return QString("lpr: job %1 queued for the A16 printer (lpq to list).").arg(next);
        }
        if (command == "lpq") {
            if (!args.isEmpty())
                return usage("lpq");
            QStringList rows{ "JOB  SIZE  FILE" };
            for (const auto &entry : entries()) {
                if (entry.directory)
                    continue;
                const QString name = entry.path.section('/', -1);
                rows << QString("%1 %2  %3")
                            .arg(name.section('-', 0, 0))
                            .arg(entry.data.size(), 6)
                            .arg(name.section('-', 1).chopped(4));
            }
            return rows.size() == 1 ? QString("A16 printer queue is empty.") : rows.join('\n');
        }
        if (args.isEmpty())
            return usage("lprm JOB | all");
        const bool all = args.first() == "all";
        int removed = 0;
        for (const auto &entry : entries()) {
            if (entry.directory)
                continue;
            const QString name = entry.path.section('/', -1);
            bool ok = false;
            const int number = name.section('-', 0, 0).toInt(&ok);
            if (!all && (!ok || number != args.first().toInt()))
                continue;
            if (m_system->remove(entry.path))
                ++removed;
        }
        return removed > 0 ? QString("lprm: %1 job(s) removed.").arg(removed)
                           : QString("error: no matching print job.");
    }
    if (command == "pack" || command == "unpack" || command == "arc") {
        QStringList operands = args;
        if (command == "pack") {
            if (operands.size() < 2)
                return usage("pack ARCHIVE FILE...");
            const QString archivePath = path(operands.takeFirst());
            QList<ArchiveEntry> members;
            for (const QString &name : operands) {
                const QString target = path(name);
                if (m_system->isDirectory(target))
                    return "error: " + name + ": directories are not supported by A16ARC.";
                const QByteArray data = m_system->read(target);
                if (!m_system->lastError().isEmpty())
                    return failure();
                members.append({target.section('/', -1), data});
            }
            QString problem;
            const QByteArray archive = Archive::pack(members, &problem);
            if (archive.isEmpty())
                return "error: " + problem;
            if (!m_system->write(archivePath, archive))
                return failure();
            return QString("pack: %1 members, %2 bytes").arg(members.size()).arg(archive.size());
        }
        if (operands.isEmpty())
            return usage(command == "arc" ? "arc [-l|-x] ARCHIVE [DIRECTORY]" : "unpack ARCHIVE [DIRECTORY]");
        bool extract = command == "unpack";
        if (command == "arc") {
            const QString option = operands.first();
            if (option == "-x" || option == "--extract") {
                extract = true;
                operands.removeFirst();
            } else if (option == "-l" || option == "--list") {
                operands.removeFirst();
            }
        }
        if (operands.isEmpty() || operands.size() > 2)
            return usage("unpack ARCHIVE [DIRECTORY]");
        const QString archivePath = path(operands.first());
        const QByteArray data = m_system->read(archivePath);
        if (!m_system->lastError().isEmpty())
            return failure();
        QString problem;
        const auto members = Archive::unpack(data, &problem);
        if (members.isEmpty())
            return "error: " + (problem.isEmpty() ? QString("empty archive") : problem);
        if (!extract) {
            QStringList rows{ "LENGTH   NAME" };
            for (const auto &member : members)
                rows << QString("%1  %2").arg(member.data.size(), 7).arg(member.name);
            rows << QString("%1 member(s), %2 bytes compressed").arg(members.size()).arg(data.size());
            return rows.join('\n');
        }
        const QString directory = operands.size() == 2 ? path(operands[1]) : m_cwd;
        if (!ensureDirectory(m_system, directory))
            return failure();
        QStringList written;
        for (const auto &member : members) {
            if (!m_system->write(directory + "/" + member.name, member.data))
                return failure();
            written << member.name;
        }
        return "unpack: " + written.join(", ");
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
        return "AURORA/86 1.5  A16  8MHz  4096KiB  October1986";
    if (command == "date") {
        const auto now = QDateTime::currentDateTime();
        auto vintage = now;
        vintage.setDate(QDate(1986, now.date().month(),
                              qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
        return now.addSecs(m_system->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong())
            .toString("dd/MM/yyyy HH:mm:ss");
    }
    if (QStringList{"prisma", "jardim", "prism", "garden", "eco", "games", "packages", "hellgate"}.contains(command)) {
        emit launchRequested((command == "prisma" || command == "prism")    ? "prism"
                             : (command == "jardim" || command == "garden") ? "garden"
                             : command == "eco"                             ? "echo"
                             : command == "packages"                        ? "packages"
                             : command == "hellgate"                        ? "hellgate"
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
        return rows.join(detailed || m_pipedOutput ? "\n" : "  ");
    }
    if (command == "cat") {
        if (args.isEmpty() && !standardInput.isNull())
            return standardInput;
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
        if (args.isEmpty())
            return usage(command + " path...");
        QStringList created;
        for (const QString &name : args) {
            const auto target = path(name);
            if (command == "touch" && m_system->exists(target)) {
                created << name;
                continue;
            }
            const bool ok = command == "mkdir" ? m_system->mkdir(target) : m_system->write(target, {});
            if (!ok)
                return failure();
            created << name;
        }
        Q_UNUSED(created);
        return {};
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
    if (QStringList{"edit", "files", "calc", "browser", "snake", "blocks", "hellgate", "about"}.contains(command)) {
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
