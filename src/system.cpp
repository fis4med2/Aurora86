#include "system.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {
QString parentPath(const QString &path) {
    const int slash = path.lastIndexOf('/');
    return slash <= 0 ? QString("/") : path.left(slash);
}
bool under(const QString &path, const QString &directory) {
    return path == directory || path.startsWith(directory + '/');
}
} // namespace

SimSystem::SimSystem(const QString &stateDirectory, QObject *parent)
    : QObject(parent), m_directory(stateDirectory),
      m_processes{{1, "aurora.kernel", 384}, {2, "desktop", 128}} {
    m_uptime.start();
    seed();
}

void SimSystem::seed() {
    m_entries.clear();
    const auto dir = [this](const QString &path, int permissions) {
        m_entries.insert(path, {path, true, {}, permissions});
    };
    for (const auto &path : {"/", "/bin", "/etc", "/sys", "/home"})
        dir(path, 0555);
    for (const auto &path : {"/home/guest", "/home/guest/docs", "/home/guest/images", "/tmp"})
        dir(path, 0755);
    const auto file = [this](const QString &path, const QByteArray &data, int permissions = 0644) {
        m_entries.insert(path, {path, false, data, permissions});
    };
    file("/home/guest/README.txt",
         "WELCOME TO AURORA/86 1.3\n"
         "A fictional A16 workstation from Aurora Laboratories.\n\n"
         "Type help in Terminal. Your home directory is /home/guest.\n"
         "Try basic to write a program in A16 BASIC, or basic docs/welcome.bas.\n"
         "Use edit notes.txt and Ctrl+S to write a document.\n"
         "F11 toggles fullscreen; windows can be moved and resized.\n"
         "F1 opens the station handbook. Print Screen saves a picture in images/.\n"
         "All applications share a 2 MiB virtual disk and 4096 KiB simulated RAM.\n\n"
         "PACKAGES\nInstall: aurora -i fastfetch htop tree grep coreutils\n"
         "Run: fastfetch, htop, tree, grep Aurora /etc/release\n"
         "Search: aurora -s text    List: aurora --list\n"
         "Installed: aurora -l     Remove: aurora -r htop\n"
         "Packages are simulated; installation never changes Linux.\n"
         "Read live system information with cat /proc/meminfo or ls /proc.\n\n"
         "LUA PROGRAMS\nOpen Lua Studio with mods. Drop .lua files in its selected mods/ folder.\n"
         "Try aurora -i notepad todo, then run notepad or todo. Both save your work.\n"
         "Run a virtual script with lua /home/guest/example.lua.\n\n"
         "Your files, settings, wallpaper and puzzle progress are saved automatically.\n");
    file("/home/guest/docs/welcome.bas",
         "10 REM WELCOME TO A16 BASIC\n"
         "20 PRINT \"AURORA/86 - A16 BASIC\"\n"
         "30 PRINT \"--------------------\"\n"
         "40 FOR I = 1 TO 4\n"
         "50 PRINT I; \" SQUARED IS \"; I * I\n"
         "60 BEEP\n"
         "70 NEXT I\n"
         "80 PRINT \"EDIT THIS FILE, THEN RUN AGAIN.\"\n"
         "90 END\n");
    file("/home/guest/docs/journal.txt",
         "23 OCT 1986\n\nToday the A16 arrived. Our workshop has a window into the future.\n");
    file("/home/guest/docs/commands.txt",
         "EXAMPLE COMMANDS (one per line)\n\nhelp\nls -l\ncd docs\ncat journal.txt\necho \"Hello, Aurora\" > "
         "notes.txt\nmkdir projects\nmv notes.txt projects/\nchmod 444 "
         "projects/notes.txt\nfree\ndf\nps\naurora -i fastfetch htop tree grep "
         "coreutils\nfastfetch\nhtop\ntree\n");
    file("/etc/release", "AURORA/86 1.2\nAurora Laboratories / October 1986\n", 0444);
    file("/sys/hardware",
         "CPU: A16 / 16-bit bus / 8 MHz\nRAM: 4096 KiB\nDISK: M2 magnetic drive / 2048 KiB\nVIDEO: LUME "
         "adapter / 960 x 640 / 4 tones\nSOUND: mono waveform generator\n",
         0444);
    file("/bin/README",
         "Built-in applications: terminal files edit calc browser games packages about handbook mods lua\nCommands are "
         "interpreted by ASTER. Use aurora --list to browse optional packages.\n",
         0444);
    QByteArray ppm("P6\n160 100\n255\n");
    for (int y = 0; y < 100; ++y) {
        for (int x = 0; x < 160; ++x) {
            const bool sun = (x - 112) * (x - 112) + (y - 35) * (y - 35) < 17 * 17;
            const bool mountain = y > 70 - std::abs(80 - x) / 3;
            ppm.append(char(mountain ? 22 : sun ? 244 : 30 + y));
            ppm.append(char(mountain ? 42 : sun ? 190 : 38 + y / 2));
            ppm.append(char(mountain ? 53 : sun ? 85 : 76 + y));
        }
    }
    file("/home/guest/images/aurora.ppm", ppm);
}

bool SimSystem::fail(const QString &message) const {
    m_error = message;
    return false;
}
QString SimSystem::lastError() const { return m_error; }

std::optional<VEntry> SimSystem::entryFor(const QString &path) const {
    if (!under(path, "/proc")) {
        const auto it = m_entries.constFind(path);
        return it == m_entries.cend() ? std::nullopt : std::optional<VEntry>(*it);
    }
    if (path == "/proc")
        return VEntry{path, true, {}, 0555};
    if (path == "/proc/cpuinfo")
        return VEntry{path, false,
                      "processor\t: 0\nvendor_id\t: Aurora Laboratories\nmodel name\t: A16\n"
                      "cpu MHz\t\t: 8\nbus width\t: 16 bits\n",
                      0444};
    if (path == "/proc/meminfo")
        return VEntry{path, false,
                      QString("MemTotal: %1 kB\nMemFree: %2 kB\nMemUsed: %3 kB\n")
                          .arg(MemoryKiB)
                          .arg(MemoryKiB - usedMemoryKiB())
                          .arg(usedMemoryKiB())
                          .toUtf8(),
                      0444};
    if (path == "/proc/uptime")
        return VEntry{path, false, QByteArray::number(m_uptime.elapsed() / 1000.0, 'f', 2) + "\n", 0444};
    if (path == "/proc/version")
        return VEntry{path, false, "AURORA/86 1.3 (aster@aurora) #1 A16\n", 0444};
    if (path == "/proc/loadavg")
        return VEntry{path, false,
                      QString("%1 0.00 0.00 1/%2 %3\n")
                          .arg(QString::number(double(m_processes.size()) / qMax(1, MemoryKiB / 1024), 'f', 2))
                          .arg(m_processes.size())
                          .arg(m_processes.isEmpty() ? 1 : m_processes.first().pid)
                          .toUtf8(),
                      0444};
    const QStringList parts = path.mid(6).split('/');
    if (parts.isEmpty() || parts.size() > 2)
        return std::nullopt;
    bool valid = false;
    const int pid = parts.first().toInt(&valid);
    if (!valid || QString::number(pid) != parts.first())
        return std::nullopt;
    for (const auto &process : m_processes) {
        if (process.pid != pid)
            continue;
        if (parts.size() == 1)
            return VEntry{path, true, {}, 0555};
        if (parts.last() == "cmdline")
            return VEntry{path, false, process.name.toUtf8() + '\0', 0444};
        if (parts.last() == "status")
            return VEntry{path, false,
                          QString("Name:\t%1\nState:\tR (simulated)\nPid:\t%2\nVmSize:\t%3 kB\n")
                              .arg(process.name)
                              .arg(pid)
                              .arg(process.memoryKiB)
                              .toUtf8(),
                          0444};
        return std::nullopt;
    }
    return std::nullopt;
}

QString SimSystem::normalize(const QString &input, const QString &cwd) const {
    if (input.contains(QChar::Null) || input.contains('\n') || input.contains('\r')) {
        fail("Invalid path.");
        return {};
    }
    QString path = input;
    if (path == "~")
        path = "/home/guest";
    if (path.startsWith("~/"))
        path = "/home/guest/" + path.mid(2);
    if (path.isEmpty())
        path = cwd;
    if (!path.startsWith('/'))
        path = cwd + '/' + path;
    QStringList parts;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == ".")
            continue;
        if (part == "..") {
            if (!parts.isEmpty())
                parts.removeLast();
        } else
            parts.append(part);
    }
    const QString result = '/' + parts.join('/');
    if (result.size() > 1024) {
        fail("Path is too long.");
        return {};
    }
    return result;
}

bool SimSystem::accessible(const QString &path) const {
    if (path.isEmpty())
        return fail("Invalid path.");
    QString parent = path == "/" ? "/" : parentPath(path);
    while (true) {
        const auto entry = entryFor(parent);
        if (!entry || !entry->directory)
            return fail("Directory not found: " + parent);
        if (!(entry->permissions & 0100))
            return fail("Permission denied: " + parent);
        if (parent == "/")
            break;
        parent = parentPath(parent);
    }
    return true;
}

bool SimSystem::protectedPath(const QString &path) const {
    return !(under(path, "/home/guest") || under(path, "/tmp")) || path == "/home/guest" || path == "/tmp";
}

bool SimSystem::writableParent(const QString &path) const {
    if (!accessible(path))
        return false;
    if (protectedPath(path))
        return fail("Protected system volume: " + path);
    const auto parent = m_entries.constFind(parentPath(path));
    if (parent == m_entries.cend() || !parent->directory)
        return fail("Destination directory not found.");
    if ((parent->permissions & 0300) != 0300)
        return fail("Permission denied: " + parent->path);
    return true;
}

bool SimSystem::exists(const QString &input) const {
    m_error.clear();
    const QString path = normalize(input);
    return accessible(path) && entryFor(path).has_value();
}

bool SimSystem::isDirectory(const QString &input) const {
    m_error.clear();
    const QString path = normalize(input);
    if (!accessible(path))
        return false;
    const auto entry = entryFor(path);
    return entry && entry->directory;
}

QList<VEntry> SimSystem::list(const QString &input) const {
    m_error.clear();
    const QString path = normalize(input);
    if (!accessible(path))
        return {};
    const auto directory = entryFor(path);
    if (!directory || !directory->directory) {
        fail("Directory not found: " + path);
        return {};
    }
    if ((directory->permissions & 0500) != 0500) {
        fail("Permission denied: " + path);
        return {};
    }
    QList<VEntry> result;
    for (const auto &entry : m_entries)
        if (entry.path != path && parentPath(entry.path) == path)
            result.append(entry);
    if (path == "/")
        result.append(*entryFor("/proc"));
    else if (path == "/proc") {
        for (const QString &name : QStringList{"cpuinfo", "loadavg", "meminfo", "uptime", "version"})
            result.append(*entryFor(path + '/' + name));
        for (const auto &process : m_processes)
            result.append(*entryFor(path + '/' + QString::number(process.pid)));
    } else if (under(path, "/proc")) {
        result.append(*entryFor(path + "/cmdline"));
        result.append(*entryFor(path + "/status"));
    }
    std::sort(result.begin(), result.end(), [](const VEntry &a, const VEntry &b) {
        if (a.directory != b.directory)
            return a.directory;
        return a.path < b.path;
    });
    return result;
}

QByteArray SimSystem::read(const QString &input) const {
    m_error.clear();
    const QString path = normalize(input);
    if (!accessible(path))
        return {};
    const auto entry = entryFor(path);
    if (!entry) {
        fail("File not found: " + path);
        return {};
    }
    if (entry->directory) {
        fail("Is a directory: " + path);
        return {};
    }
    if (!(entry->permissions & 0400)) {
        fail("Permission denied: " + path);
        return {};
    }
    return entry->data;
}

int SimSystem::usedBytes() const {
    int bytes = 0;
    for (const auto &entry : m_entries)
        bytes += int(entry.data.size()) + int(entry.path.toUtf8().size()) + 64;
    return bytes;
}

bool SimSystem::commit(const QMap<QString, VEntry> &previous) {
    if (!save()) {
        m_entries = previous;
        return false;
    }
    emit changed();
    return true;
}

bool SimSystem::write(const QString &input, const QByteArray &data) {
    m_error.clear();
    const QString path = normalize(input);
    if (!accessible(path))
        return false;
    const auto it = m_entries.constFind(path);
    const bool existing = it != m_entries.cend();
    if (existing) {
        if (it->directory)
            return fail("Is a directory: " + path);
        if (protectedPath(path) || !(it->permissions & 0200))
            return fail("Permission denied: " + path);
    } else if (!writableParent(path))
        return false;
    const qsizetype size =
        usedBytes() - (existing ? it->data.size() : -64 - path.toUtf8().size()) + data.size();
    if (size > DiskBytes)
        return fail("Disk full (limit: 2048 KiB).");
    const auto previous = m_entries;
    m_entries.insert(path, {path, false, data, existing ? it->permissions : 0644});
    return commit(previous);
}

bool SimSystem::mkdir(const QString &input) {
    m_error.clear();
    const QString path = normalize(input);
    if (under(path, "/proc"))
        return fail("Protected system volume: " + path);
    if (m_entries.contains(path))
        return fail("Already exists: " + path);
    if (!writableParent(path))
        return false;
    if (usedBytes() + 64 + path.toUtf8().size() > DiskBytes)
        return fail("Disk full.");
    const auto previous = m_entries;
    m_entries.insert(path, {path, true, {}, 0755});
    return commit(previous);
}

bool SimSystem::remove(const QString &input, bool recursive) {
    m_error.clear();
    const QString path = normalize(input);
    if (under(path, "/proc"))
        return fail("Protected system volume: " + path);
    if (!m_entries.contains(path))
        return fail("Not found: " + path);
    if (!writableParent(path))
        return false;
    QList<QString> paths;
    for (const auto &entry : m_entries)
        if (under(entry.path, path))
            paths.append(entry.path);
    if (paths.size() > 1 && !recursive)
        return fail("Directory is not empty; use rm -r.");
    for (const auto &key : paths)
        if (!writableParent(key))
            return false;
    const auto previous = m_entries;
    for (const auto &key : paths)
        m_entries.remove(key);
    return commit(previous);
}

bool SimSystem::move(const QString &input, const QString &destination) {
    m_error.clear();
    const QString source = normalize(input);
    QString target = normalize(destination);
    if (under(source, "/proc") || under(target, "/proc"))
        return fail("Protected system volume: /proc");
    if (!m_entries.contains(source))
        return fail("Not found: " + source);
    if (!accessible(target))
        return false;
    if (m_entries.contains(target) && m_entries.value(target).directory)
        target += (target == "/" ? "" : "/") + source.section('/', -1);
    if (source == target)
        return true;
    if (under(target, source))
        return fail("Cannot move a directory inside itself.");
    if (m_entries.contains(target))
        return fail("Destination already exists: " + target);
    if (!writableParent(source) || !writableParent(target))
        return false;
    const auto previous = m_entries;
    for (const auto &entry : previous) {
        if (!under(entry.path, source))
            continue;
        auto moved = entry;
        moved.path = target + entry.path.mid(source.size());
        m_entries.remove(entry.path);
        m_entries.insert(moved.path, moved);
    }
    if (usedBytes() > DiskBytes) {
        m_entries = previous;
        return fail("Disk full.");
    }
    return commit(previous);
}

bool SimSystem::copy(const QString &input, const QString &destination) {
    m_error.clear();
    const QString source = normalize(input);
    const auto data = read(source);
    if (!m_error.isEmpty())
        return false;
    QString target = normalize(destination);
    const auto targetEntry = entryFor(target);
    if (targetEntry && targetEntry->directory)
        target += (target == "/" ? "" : "/") + source.section('/', -1);
    if (target == source)
        return fail("Source and destination are the same.");
    return write(target, data);
}

bool SimSystem::chmod(const QString &input, int permissions) {
    m_error.clear();
    const QString path = normalize(input);
    if (under(path, "/proc"))
        return fail("Protected system volume: " + path);
    if (!accessible(path))
        return false;
    if (!m_entries.contains(path))
        return fail("Not found: " + path);
    if (protectedPath(path))
        return fail("Protected system volume: " + path);
    if (permissions < 0 || permissions > 0777)
        return fail("Invalid permissions.");
    const auto previous = m_entries;
    m_entries[path].permissions = permissions;
    return commit(previous);
}

bool SimSystem::save() {
    if (m_blocked)
        return fail(
            "Invalid saved state was preserved. Close Aurora and rename vfs.json to start a new disk.");
    const auto settings = QJsonObject::fromVariantMap(m_settings);
    if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > 64 * 1024)
        return fail("Settings exceed the 64 KiB limit.");
    if (!QDir().mkpath(m_directory))
        return fail("Could not create the data directory.");
    QJsonArray entries;
    for (const auto &entry : m_entries)
        entries.append(QJsonObject{{"path", entry.path},
                                   {"directory", entry.directory},
                                   {"data", QString::fromLatin1(entry.data.toBase64())},
                                   {"permissions", entry.permissions}});
    QSaveFile file(QDir(m_directory).filePath("vfs.json"));
    if (!file.open(QIODevice::WriteOnly))
        return fail("Failed to save: " + file.errorString());
    const auto bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries}, {"settings", settings}})
            .toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail("Failed to save: " + file.errorString());
    m_error.clear();
    return true;
}

bool SimSystem::load() {
    m_error.clear();
    QFile file(QDir(m_directory).filePath("vfs.json"));
    if (!file.exists())
        return save();
    const auto invalid = [this]() {
        m_blocked = true;
        return fail(
            "Invalid or unreadable virtual disk. The original file was preserved; this session cannot save.");
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * DiskBytes)
        return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return invalid();
    const auto root = document.object();
    if (root.value("version").toInt() != 1 || !root.value("entries").isArray() ||
        !root.value("settings").isObject())
        return invalid();
    if (QJsonDocument(root.value("settings").toObject()).toJson(QJsonDocument::Compact).size() > 64 * 1024)
        return invalid();
    QMap<QString, VEntry> loaded;
    qsizetype bytes = 0;
    for (const auto &value : root.value("entries").toArray()) {
        if (!value.isObject())
            return invalid();
        const auto obj = value.toObject();
        const auto path = obj.value("path").toString();
        const int permissions = obj.value("permissions").toInt(-1);
        if (path.isEmpty() || !path.startsWith('/') || normalize(path) != path ||
            under(path, "/proc") || loaded.contains(path) ||
            permissions < 0 || permissions > 0777 || !obj.value("directory").isBool() ||
            !obj.value("data").isString())
            return invalid();
        const auto decoded = QByteArray::fromBase64Encoding(obj.value("data").toString().toLatin1(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return invalid();
        VEntry entry{path, obj.value("directory").toBool(), decoded.decoded, permissions};
        if (entry.directory && !entry.data.isEmpty())
            return invalid();
        bytes += entry.data.size() + entry.path.toUtf8().size() + 64;
        if (bytes > DiskBytes)
            return invalid();
        loaded.insert(path, entry);
    }
    for (const auto &path : {"/", "/bin", "/etc", "/sys", "/home", "/home/guest", "/tmp"})
        if (!loaded.contains(path) || !loaded.value(path).directory)
            return invalid();
    for (const auto &entry : loaded) {
        if (entry.path != "/" &&
            (!loaded.contains(parentPath(entry.path)) || !loaded.value(parentPath(entry.path)).directory))
            return invalid();
        if (protectedPath(entry.path) && entry.path != "/home/guest" && entry.path != "/tmp" &&
            (entry.permissions & 0200))
            return invalid();
    }
    for (const QString &path : QStringList{"/etc/release", "/sys/hardware", "/bin/README"}) {
        if (!loaded.contains(path))
            continue;
        const VEntry fresh = m_entries.value(path);
        if (bytes - loaded.value(path).data.size() + fresh.data.size() <= DiskBytes) {
            bytes += fresh.data.size() - loaded.value(path).data.size();
            loaded[path] = fresh;
        }
    }
    m_entries = loaded;
    m_settings = root.value("settings").toObject().toVariantMap();
    m_blocked = false;
    m_error.clear();
    emit changed();
    emit settingsChanged();
    return true;
}

int SimSystem::startProcess(const QString &name, int memoryKiB) {
    m_error.clear();
    if (memoryKiB <= 0 || memoryKiB > MemoryKiB - usedMemoryKiB()) {
        fail("Not enough memory. Close another application.");
        return -1;
    }
    const int pid = m_nextPid++;
    m_processes.append({pid, name, memoryKiB});
    emit processesChanged();
    emit changed();
    return pid;
}
bool SimSystem::stopProcess(int pid) {
    m_error.clear();
    if (pid < 3)
        return fail("Essential process is protected.");
    for (qsizetype i = 0; i < m_processes.size(); ++i)
        if (m_processes[i].pid == pid) {
            m_processes.removeAt(i);
            emit processesChanged();
            emit changed();
            return true;
        }
    return fail("Process not found.");
}
QList<SimProcess> SimSystem::processes() const { return m_processes; }
int SimSystem::usedMemoryKiB() const {
    int used = 0;
    for (const auto &p : m_processes)
        used += p.memoryKiB;
    return used;
}
QVariant SimSystem::setting(const QString &key, const QVariant &fallback) const {
    return m_settings.value(key, fallback);
}
void SimSystem::setSetting(const QString &key, const QVariant &value) { setSettings({{key, value}}); }
bool SimSystem::setSettings(const QVariantMap &values) {
    const auto previous = m_settings;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        m_settings.insert(it.key(), it.value());
    if (!save()) {
        m_settings = previous;
        return false;
    }
    emit settingsChanged();
    return true;
}

qint64 SimSystem::uptimeSeconds() const { return m_uptime.elapsed() / 1000; }
