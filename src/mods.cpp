#include "mods.h"
#include "packages.h"
#include "system.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
namespace {
constexpr int SourceLimit = 128 * 1024;
const QString Directory = "/home/guest/.mods";
bool validName(const QString &name) {
    return QRegularExpression("^[a-z][a-z0-9_-]{0,31}$").match(name).hasMatch();
}
QString path(const QString &name) { return Directory + '/' + name + ".lua"; }
bool parse(const QString &name, const QByteArray &source, AuroraMod &mod, QString &error) {
    if (!validName(name)) {
        error = "Mod names must use 1–32 lowercase letters, digits, underscores or dashes, starting with a "
                "letter.";
        return false;
    }
    if (source.isEmpty() || source.size() > SourceLimit || source.contains('\0') ||
        source.startsWith('\x1b')) {
        error = "Expected a text Lua mod between 1 byte and 128 KiB.";
        return false;
    }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(source);
    if (decoder.hasError()) {
        error = "Lua mods must use UTF-8 text.";
        return false;
    }
    mod = {name, "1.0", "Local Lua program", source};
    const QStringList lines = text.left(4096).split('\n');
    for (const QString &line : lines) {
        if (!line.trimmed().startsWith("--"))
            break;
        auto match = QRegularExpression("^\\s*--\\s*@([a-z]+)\\s+(.+?)\\s*$").match(line);
        if (!match.hasMatch())
            continue;
        const QString key = match.captured(1), value = match.captured(2);
        if (key == "name" && value != name) {
            error = "The @name header must match the Lua filename.";
            return false;
        }
        if (key == "version") {
            if (value.size() > 24 || !QRegularExpression("^[A-Za-z0-9._-]+$").match(value).hasMatch()) {
                error = "Invalid mod version header.";
                return false;
            }
            mod.version = value;
        }
        if (key == "description")
            mod.description = value.left(160);
    }
    return true;
}
QString fail(const QString &error) { return "error: " + error; }
} // namespace
QList<AuroraMod> availableMods(SimSystem *system, QString *error) {
    if (error)
        error->clear();
    QList<AuroraMod> result;
    const QString location = system->setting("modsDirectory").toString();
    if (location.isEmpty())
        return result;
    const QFileInfo root(location);
    const QString canonical = root.canonicalFilePath();
    if (!root.isDir() || canonical.isEmpty()) {
        if (error)
            *error = "The selected mods folder is unavailable.";
        return result;
    }
    const QDir directory(canonical);
    const auto entries = directory.entryInfoList({"*.lua"}, QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    if (entries.size() > 128) {
        if (error)
            *error = "The mods folder contains too many Lua files (limit 128).";
        return {};
    }
    for (const auto &entry : entries) {
        const QString relative = directory.relativeFilePath(entry.canonicalFilePath());
        if (entry.isSymLink() || relative.startsWith("../") || QDir::isAbsolutePath(relative) ||
            relative.contains('/'))
            continue;
        if (entry.size() > SourceLimit)
            continue;
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray source = file.read(SourceLimit + 1);
        AuroraMod mod;
        QString problem;
        if (parse(entry.completeBaseName(), source, mod, problem))
            result.append(mod);
    }
    return result;
}
QList<AuroraMod> installedMods(SimSystem *system, QString *error) {
    if (error)
        error->clear();
    QList<AuroraMod> result;
    if (!system->exists(Directory)) {
        if (error)
            *error = system->lastError();
        return result;
    }
    const auto entries = system->list(Directory);
    QString problem = system->lastError();
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        return {};
    }
    for (const auto &entry : entries) {
        if (entry.directory || !entry.path.endsWith(".lua"))
            continue;
        const QString name = entry.path.section('/', -1).chopped(4);
        AuroraMod mod;
        const QByteArray source = system->read(entry.path);
        problem = system->lastError();
        if (!problem.isEmpty() || !parse(name, source, mod, problem)) {
            if (error)
                *error = "Installed mod " + name + ": " + problem;
            return {};
        }
        result.append(mod);
    }
    return result;
}
QString installMod(SimSystem *system, const QString &name) {
    if (!validName(name))
        return fail("Invalid Lua mod name.");
    if (isModNameReserved(system, name))
        return fail("This mod name is reserved for an OS command or package.");
    QString error;
    const auto mods = availableMods(system, &error);
    if (!error.isEmpty())
        return fail(error);
    const AuroraMod *selected = nullptr;
    for (const auto &mod : mods)
        if (mod.name == name) {
            selected = &mod;
            break;
        }
    if (!selected)
        return fail("Unknown package or local mod: " + name + ". Put " + name +
                    ".lua in the selected mods/ folder.");
    bool created = false;
    if (!system->exists(Directory)) {
        if (!system->mkdir(Directory))
            return fail(system->lastError());
        created = true;
    }
    if (!system->write(path(name), selected->source)) {
        error = system->lastError();
        if (created)
            system->remove(Directory);
        return fail(error);
    }
    return "Installed local Lua mod " + name + " " + selected->version + ". Run: " + name;
}
QString removeMod(SimSystem *system, const QString &name) {
    if (!validName(name))
        return fail("Invalid Lua mod name.");
    if (!system->exists(path(name)))
        return fail("Lua mod is not installed: " + name);
    return system->remove(path(name)) ? "Removed local Lua mod " + name + '.' : fail(system->lastError());
}
bool isModInstalled(SimSystem *system, const QString &name) {
    if (!validName(name) || !system->exists(path(name)))
        return false;
    AuroraMod mod;
    QString error;
    const QByteArray source = system->read(path(name));
    return system->lastError().isEmpty() && parse(name, source, mod, error);
}
QByteArray installedModSource(SimSystem *system, const QString &name) {
    if (!validName(name))
        return {};
    const QByteArray source = system->read(path(name));
    if (!system->lastError().isEmpty())
        return {};
    AuroraMod mod;
    QString error;
    return parse(name, source, mod, error) ? source : QByteArray();
}

bool isModNameReserved(SimSystem *system, const QString &name) {
    PackageManager manager(system);
    if (manager.ownsCommand(name))
        return true;
    for (const auto &package : manager.catalog())
        if (package.name == name)
            return true;
    return QStringList{"help",   "aurora", "ls",       "cd",     "pwd",     "cat",   "echo",
                       "mkdir",  "touch",  "rm",       "mv",     "cp",      "chmod", "stat",
                       "df",     "free",   "ps",       "kill",   "date",    "uname", "whoami",
                       "clear",  "edit",   "files",    "calc",   "browser", "games", "packages",
                       "about",  "exit",   "shutdown", "lock",   "lua",     "mods",  "lua-studio",
                       "prisma", "jardim", "prism",    "garden", "eco",     "snake", "blocks"}
        .contains(name);
}
