#include "shell.h"

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
        error = "Aspas ou escape incompletos.";
        return {};
    }
    flush();
    return tokens;
}
} // namespace

Shell::Shell(SimSystem *system, QObject *parent) : QObject(parent), m_system(system) {}
QString Shell::cwd() const { return m_cwd; }
QString Shell::prompt() const {
    return "guest@aurora:" + (m_cwd.startsWith("/home/guest") ? "~" + m_cwd.mid(11) : m_cwd) + "$ ";
}

QString Shell::execute(const QString &line) {
    QString error;
    QList<int> operators;
    auto args = tokenize(line, error, operators);
    if (!error.isEmpty())
        return "erro: " + error;
    if (args.isEmpty())
        return {};
    const QString command = args.takeFirst();
    for (int &index : operators)
        --index;
    const auto path = [this](const QString &input) { return m_system->normalize(input, m_cwd); };
    const auto failure = [this] { return "erro: " + m_system->lastError(); };
    const auto usage = [](const QString &value) { return "uso: " + value; };
    if (operators.size() > 1 || (!operators.isEmpty() && operators.first() < 0))
        return "erro: redirecionamento inválido.";
    const int redirect = !operators.isEmpty() && args[operators.first()] == ">" ? operators.first() : -1;
    const int append = !operators.isEmpty() && args[operators.first()] == ">>" ? operators.first() : -1;
    if (command != "echo" && (redirect >= 0 || append >= 0))
        return "erro: redirecionamento disponível somente em echo.";
    if (command == "help")
        return "ASTER shell / AURORA 1.0\n"
               "ls [-l] [pasta]    cd [pasta]         pwd\n"
               "cat arquivo        echo texto [> ou >> arquivo]\n"
               "mkdir pasta        touch arquivo      rm [-r] caminho\n"
               "mv origem destino  cp origem destino  chmod 644 arquivo\n"
               "stat caminho       df                 free\n"
               "ps                 kill PID           date\n"
               "uname              whoami             clear\n"
               "edit [arquivo]     files [pasta]      calc\n"
               "browser [URL]      snake              blocks\n"
               "prisma             jardim             eco\n"
               "about              exit (fechar terminal)\n"
               "Aspas protegem nomes com espaços. ~ é /home/guest.\n"
               "Sem execução de programas ou comandos do Linux.";
    if (command == "pwd")
        return m_cwd;
    if (command == "whoami")
        return "guest";
    if (command == "uname")
        return "AURORA/86 1.0  A16  8MHz  4096KiB  outubro1986";
    if (command == "date") {
        const auto now = QDateTime::currentDateTime();
        auto vintage = now;
        vintage.setDate(QDate(1986, now.date().month(),
                              qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
        return now.addSecs(m_system->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong())
            .toString("dd/MM/yyyy HH:mm:ss");
    }
    if (command == "prisma" || command == "jardim" || command == "eco" || command == "games") {
        emit launchRequested(command == "prisma"   ? "prism"
                             : command == "jardim" ? "garden"
                             : command == "eco"    ? "echo"
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
        return "Sincronizando volumes...";
    }
    if (command == "cd") {
        if (args.size() > 1)
            return usage("cd [pasta]");
        const auto target = path(args.isEmpty() ? "~" : args.first());
        if (!m_system->isDirectory(target))
            return "erro: diretório indisponível: " + target;
        m_system->list(target);
        if (!m_system->lastError().isEmpty())
            return failure();
        m_cwd = target;
        return {};
    }
    if (command == "ls") {
        const bool detailed = args.removeAll("-l") > 0;
        if (args.size() > 1)
            return usage("ls [-l] [pasta]");
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
            return usage("cat arquivo");
        const auto data = m_system->read(path(args.first()));
        return m_system->lastError().isEmpty() ? QString::fromUtf8(data) : failure();
    }
    if (command == "echo") {
        const int index = redirect >= 0 ? redirect : append;
        if (index < 0)
            return args.join(' ');
        if (index != args.size() - 2 || (redirect >= 0 && append >= 0))
            return usage("echo texto > arquivo");
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
            return usage(command + " caminho");
        const auto target = path(args.first());
        if (command == "touch" && m_system->exists(target))
            return {};
        return (command == "mkdir" ? m_system->mkdir(target) : m_system->write(target, {})) ? QString()
                                                                                            : failure();
    }
    if (command == "rm") {
        const bool recursive = args.removeAll("-r") > 0;
        if (args.size() != 1)
            return usage("rm [-r] caminho");
        return m_system->remove(path(args.first()), recursive) ? QString() : failure();
    }
    if (command == "mv" || command == "cp") {
        if (args.size() != 2)
            return usage(command + " origem destino");
        return (command == "mv" ? m_system->move(path(args[0]), path(args[1]))
                                : m_system->copy(path(args[0]), path(args[1])))
                   ? QString()
                   : failure();
    }
    if (command == "chmod") {
        if (args.size() != 2 || !QRegularExpression("^[0-7]{3}$").match(args[0]).hasMatch())
            return usage("chmod 644 arquivo");
        return m_system->chmod(path(args[1]), args[0].toInt(nullptr, 8)) ? QString() : failure();
    }
    if (command == "stat") {
        if (args.size() != 1)
            return usage("stat caminho");
        const auto target = path(args.first());
        if (!m_system->exists(target))
            return "erro: arquivo indisponível.";
        if (target == "/")
            return "/  diretório protegido";
        for (const auto &entry :
             m_system->list(target.section('/', 0, -2).isEmpty() ? "/" : target.section('/', 0, -2)))
            if (entry.path == target)
                return QString("%1\n%2 guest  %3 bytes")
                    .arg(target, mode(entry.permissions, entry.directory))
                    .arg(entry.data.size());
        return failure();
    }
    if (command == "df")
        return QString("Volume    Capacidade  Usado     Livre\nAURORA-0  %1 KiB    %2 KiB   %3 KiB")
            .arg(SimSystem::DiskBytes / 1024)
            .arg((m_system->usedBytes() + 1023) / 1024)
            .arg((SimSystem::DiskBytes - m_system->usedBytes()) / 1024);
    if (command == "free")
        return QString("RAM      Total      Usada      Livre\n         %1 KiB   %2 KiB    %3 KiB")
            .arg(SimSystem::MemoryKiB)
            .arg(m_system->usedMemoryKiB())
            .arg(SimSystem::MemoryKiB - m_system->usedMemoryKiB());
    if (command == "ps") {
        QString result = "PID   RAM/KiB  PROCESSO";
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
            return usage(command + " [caminho]");
        QString target = args.isEmpty() ? QString() : args.first();
        if (command == "edit" || command == "files")
            target = path(target.isEmpty() && command == "files" ? m_cwd : target);
        if (command == "edit" && args.isEmpty())
            target.clear();
        emit launchRequested(command, target);
        return {};
    }
    return "erro: comando desconhecido: " + command + "\nDigite help para consultar os comandos.";
}
