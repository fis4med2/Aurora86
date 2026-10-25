#include "doom.h"
#include "system.h"
#include "../third_party/doom/PureDOOM.h"
#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QtEndian>
#include <functional>
#if AURORA_WITH_ZLIB
#include <zlib.h>
#endif
#if AURORA_WITH_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#endif

DoomWadInfo inspectDoomWad(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, "Cannot read this WAD."};
    if (file.size() < 12 || file.size() > 128 * 1024 * 1024)
        return {{}, "Choose a Doom IWAD smaller than 128 MiB."};
    const QByteArray header = file.read(12);
    if (header.first(4) != "IWAD") return {{}, "Choose a complete IWAD (Doom, Doom II or Freedoom), not a patch WAD."};
    const auto *bytes = reinterpret_cast<const uchar *>(header.constData());
    const quint32 count = qFromLittleEndian<quint32>(bytes + 4), offset = qFromLittleEndian<quint32>(bytes + 8);
    if (count > 100000 || quint64(offset) + quint64(count) * 16 > quint64(file.size()) || !file.seek(offset))
        return {{}, "Invalid WAD directory."};
    QSet<QByteArray> names;
    for (quint32 i = 0; i < count; ++i) {
        const QByteArray entry = file.read(16);
        if (entry.size() != 16) return {{}, "Truncated WAD directory."};
        const auto *entryBytes = reinterpret_cast<const uchar *>(entry.constData());
        if (quint64(qFromLittleEndian<quint32>(entryBytes)) + qFromLittleEndian<quint32>(entryBytes + 4) > quint64(file.size()))
            return {{}, "A WAD lump extends beyond the file."};
        names.insert(entry.mid(8, 8).split('\0').first().toUpper());
    }
    for (const auto &required : {"PLAYPAL", "COLORMAP", "TEXTURE1", "PNAMES"})
        if (!names.contains(required)) return {{}, "This IWAD is missing Doom graphics data."};
    if (names.contains("MAP01")) return {"doom2", {}};
    if (names.contains("E4M1")) return {"doomu", {}};
    if (names.contains("E2M1")) return {"doom", {}};
    if (names.contains("E1M1")) return {"doom1", {}};
    return {{}, "No Doom levels found in this IWAD."};
}

namespace {
constexpr qint64 SharewareBytes = 4196020;
const QByteArray FreedoomSha256("7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d");
const QByteArray SharewareSha256("bb449c7480e9a02a62012d041406e8e43daa51caa0650646d1307d8650b8f837");
const QByteArray SharewareArchiveSha256("298c1e4bba353bb18447b69713a8e6d84888c52cf16d2e8b16d67a98618e407d");
const QUrl SharewareUrl("https://www.libsdl.org/projects/doom/data/doom1.wad.gz");

bool validShareware(const QString &path) {
    if (QFileInfo(path).isSymLink()) return false;
    QFile file(path);
    return file.size() == SharewareBytes && file.open(QIODevice::ReadOnly) &&
           QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex() == SharewareSha256;
}

class DoomCanvas final : public QWidget {
  public:
    QImage frame;
    QString message = "Starting the Doom engine…";
    std::function<void(bool, int)> key;
    std::function<void(bool)> focus;
    explicit DoomCanvas(QWidget *parent) : QWidget(parent) {
        setObjectName("doomCanvas");
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(320, 240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void releaseKeys() {
        for (int value : held_) if (key) key(false, value);
        held_.clear();
    }
  protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            const bool hostShortcut = (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers().testFlag(Qt::ControlModifier)) ||
                                      (keyEvent->key() == Qt::Key_F4 && keyEvent->modifiers().testFlag(Qt::AltModifier));
            if (!hostShortcut && mapKey(keyEvent->key()) >= 0) {
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Tab) {
            keyPressEvent(static_cast<QKeyEvent *>(event));
            return true;
        }
        return QWidget::event(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#080c13"));
        if (!frame.isNull()) {
            QSize size = QSize(4, 3).scaled(this->size(), Qt::KeepAspectRatio);
            p.setRenderHint(QPainter::SmoothPixmapTransform, false);
            p.drawImage(QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), frame);
        }
        if (!message.isEmpty()) {
            p.fillRect(rect(), QColor(7, 12, 20, frame.isNull() ? 255 : 180));
            p.setPen(QColor("#e8eef6"));
            p.drawText(rect().adjusted(24, 24, -24, -24), Qt::AlignCenter | Qt::TextWordWrap, message);
        }
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->isAutoRepeat()) { event->accept(); return; }
        const int value = mapKey(event->key());
        if (value >= 0) {
            held_.insert(value);
            if (key) key(true, value);
            event->accept();
        } else QWidget::keyPressEvent(event);
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (event->isAutoRepeat()) { event->accept(); return; }
        const int value = mapKey(event->key());
        if (held_.remove(value) && key) key(false, value);
        event->accept();
    }
    void focusOutEvent(QFocusEvent *event) override {
        releaseKeys();
        if (focus) focus(false);
        QWidget::focusOutEvent(event);
    }
    void focusInEvent(QFocusEvent *event) override {
        if (focus) focus(true);
        QWidget::focusInEvent(event);
    }
    void hideEvent(QHideEvent *event) override {
        releaseKeys();
        if (focus) focus(false);
        QWidget::hideEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus(Qt::MouseFocusReason);
        QWidget::mousePressEvent(event);
    }
  private:
    QSet<int> held_;
    static int mapKey(int key) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) return key - Qt::Key_A + 'a';
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return key - Qt::Key_0 + '0';
        if (key >= Qt::Key_F1 && key <= Qt::Key_F10) return DOOM_KEY_F1 + key - Qt::Key_F1;
        if (key == Qt::Key_F11) return DOOM_KEY_F11;
        if (key == Qt::Key_F12) return DOOM_KEY_F12;
        switch (key) {
        case Qt::Key_Up: return DOOM_KEY_UP_ARROW;
        case Qt::Key_Down: return DOOM_KEY_DOWN_ARROW;
        case Qt::Key_Left: return DOOM_KEY_LEFT_ARROW;
        case Qt::Key_Right: return DOOM_KEY_RIGHT_ARROW;
        case Qt::Key_Return: case Qt::Key_Enter: return DOOM_KEY_ENTER;
        case Qt::Key_Escape: return DOOM_KEY_ESCAPE;
        case Qt::Key_Control: return DOOM_KEY_CTRL;
        case Qt::Key_Shift: return DOOM_KEY_SHIFT;
        case Qt::Key_Alt: return DOOM_KEY_ALT;
        case Qt::Key_Space: return DOOM_KEY_SPACE;
        case Qt::Key_Tab: return DOOM_KEY_TAB;
        case Qt::Key_Backspace: return DOOM_KEY_BACKSPACE;
        case Qt::Key_Minus: return DOOM_KEY_MINUS;
        case Qt::Key_Equal: return DOOM_KEY_EQUALS;
        default: return -1;
        }
    }
};

class DoomApp final : public QWidget {
  public:
    explicit DoomApp(SimSystem *system, QWidget *parent) : QWidget(parent), system_(system) {
        setObjectName("doomApp");
        setProperty("doomFrames", 0);
        setProperty("doomRunning", false);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 12, 12, 12);
        auto *title = new QLabel("DOOM  /  native engine", this);
        title->setStyleSheet("font-weight: bold; font-size: 17px;");
        layout->addWidget(title);
        source_ = new QLabel(this);
        source_->setWordWrap(true);
        layout->addWidget(source_);
        auto *toolbar = new QHBoxLayout;
        auto *import = new QPushButton("Open Doom WAD…", this);
        import->setObjectName("doomImport");
        auto *freedoom = new QPushButton("Freedoom", this);
        freedoom->setObjectName("doomFreedoom");
        shareware_ = new QPushButton("Get Doom shareware", this);
        shareware_->setObjectName("doomShareware");
        auto *restart = new QPushButton("New game", this);
        restart->setObjectName("doomRestart");
        pause_ = new QPushButton("Pause", this);
        pause_->setObjectName("doomPause");
        for (auto *button : {import, shareware_, freedoom, restart, pause_}) toolbar->addWidget(button);
        layout->addLayout(toolbar);
        auto *edition = new QLabel("Freedoom is included. Doom shareware optionally downloads id Software's original Episode 1 from the SDL project (1.7 MB).", this);
        edition->setObjectName("doomEdition");
        edition->setWordWrap(true);
        edition->setStyleSheet("font-size: 12px;");
        layout->addWidget(edition);
        canvas_ = new DoomCanvas(this);
        layout->addWidget(canvas_, 1);
        auto *keys = new QLabel("WASD move · ← → turn · Ctrl fire · E / Space use · Shift run\nTab map · Esc menu · F2 save · F3 load · click the game to resume", this);
        keys->setWordWrap(true);
        keys->setStyleSheet("font-size: 12px;");
        layout->addWidget(keys);
        process_ = new QProcess(this);
        connect(process_, &QProcess::readyReadStandardOutput, this, [this] { receive(); });
        connect(process_, &QProcess::readyReadStandardError, this, [this] {
            log_.append(process_->readAllStandardError());
            if (log_.size() > 8192) log_ = log_.right(8192);
        });
        connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart) fail("Doom engine unavailable. Keep aurora86-doom beside the application.");
        });
        connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            setProperty("doomRunning", false);
            if (!stopping_) fail(status == QProcess::NormalExit && code == 0 ? "Game closed. Choose New game to play again." : "Doom stopped: " + QString::fromUtf8(log_.right(400)));
        });
        canvas_->key = [this](bool down, int key) { send(down ? 'D' : 'U', key); };
        canvas_->focus = [this](bool focused) {
            focused_ = focused;
            if (focused) manualPause_ = false;
            applyPause();
        };
        connect(pause_, &QPushButton::clicked, this, [this] {
            manualPause_ = !manualPause_;
            if (!manualPause_) canvas_->setFocus();
            applyPause();
        });
        connect(restart, &QPushButton::clicked, this, [this] { start(wad_); });
        connect(freedoom, &QPushButton::clicked, this, [this] {
            const QString path = bundledWad();
            system_->setSetting("doom/wad", path);
            start(path);
        });
        connect(shareware_, &QPushButton::clicked, this, [this] { getShareware(); });
        connect(system_, &SimSystem::servicesChanged, this, [this] {
            if (sharewareReply_ && !networkRunning()) {
                downloadError_ = "Download stopped: start the network service in System Center to continue.";
                sharewareReply_->abort();
            }
        });
        connect(import, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, "Open your Doom / Doom II IWAD", {}, "Doom IWAD (*.wad *.WAD)");
            if (path.isEmpty()) return;
            const auto info = inspectDoomWad(path);
            if (!info.valid()) { source_->setText(info.error); return; }
            system_->setSetting("doom/wad", path);
            start(path);
        });
        QTimer::singleShot(0, this, [this] {
            QString path = system_->setting("doom/wad").toString();
            if (path.isEmpty() && validShareware(dataRoot() + "/doom1.wad")) path = dataRoot() + "/doom1.wad";
            if (path.isEmpty() || !QFileInfo::exists(path)) path = bundledWad();
            start(path);
        });
    }
    ~DoomApp() override {
        if (sharewareReply_) {
            disconnect(sharewareReply_, nullptr, this, nullptr);
            sharewareReply_->abort();
        }
        stop();
    }
  private:
    SimSystem *system_;
    DoomCanvas *canvas_;
    QLabel *source_;
    QPushButton *pause_, *shareware_;
    QProcess *process_;
    QByteArray buffer_, log_;
    QString wad_;
    bool stopping_ = false, manualPause_ = false, focused_ = true;
    QNetworkAccessManager *network_ = nullptr;
    QNetworkReply *sharewareReply_ = nullptr;
    QByteArray downloadData_;
    QString downloadError_;
#if AURORA_WITH_MULTIMEDIA
    QAudioSink *audio_ = nullptr;
    QIODevice *audioDevice_ = nullptr;
#endif
    QString dataRoot() const {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/doom";
    }
    bool networkRunning() const {
        for (const auto &service : system_->services())
            if (service.name == "network") return service.running;
        return false;
    }
    void getShareware() {
        if (sharewareReply_) {
            downloadError_ = "Download cancelled. Freedoom remains available offline.";
            sharewareReply_->abort();
            return;
        }
        const QString destination = dataRoot() + "/doom1.wad";
        if (QFileInfo(destination).isSymLink()) {
            source_->setText("Cannot use a linked shareware file. Choose Open Doom WAD to select your own copy.");
            return;
        }
        if (validShareware(destination)) {
            system_->setSetting("doom/wad", destination);
            start(destination);
            return;
        }
        if (!networkRunning()) {
            source_->setText("Network offline. Start the network service in System Center or run: systemctl start network");
            return;
        }
#if !AURORA_WITH_ZLIB
        source_->setText("Download the shareware from SDL, extract doom1.wad, then choose Open Doom WAD.");
        QDesktopServices::openUrl(QUrl("https://www.libsdl.org/projects/doom/"));
#else
        if (!network_) network_ = new QNetworkAccessManager(this);
        QNetworkRequest request(SharewareUrl);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
        request.setTransferTimeout(30000);
        sharewareReply_ = network_->get(request);
        sharewareReply_->setReadBufferSize(2 * 1024 * 1024);
        downloadData_.clear();
        downloadError_.clear();
        shareware_->setText("Cancel download");
        source_->setText("Downloading original Doom shareware Episode 1 from the SDL project…");
        auto *reply = sharewareReply_;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
            downloadData_ += reply->readAll();
            if (downloadData_.size() > 2 * 1024 * 1024) {
                downloadError_ = "The shareware download exceeded its expected size.";
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::downloadProgress, this, [this, reply](qint64 received, qint64 total) {
            if (total > 2 * 1024 * 1024 || received > 2 * 1024 * 1024) {
                downloadError_ = "The shareware download exceeded its expected size.";
                reply->abort();
            } else if (total > 0) shareware_->setText(QString("Cancel (%1%)").arg(received * 100 / total));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, destination] {
            sharewareReply_ = nullptr;
            shareware_->setText("Get Doom shareware");
            const auto error = reply->error();
            const QString errorText = reply->errorString();
            reply->deleteLater();
            if (error != QNetworkReply::NoError || !downloadError_.isEmpty()) {
                source_->setText(downloadError_.isEmpty() ? "Shareware download failed: " + errorText : downloadError_);
                downloadData_.clear();
                return;
            }
            if (downloadData_.size() != 1765820 || QCryptographicHash::hash(downloadData_, QCryptographicHash::Sha256).toHex() != SharewareArchiveSha256) {
                source_->setText("Shareware verification failed. Use Open Doom WAD to import your own copy.");
                downloadData_.clear();
                return;
            }
            QByteArray wad(SharewareBytes, '\0');
            z_stream stream{};
            stream.next_in = reinterpret_cast<Bytef *>(downloadData_.data());
            stream.avail_in = uInt(downloadData_.size());
            stream.next_out = reinterpret_cast<Bytef *>(wad.data());
            stream.avail_out = uInt(wad.size());
            bool unpacked = false;
            if (inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK) {
                unpacked = inflate(&stream, Z_FINISH) == Z_STREAM_END && stream.total_out == SharewareBytes;
                inflateEnd(&stream);
            }
            downloadData_.clear();
            if (!unpacked || QCryptographicHash::hash(wad, QCryptographicHash::Sha256).toHex() != SharewareSha256) {
                source_->setText("Shareware extraction failed verification.");
                return;
            }
            QDir().mkpath(dataRoot());
            QSaveFile output(destination);
            if (!output.open(QIODevice::WriteOnly) || output.write(wad) != wad.size() || !output.commit()) {
                source_->setText("Could not save the shareware. Check free space and try again.");
                return;
            }
            if (!inspectDoomWad(destination).valid()) {
                source_->setText("The downloaded shareware is not a compatible Doom IWAD.");
                return;
            }
            system_->setSetting("doom/wad", destination);
            shareware_->setText("Play Doom shareware");
            start(destination);
        });
#endif
    }
    QString bundledWad() {
        const QString destination = dataRoot() + "/freedoom1.wad";
        if (QFileInfo(destination).isSymLink()) return {};
        QFile resource(":/doom/freedoom1.wad");
        if (!resource.open(QIODevice::ReadOnly)) return {};
        QFile cached(destination);
        if (cached.size() == resource.size() && cached.open(QIODevice::ReadOnly)) {
            QCryptographicHash digest(QCryptographicHash::Sha256);
            if (digest.addData(&cached) && digest.result().toHex() == FreedoomSha256) return destination;
        }
        cached.close();
        const QByteArray data = resource.readAll();
        if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != FreedoomSha256) return {};
        QDir().mkpath(dataRoot());
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) return {};
        return destination;
    }
    void fail(const QString &text) {
        setProperty("doomError", text);
        canvas_->message = text;
        canvas_->update();
    }
    void send(char kind, int value = 0) {
        if (process_->state() == QProcess::Running) {
            const char command[2] = {kind, char(value)};
            process_->write(command, 2);
        }
    }
    void stop() {
        stopping_ = true;
        canvas_->releaseKeys();
        send('Q');
        process_->closeWriteChannel();
        if (process_->state() != QProcess::NotRunning && !process_->waitForFinished(700)) {
            process_->terminate();
            if (!process_->waitForFinished(300)) { process_->kill(); process_->waitForFinished(300); }
        }
#if AURORA_WITH_MULTIMEDIA
        delete audio_;
        audio_ = nullptr;
        audioDevice_ = nullptr;
#endif
        setProperty("doomRunning", false);
        stopping_ = false;
    }
    void start(QString path) {
        if (path == dataRoot() + "/freedoom1.wad") path = bundledWad();
        const auto info = inspectDoomWad(path);
        if (!info.valid()) { fail(path.isEmpty() ? "Cannot prepare the bundled Freedoom data." : info.error); return; }
        stop();
        wad_ = path;
        buffer_.clear(); log_.clear();
        setProperty("doomError", QString());
        setProperty("doomFrames", 0);
        manualPause_ = false;
        canvas_->message = "Starting the Doom engine…";
        canvas_->frame = {};
        canvas_->update();
        const bool free = QFileInfo(path).fileName() == "freedoom1.wad" && path.startsWith(dataRoot());
        const bool shareware = path == dataRoot() + "/doom1.wad" && validShareware(path);
        source_->setText(free ? "Freedoom: Phase 1 · free game data · original Doom engine" : shareware ? "Doom shareware · Episode 1 · id Software · original Doom engine" : QFileInfo(path).fileName() + " · your IWAD · original Doom engine");
        if (!sharewareReply_) shareware_->setText(validShareware(dataRoot() + "/doom1.wad") ? "Play Doom shareware" : "Get Doom shareware");
        const QString saveKey = QString::fromLatin1(QCryptographicHash::hash(QFileInfo(path).absoluteFilePath().toUtf8(), QCryptographicHash::Sha256).toHex().first(16));
        const QString saves = dataRoot() + "/saves/" + saveKey;
        QDir().mkpath(saves);
        process_->setWorkingDirectory(saves);
        QString helper = QCoreApplication::applicationDirPath() + "/aurora86-doom";
#ifdef Q_OS_WIN
        helper += ".exe";
#endif
        process_->start(helper, {QFileInfo(path).absoluteFilePath(), info.mode});
        canvas_->setFocus();
        QTimer::singleShot(100, this, [this] { canvas_->setFocus(); });
#if AURORA_WITH_MULTIMEDIA
        if (!qEnvironmentVariableIsSet("AURORA_NO_AUDIO") && QGuiApplication::platformName() != "offscreen") {
            QAudioFormat format;
            format.setSampleRate(11025);
            format.setChannelCount(2);
            format.setSampleFormat(QAudioFormat::Int16);
            const auto device = QMediaDevices::defaultAudioOutput();
            if (!device.isNull() && device.isFormatSupported(format)) {
                audio_ = new QAudioSink(device, format, this);
                audio_->setVolume(.4);
                audioDevice_ = audio_->start();
            }
        }
#endif
    }
    void applyPause() {
        const bool paused = manualPause_ || !focused_;
        if (paused) canvas_->releaseKeys();
        send('P', paused);
        pause_->setText(manualPause_ ? "Resume" : "Pause");
        canvas_->message = paused ? "Paused · click the game to resume" : QString();
        canvas_->update();
    }
    void receive() {
        buffer_.append(process_->readAllStandardOutput());
        while (buffer_.size() >= 8) {
            const quint32 size = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(buffer_.constData() + 4));
            if (size > 320 * 200 * 4) { stop(); fail("Invalid Doom engine frame."); return; }
            if (buffer_.size() < qsizetype(size) + 8) break;
            const QByteArray type = buffer_.first(4);
            if (type == "FRAM" && size == 320 * 200 * 4) {
                canvas_->frame = QImage(reinterpret_cast<const uchar *>(buffer_.constData() + 8), 320, 200, QImage::Format_RGBA8888).copy();
                setProperty("doomFrames", property("doomFrames").toInt() + 1);
                setProperty("doomRunning", true);
                canvas_->message = manualPause_ || !focused_ ? "Paused · click the game to resume" : QString();
            }
#if AURORA_WITH_MULTIMEDIA
            else if (type == "AUDI" && size == 2048 && audioDevice_ && audio_ && system_->setting("sound", true).toBool() && audio_->bytesFree() >= size)
                audioDevice_->write(buffer_.constData() + 8, size);
#endif
            buffer_.remove(0, size + 8);
        }
        canvas_->update();
    }
};
} // namespace

QWidget *createDoom(SimSystem *system, QWidget *parent) { return new DoomApp(system, parent); }
