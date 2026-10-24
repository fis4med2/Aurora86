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
}

SimSystem::SimSystem(const QString &stateDirectory, QObject *parent)
    : QObject(parent), m_directory(stateDirectory),
      m_processes{{1, "aurora.kernel", 384}, {2, "desktop", 128}} {
    seed();
}

void SimSystem::seed() {
    m_entries.clear();
    const auto dir = [this](const QString &path, int permissions) {
        m_entries.insert(path, {path, true, {}, permissions});
    };
    for (const auto &path : {"/", "/bin", "/etc", "/sys", "/home"}) dir(path, 0555);
    for (const auto &path : {"/home/guest", "/home/guest/docs", "/home/guest/images", "/tmp"}) dir(path, 0755);
    const auto file = [this](const QString &path, const QByteArray &data, int permissions = 0644) {
        m_entries.insert(path, {path, false, data, permissions});
    };
    file("/home/guest/LEIA-ME.txt", QString::fromUtf8(
        "BEM-VINDO AO AURORA/86\n"
        "Computador pessoal fictício, estação A16 dos Laboratórios Aurora.\n\n"
        "Terminal: digite help. Caminho inicial: /home/guest.\n"
        "Use edit notas.txt para criar um documento e Ctrl+S para salvar.\n"
        "Todos os aplicativos compartilham o disco virtual de 2 MiB.\n"
        "A memória simulada é de 4096 KiB. Feche programas para liberá-la.\n"
        "F11 alterna tela cheia. Janelas podem ser movidas e fechadas.\n\n"
        "O Navegador usa uma ponte moderna para a internet real.\n"
        "Essa ponte não representa hardware de 1986.\n"
        "Arquivos virtuais nunca executam comandos do Linux.\n"
        "Documentos e preferências são salvos automaticamente entre sessões.\n").toUtf8());
    file("/home/guest/docs/diario.txt", "23 OUT 1986\n\nHoje a estação A16 chegou. A oficina ganhou uma janela para o futuro.\n");
    file("/home/guest/docs/comandos.txt", "COMANDOS DE EXEMPLO (uma linha por vez)\n\nhelp\nls -l\ncd docs\ncat diario.txt\necho \"Olá, Aurora\" > notas.txt\nmkdir projetos\nmv notas.txt projetos/\nchmod 444 projetos/notas.txt\nfree\ndf\nps\n");
    file("/etc/release", "AURORA/86 1.0\nLaboratórios Aurora / outubro de 1986\n", 0444);
    file("/sys/hardware", "CPU: A16 / barramento de 16 bits / 8 MHz\nRAM: 4096 KiB\nDISCO: unidade magnetica M2 / 2048 KiB\nVIDEO: adaptador LUME / 960 x 640 / 4 tons\nSOM: gerador de ondas / mono\n", 0444);
    file("/bin/README", "Programas internos: terminal files edit calc browser snake blocks about\nComandos implementados pelo interpretador Aster.\n", 0444);
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

bool SimSystem::fail(const QString &message) const { m_error = message; return false; }
QString SimSystem::lastError() const { return m_error; }

QString SimSystem::normalize(const QString &input, const QString &cwd) const {
    if (input.contains(QChar::Null) || input.contains('\n') || input.contains('\r')) {
        fail("Caminho inválido."); return {};
    }
    QString path = input;
    if (path == "~") path = "/home/guest";
    if (path.startsWith("~/")) path = "/home/guest/" + path.mid(2);
    if (path.isEmpty()) path = cwd;
    if (!path.startsWith('/')) path = cwd + '/' + path;
    QStringList parts;
    for (const auto &part : path.split('/')) {
        if (part.isEmpty() || part == ".") continue;
        if (part == "..") { if (!parts.isEmpty()) parts.removeLast(); }
        else parts.append(part);
    }
    const QString result = '/' + parts.join('/');
    if (result.size() > 1024) { fail("Caminho longo demais."); return {}; }
    return result;
}

bool SimSystem::accessible(const QString &path) const {
    if (path.isEmpty()) return fail("Caminho inválido.");
    QString parent = path == "/" ? "/" : parentPath(path);
    while (true) {
        const auto it = m_entries.constFind(parent);
        if (it == m_entries.cend() || !it->directory) return fail("Diretório não encontrado: " + parent);
        if (!(it->permissions & 0100)) return fail("Permissão negada: " + parent);
        if (parent == "/") break;
        parent = parentPath(parent);
    }
    return true;
}

bool SimSystem::protectedPath(const QString &path) const {
    return !(under(path, "/home/guest") || under(path, "/tmp")) ||
           path == "/home/guest" || path == "/tmp";
}

bool SimSystem::writableParent(const QString &path) const {
    if (!accessible(path)) return false;
    if (protectedPath(path)) return fail("Volume do sistema protegido: " + path);
    const auto parent = m_entries.constFind(parentPath(path));
    if (parent == m_entries.cend() || !parent->directory) return fail("Diretório de destino não encontrado.");
    if ((parent->permissions & 0300) != 0300) return fail("Permissão negada: " + parent->path);
    return true;
}

bool SimSystem::exists(const QString &input) const {
    m_error.clear(); const QString path = normalize(input);
    return accessible(path) && m_entries.contains(path);
}

bool SimSystem::isDirectory(const QString &input) const {
    m_error.clear(); const QString path = normalize(input);
    return accessible(path) && m_entries.contains(path) && m_entries.value(path).directory;
}

QList<VEntry> SimSystem::list(const QString &input) const {
    m_error.clear(); const QString path = normalize(input);
    if (!accessible(path)) return {};
    if (!m_entries.contains(path) || !m_entries.value(path).directory) { fail("Diretório não encontrado: " + path); return {}; }
    if ((m_entries.value(path).permissions & 0500) != 0500) { fail("Permissão negada: " + path); return {}; }
    QList<VEntry> result;
    for (const auto &entry : m_entries) if (entry.path != path && parentPath(entry.path) == path) result.append(entry);
    std::sort(result.begin(), result.end(), [](const VEntry &a, const VEntry &b) {
        if (a.directory != b.directory) return a.directory;
        return a.path < b.path;
    });
    return result;
}

QByteArray SimSystem::read(const QString &input) const {
    m_error.clear(); const QString path = normalize(input);
    if (!accessible(path)) return {};
    const auto it = m_entries.constFind(path);
    if (it == m_entries.cend()) { fail("Arquivo não encontrado: " + path); return {}; }
    if (it->directory) { fail("É um diretório: " + path); return {}; }
    if (!(it->permissions & 0400)) { fail("Permissão negada: " + path); return {}; }
    return it->data;
}

int SimSystem::usedBytes() const {
    int bytes = 0;
    for (const auto &entry : m_entries) bytes += int(entry.data.size()) + int(entry.path.toUtf8().size()) + 64;
    return bytes;
}

bool SimSystem::commit(const QMap<QString, VEntry> &previous) {
    if (!save()) { m_entries = previous; return false; }
    emit changed(); return true;
}

bool SimSystem::write(const QString &input, const QByteArray &data) {
    m_error.clear(); const QString path = normalize(input);
    if (!accessible(path)) return false;
    const auto it = m_entries.constFind(path);
    const bool existing = it != m_entries.cend();
    if (existing) {
        if (it->directory) return fail("É um diretório: " + path);
        if (protectedPath(path) || !(it->permissions & 0200)) return fail("Permissão negada: " + path);
    } else if (!writableParent(path)) return false;
    const qsizetype size = usedBytes() - (existing ? it->data.size() : -64 - path.toUtf8().size()) + data.size();
    if (size > DiskBytes) return fail("Disco cheio (limite: 2048 KiB).");
    const auto previous = m_entries;
    m_entries.insert(path, {path, false, data, existing ? it->permissions : 0644});
    return commit(previous);
}

bool SimSystem::mkdir(const QString &input) {
    m_error.clear(); const QString path = normalize(input);
    if (m_entries.contains(path)) return fail("Já existe: " + path);
    if (!writableParent(path)) return false;
    if (usedBytes() + 64 + path.toUtf8().size() > DiskBytes) return fail("Disco cheio.");
    const auto previous = m_entries;
    m_entries.insert(path, {path, true, {}, 0755});
    return commit(previous);
}

bool SimSystem::remove(const QString &input, bool recursive) {
    m_error.clear(); const QString path = normalize(input);
    if (!m_entries.contains(path)) return fail("Não encontrado: " + path);
    if (!writableParent(path)) return false;
    QList<QString> paths;
    for (const auto &entry : m_entries) if (under(entry.path, path)) paths.append(entry.path);
    if (paths.size() > 1 && !recursive) return fail("Diretório não vazio; use rm -r.");
    for (const auto &key : paths) if (!writableParent(key)) return false;
    const auto previous = m_entries;
    for (const auto &key : paths) m_entries.remove(key);
    return commit(previous);
}

bool SimSystem::move(const QString &input, const QString &destination) {
    m_error.clear(); const QString source = normalize(input);
    QString target = normalize(destination);
    if (!m_entries.contains(source)) return fail("Não encontrado: " + source);
    if (!accessible(target)) return false;
    if (m_entries.contains(target) && m_entries.value(target).directory) target += (target == "/" ? "" : "/") + source.section('/', -1);
    if (source == target) return true;
    if (under(target, source)) return fail("Não é possível mover um diretório para dentro dele mesmo.");
    if (m_entries.contains(target)) return fail("Destino já existe: " + target);
    if (!writableParent(source) || !writableParent(target)) return false;
    const auto previous = m_entries;
    for (const auto &entry : previous) {
        if (!under(entry.path, source)) continue;
        auto moved = entry;
        moved.path = target + entry.path.mid(source.size());
        m_entries.remove(entry.path);
        m_entries.insert(moved.path, moved);
    }
    if (usedBytes() > DiskBytes) { m_entries = previous; return fail("Disco cheio."); }
    return commit(previous);
}

bool SimSystem::copy(const QString &input, const QString &destination) {
    m_error.clear(); const QString source = normalize(input);
    const auto data = read(source);
    if (!m_error.isEmpty()) return false;
    QString target = normalize(destination);
    if (m_entries.contains(target) && m_entries.value(target).directory) target += (target == "/" ? "" : "/") + source.section('/', -1);
    if (target == source) return fail("Origem e destino são iguais.");
    return write(target, data);
}

bool SimSystem::chmod(const QString &input, int permissions) {
    m_error.clear(); const QString path = normalize(input);
    if (!accessible(path)) return false;
    if (!m_entries.contains(path)) return fail("Não encontrado: " + path);
    if (protectedPath(path)) return fail("Volume do sistema protegido: " + path);
    if (permissions < 0 || permissions > 0777) return fail("Permissões inválidas.");
    const auto previous = m_entries;
    m_entries[path].permissions = permissions;
    return commit(previous);
}

bool SimSystem::save() {
    if (m_blocked) return fail("Estado salvo inválido foi preservado. Feche o Aurora e renomeie vfs.json para iniciar um disco novo.");
    const auto settings = QJsonObject::fromVariantMap(m_settings);
    if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > 64 * 1024)
        return fail("Preferências excedem o limite de 64 KiB.");
    if (!QDir().mkpath(m_directory)) return fail("Não foi possível criar o diretório de dados.");
    QJsonArray entries;
    for (const auto &entry : m_entries) entries.append(QJsonObject{{"path", entry.path}, {"directory", entry.directory},
        {"data", QString::fromLatin1(entry.data.toBase64())}, {"permissions", entry.permissions}});
    QSaveFile file(QDir(m_directory).filePath("vfs.json"));
    if (!file.open(QIODevice::WriteOnly)) return fail("Falha ao salvar: " + file.errorString());
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries},
        {"settings", settings}}).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return fail("Falha ao salvar: " + file.errorString());
    m_error.clear(); return true;
}

bool SimSystem::load() {
    m_error.clear();
    QFile file(QDir(m_directory).filePath("vfs.json"));
    if (!file.exists()) return save();
    const auto invalid = [this]() {
        m_blocked = true;
        return fail("Disco virtual inválido ou ilegível. O arquivo original foi preservado; esta sessão não pode gravar.");
    };
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * DiskBytes) return invalid();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
    const auto root = document.object();
    if (root.value("version").toInt() != 1 || !root.value("entries").isArray() || !root.value("settings").isObject()) return invalid();
    if (QJsonDocument(root.value("settings").toObject()).toJson(QJsonDocument::Compact).size() > 64 * 1024) return invalid();
    QMap<QString, VEntry> loaded;
    qsizetype bytes = 0;
    for (const auto &value : root.value("entries").toArray()) {
        if (!value.isObject()) return invalid();
        const auto obj = value.toObject();
        const auto path = obj.value("path").toString();
        const int permissions = obj.value("permissions").toInt(-1);
        if (path.isEmpty() || !path.startsWith('/') || normalize(path) != path || loaded.contains(path) ||
            permissions < 0 || permissions > 0777 || !obj.value("directory").isBool() || !obj.value("data").isString()) return invalid();
        const auto decoded = QByteArray::fromBase64Encoding(obj.value("data").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return invalid();
        VEntry entry{path, obj.value("directory").toBool(), decoded.decoded, permissions};
        if (entry.directory && !entry.data.isEmpty()) return invalid();
        bytes += entry.data.size() + entry.path.toUtf8().size() + 64;
        if (bytes > DiskBytes) return invalid();
        loaded.insert(path, entry);
    }
    for (const auto &path : {"/", "/bin", "/etc", "/sys", "/home", "/home/guest", "/tmp"})
        if (!loaded.contains(path) || !loaded.value(path).directory) return invalid();
    for (const auto &entry : loaded) {
        if (entry.path != "/" && (!loaded.contains(parentPath(entry.path)) || !loaded.value(parentPath(entry.path)).directory)) return invalid();
        if (protectedPath(entry.path) && entry.path != "/home/guest" && entry.path != "/tmp" && (entry.permissions & 0200)) return invalid();
    }
    m_entries = loaded;
    m_settings = root.value("settings").toObject().toVariantMap();
    m_blocked = false; m_error.clear();
    emit changed(); emit settingsChanged(); return true;
}

int SimSystem::startProcess(const QString &name, int memoryKiB) {
    m_error.clear();
    if (memoryKiB <= 0 || memoryKiB > MemoryKiB - usedMemoryKiB()) { fail("Memória insuficiente. Feche outro programa."); return -1; }
    const int pid = m_nextPid++;
    m_processes.append({pid, name, memoryKiB});
    emit processesChanged(); return pid;
}
bool SimSystem::stopProcess(int pid) {
    m_error.clear();
    if (pid < 3) return fail("Processo essencial protegido.");
    for (qsizetype i = 0; i < m_processes.size(); ++i) if (m_processes[i].pid == pid) {
        m_processes.removeAt(i); emit processesChanged(); return true;
    }
    return fail("Processo não encontrado.");
}
QList<SimProcess> SimSystem::processes() const { return m_processes; }
int SimSystem::usedMemoryKiB() const { int used = 0; for (const auto &p : m_processes) used += p.memoryKiB; return used; }
QVariant SimSystem::setting(const QString &key, const QVariant &fallback) const { return m_settings.value(key, fallback); }
void SimSystem::setSetting(const QString &key, const QVariant &value) {
    const auto previous = m_settings;
    m_settings.insert(key, value);
    if (!save()) { m_settings = previous; return; }
    emit settingsChanged();
}
