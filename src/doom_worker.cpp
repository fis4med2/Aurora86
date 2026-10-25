#include "../third_party/doom/PureDOOM.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtEndian>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

// The original engine owns global state and may call exit(). It runs in this
// separate process; only RGBA frames, PCM samples and two-byte input commands
// cross the pipe. Closing Doom can never terminate the simulated desktop.
static QString wadPath, wadAlias;
static QElapsedTimer clockTimer;
static qint64 stoppedAt = 0, pausedTime = 0;
static bool helperPaused = false;
static std::mutex inputMutex;
static std::vector<std::pair<int, int>> input;
static std::atomic_bool inputClosed{false};
extern "C" void aurora_doom_save_config(void);

static void *openFile(const char *name, const char *mode) {
    const bool reading = std::strcmp(mode, "r") == 0 || std::strcmp(mode, "rb") == 0;
    const bool writing = std::strcmp(mode, "w") == 0 || std::strcmp(mode, "wb") == 0;
    if (!reading && !writing) return nullptr;
    QString path = QString::fromUtf8(name);
    const bool wad = path == "./" + wadAlias;
    if (wad) {
        if (!reading) return nullptr;
        path = wadPath;
    } else {
        if (path.startsWith("./")) path.remove(0, 2);
        const bool save = path.size() == 12 && path.startsWith("doomsav") && path.mid(8) == ".dsg" &&
                          path[7] >= QLatin1Char('0') && path[7] <= QLatin1Char('9');
        if (path != ".doomrc" && path != "default.cfg" && !save) return nullptr;
    }
    if (QFileInfo(path).isSymLink()) return nullptr;
    auto *file = new QFile;
    const auto flags = writing ? QIODevice::WriteOnly : QIODevice::ReadOnly;
#ifdef Q_OS_UNIX
    // All state names are single path components relative to the helper's save
    // directory. Reject symlinks atomically and never truncate non-regular files.
    const int descriptor = ::open(QFile::encodeName(path).constData(),
                                  (writing ? O_WRONLY | O_CREAT : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    struct stat info{};
    if (descriptor < 0 || ::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) ||
        (writing && (info.st_nlink != 1 || ::ftruncate(descriptor, 0) != 0))) {
        if (descriptor >= 0) ::close(descriptor);
        delete file;
        return nullptr;
    }
    if (!file->open(descriptor, flags, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        delete file;
        return nullptr;
    }
#else
    file->setFileName(path);
    if (!file->open(writing ? flags | QIODevice::Truncate : flags)) {
        delete file;
        return nullptr;
    }
#endif
    return file;
}
static void closeFile(void *file) { delete static_cast<QFile *>(file); }
static int readFile(void *f, void *buffer, int n) { return int(static_cast<QFile *>(f)->read(static_cast<char *>(buffer), n)); }
static int writeFile(void *f, const void *buffer, int n) { return int(static_cast<QFile *>(f)->write(static_cast<const char *>(buffer), n)); }
static int seekFile(void *f, int offset, doom_seek_t origin) {
    auto *file = static_cast<QFile *>(f);
    const qint64 start = origin == DOOM_SEEK_SET ? 0 : origin == DOOM_SEEK_END ? file->size() : file->pos();
    return file->seek(start + offset) ? 0 : -1;
}
static int tellFile(void *f) { return int(static_cast<QFile *>(f)->pos()); }
static int eofFile(void *f) { return static_cast<QFile *>(f)->atEnd(); }
static char *getEnvironment(const char *) { static char directory[] = "."; return directory; }
static void getTime(int *seconds, int *microseconds) {
    const qint64 elapsed = (helperPaused ? stoppedAt : clockTimer.elapsed()) - pausedTime;
    *seconds = int(elapsed / 1000);
    *microseconds = int(elapsed % 1000) * 1000;
}
static void sendPacket(const char *type, const void *data, quint32 size) {
    char header[8];
    std::memcpy(header, type, 4);
    qToLittleEndian(size, reinterpret_cast<uchar *>(header + 4));
    if (std::fwrite(header, 1, 8, stdout) != 8 || std::fwrite(data, 1, size, stdout) != size)
        std::exit(0);
    std::fflush(stdout);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 3)
        return 2;
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    wadPath = QString::fromLocal8Bit(argv[1]);
    wadAlias = QString::fromLatin1(argv[2]) + ".wad";
    doom_set_print([](const char *s) { std::fputs(s, stderr); });
    doom_set_file_io(openFile, closeFile, readFile, writeFile, seekFile, tellFile, eofFile);
    doom_set_getenv(getEnvironment);
    clockTimer.start();
    doom_set_gettime(getTime);
    doom_set_default_int("key_up", 'w');
    doom_set_default_int("key_down", 's');
    doom_set_default_int("key_strafeleft", 'a');
    doom_set_default_int("key_straferight", 'd');
    doom_set_default_int("key_use", 'e');
    doom_set_default_int("key_fire", DOOM_KEY_CTRL);
    doom_set_default_int("music_volume", 0);
    char program[] = "aurora86-doom", warp[] = "-warp", one[] = "1", skill[] = "-skill", three[] = "3";
    char *doomArgs[] = {program, warp, one, one, skill, three};
    doom_init(6, doomArgs, DOOM_FLAG_HIDE_MUSIC_OPTIONS | DOOM_FLAG_MENU_DARKEN_BG);
    std::thread([] {
        unsigned char command[2];
        int used = 0;
        for (;;) {
#ifdef Q_OS_WIN
            const int count = _read(_fileno(stdin), command + used, 2 - used);
#else
            const auto count = ::read(STDIN_FILENO, command + used, 2 - used);
#endif
            if (count <= 0) { inputClosed = true; return; }
            used += int(count);
            if (used < 2) continue;
            used = 0;
            std::lock_guard lock(inputMutex);
            if (input.size() < 512)
                input.emplace_back(command[0], command[1]);
        }
    }).detach();
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (inputClosed) { aurora_doom_save_config(); app.quit(); return; }
        std::vector<std::pair<int, int>> events;
        { std::lock_guard lock(inputMutex); events.swap(input); }
        for (auto [kind, key] : events) {
            if (kind == 'D' || kind == 'U') {
                const auto event = kind == 'D' ? doom_key_down : doom_key_up;
                event(static_cast<doom_key_t>(key));
                // Arrow keys remain menu navigation and also honor WASD bindings.
                if (key == DOOM_KEY_UP_ARROW) event(DOOM_KEY_W);
                if (key == DOOM_KEY_DOWN_ARROW) event(DOOM_KEY_S);
                if (key == DOOM_KEY_SPACE) event(DOOM_KEY_E);
            }
            else if (kind == 'P' && bool(key) != helperPaused) {
                if (key) stoppedAt = clockTimer.elapsed();
                else pausedTime += clockTimer.elapsed() - stoppedAt;
                helperPaused = bool(key);
            } else if (kind == 'Q') { aurora_doom_save_config(); app.quit(); return; }
        }
        if (!helperPaused) {
            doom_update();
            sendPacket("FRAM", doom_get_framebuffer(4), 320 * 200 * 4);
        }
    });
    timer.start(29);
    QTimer audio;
    audio.setTimerType(Qt::PreciseTimer);
    QObject::connect(&audio, &QTimer::timeout, &app, [] {
        if (!helperPaused) sendPacket("AUDI", doom_get_sound_buffer(), 2048);
    });
    audio.start(46);
    return app.exec();
}
