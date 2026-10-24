#include "login.h"
#include "system.h"
#include <QCryptographicHash>
#include <QImageReader>
#include <QPasswordDigestor>
#include <QRandomGenerator>
#include <QtWidgets>

namespace {
constexpr int Iterations = 120000;
constexpr auto DefaultMessage = "A small workstation. A whole world.";
const QString AvatarPath = "/home/guest/images/avatar.png";
const QString BackgroundPath = "/home/guest/images/login-background.png";

bool report(QString *error, const QString &message) {
    if (error)
        *error = message;
    return false;
}
bool putSetting(SimSystem *system, const QString &key, const QVariant &value, QString *error = nullptr) {
    if (system->setSettings({{key, value}}))
        return true;
    return report(error, system->lastError().isEmpty()
                             ? "Could not save this preference. Check the virtual disk state."
                             : system->lastError());
}
bool applySettings(SimSystem *system, const QVariantMap &values, QString *error = nullptr) {
    if (system->setSettings(values))
        return true;
    return report(error, system->lastError().isEmpty()
                             ? "Could not save this identity. Check the virtual disk state."
                             : system->lastError());
}
QLabel *plainLabel(const QString &text = {}) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
QImage virtualImage(SimSystem *system, const QString &path) {
    if (path.isEmpty())
        return {};
    QByteArray data = system->read(system->normalize(path));
    if (!system->lastError().isEmpty())
        return {};
    QBuffer source(&data);
    source.open(QIODevice::ReadOnly);
    QImageReader reader(&source);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096)
        return {};
    if (size.width() > 960 || size.height() > 640)
        reader.setScaledSize(size.scaled(960, 640, Qt::KeepAspectRatio));
    return reader.read();
}
QPixmap avatarPixmap(SimSystem *system, int size) {
    QImage image = virtualImage(system, system->setting("avatarPath").toString());
    if (!image.isNull()) {
        image = image.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        return QPixmap::fromImage(
            image.copy((image.width() - size) / 2, (image.height() - size) / 2, size, size));
    }
    QPixmap picture(size, size);
    picture.fill(QColor("#203d35"));
    QPainter painter(&picture);
    painter.setPen(QPen(QColor("#b6cc95"), 2));
    painter.drawRect(1, 1, size - 3, size - 3);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#b6cc95"));
    painter.drawRect(size * 3 / 8, size / 5, size / 4, size / 4);
    painter.drawRect(size / 4, size / 2, size / 2, size / 3);
    return picture;
}
QDateTime simulatedTime(SimSystem *system) {
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime vintage = now;
    vintage.setDate(QDate(1986, now.date().month(),
                          qMin(now.date().day(), QDate(1986, now.date().month(), 1).daysInMonth())));
    return now.addSecs(system->setting("clockOffsetSeconds", now.secsTo(vintage)).toLongLong());
}
} // namespace

QString virtualUserName(SimSystem *system) {
    const QString name = system->setting("username", "guest").toString();
    return QRegularExpression("^[a-z][a-z0-9_-]{0,15}$").match(name).hasMatch() ? name : "guest";
}
bool hasLoginPassword(SimSystem *system) {
    const QVariant stored = system->setting("loginCredentials");
    if (!stored.isValid() || stored.isNull())
        return false;
    return stored.metaType().id() != QMetaType::QVariantMap || !stored.toMap().isEmpty();
}
bool verifyLoginPassword(SimSystem *system, const QString &password) {
    if (password.size() > 128)
        return false;
    const QVariant stored = system->setting("loginCredentials");
    if (stored.isValid() && !stored.isNull() && stored.metaType().id() != QMetaType::QVariantMap)
        return false;
    const QVariantMap record = stored.toMap();
    if (record.isEmpty())
        return password.isEmpty();
    const QString saltText = record.value("salt").toString(), hashText = record.value("hash").toString();
    if (record.value("scheme").toString() != "PBKDF2-SHA256" ||
        record.value("iterations").toInt() != Iterations ||
        !QRegularExpression("^[0-9a-f]{32}$").match(saltText).hasMatch() ||
        !QRegularExpression("^[0-9a-f]{64}$").match(hashText).hasMatch())
        return false;
    const QByteArray expected = QByteArray::fromHex(hashText.toLatin1());
    const QByteArray actual =
        QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, password.toUtf8(),
                                           QByteArray::fromHex(saltText.toLatin1()), Iterations, 32);
    if (actual.size() != expected.size())
        return false;
    unsigned char difference = 0;
    for (qsizetype i = 0; i < actual.size(); ++i)
        difference |= static_cast<unsigned char>(actual[i]) ^ static_cast<unsigned char>(expected[i]);
    return difference == 0;
}
bool setLoginPassword(SimSystem *system, const QString &password, QString *error) {
    if (error)
        error->clear();
    if (password.size() > 128)
        return report(error, "Use at most 128 characters for the simulator password.");
    QVariantMap record;
    if (!password.isEmpty()) {
        QByteArray salt(16, Qt::Uninitialized);
        for (char &byte : salt)
            byte = static_cast<char>(QRandomGenerator::system()->bounded(256));
        const QByteArray hash = QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256,
                                                                   password.toUtf8(), salt, Iterations, 32);
        record = {{"scheme", "PBKDF2-SHA256"},
                  {"iterations", Iterations},
                  {"salt", QString::fromLatin1(salt.toHex())},
                  {"hash", QString::fromLatin1(hash.toHex())}};
    }
    return putSetting(system, "loginCredentials", record, error);
}
bool changeLoginPassword(SimSystem *system, const QString &current, const QString &replacement,
                         QString *error) {
    if (!verifyLoginPassword(system, current))
        return report(error, "The current simulator password is incorrect.");
    return setLoginPassword(system, replacement, error);
}
bool importIdentityImage(SimSystem *system, const QString &localPath, IdentityImage kind, QString *error) {
    if (error)
        error->clear();
    QFile source(localPath);
    if (!source.open(QIODevice::ReadOnly))
        return report(error, "Could not open the selected image.");
    if (source.size() > 64 * 1024 * 1024)
        return report(error, "Image files must be smaller than 64 MiB.");
    QImageReader reader(&source);
    reader.setAutoTransform(true);
    const QByteArray format = reader.format().toLower();
    const QSize dimensions = reader.size();
    if (!QList<QByteArray>{"png", "jpeg", "jpg", "bmp", "webp", "ppm"}.contains(format) ||
        !dimensions.isValid())
        return report(error, "Choose a valid PNG, JPEG, BMP, WebP or PPM image.");
    if (dimensions.width() > 32768 || dimensions.height() > 32768 ||
        qint64(dimensions.width()) * dimensions.height() > 64000000)
        return report(error, "This image is too large. Reduce its resolution before importing.");
    const bool avatar = kind == IdentityImage::Avatar;
    const QSize bound = avatar ? QSize(256, 256) : QSize(960, 640);
    if (dimensions.width() > bound.width() || dimensions.height() > bound.height())
        reader.setScaledSize(dimensions.scaled(bound, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull())
        return report(error, "Could not decode this image within the memory limit.");
    if (avatar) {
        image = image.scaled(128, 128, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        image = image.copy((image.width() - 128) / 2, (image.height() - 128) / 2, 128, 128);
    } else if (image.width() > 960 || image.height() > 640)
        image = image.scaled(960, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return report(error, "Could not prepare the image for the virtual disk.");
    const QString target = avatar ? AvatarPath : BackgroundPath;
    const QString key = avatar ? "avatarPath" : "loginWallpaperPath";
    const QVariant previous = system->setting(key, "");
    if (!putSetting(system, key, target, error))
        return false;
    if (!system->write(target, png)) {
        const QString message = system->lastError();
        system->setSetting(key, previous);
        return report(error, message.isEmpty() ? "Could not save the image on the virtual disk." : message);
    }
    return true;
}

LoginScreen::LoginScreen(SimSystem *system, QWidget *parent) : QWidget(parent), system_(system) {
    setObjectName("auroraSYNC");
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(32, 24, 32, 24);
    auto *top = new QHBoxLayout;
    auto *identity = plainLabel("AURORA/86 / auroraSYNC");
    identity->setWordWrap(false);
    identity->setStyleSheet("color:#e5e6c8;background:transparent");
    top->addWidget(identity, 1);
    clock_ = plainLabel();
    clock_->setWordWrap(false);
    clock_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    clock_->setStyleSheet("color:#e5e6c8;background:transparent");
    top->addWidget(clock_);
    outer->addLayout(top);
    auto *center = new QScrollArea;
    center->setFrameShape(QFrame::NoFrame);
    center->setWidgetResizable(true);
    center->setStyleSheet("QScrollArea{background:transparent;border:0}");
    center->viewport()->setAutoFillBackground(false);
    auto *centerContent = new QWidget;
    centerContent->setAutoFillBackground(false);
    auto *centerLayout = new QVBoxLayout(centerContent);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addStretch();
    center->setWidget(centerContent);
    centerContent->setAutoFillBackground(false);
    center->viewport()->setAutoFillBackground(false);
    outer->addWidget(center, 1);
    auto *card = new QFrame;
    card->setObjectName("loginCard");
    card->setMinimumWidth(500);
    card->setMaximumWidth(510);
    card->setStyleSheet("QFrame#loginCard{background:#e5e6c8;border:2px solid #8fac74}"
                        "QFrame#loginCard QLabel{color:#172b2a;background:transparent}");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);
    auto *brand = plainLabel("auroraSYNC");
    brand->setAlignment(Qt::AlignCenter);
    brand->setStyleSheet("font-size:28px;font-weight:bold;color:#172b2a;background:transparent");
    layout->addWidget(brand);
    auto *machine = plainLabel("A16 / 8 MHz / tty00 / local session");
    machine->setAlignment(Qt::AlignCenter);
    layout->addWidget(machine);
    auto *person = new QGridLayout;
    avatar_ = new QLabel;
    avatar_->setObjectName("loginAvatar");
    avatar_->setFixedSize(84, 84);
    person->addWidget(avatar_, 0, 0, 3, 1, Qt::AlignVCenter);
    name_ = plainLabel();
    name_->setObjectName("loginDisplayName");
    name_->setStyleSheet("font-size:20px;font-weight:bold;color:#172b2a;background:transparent");
    user_ = plainLabel();
    user_->setWordWrap(false);
    user_->setObjectName("loginUser");
    bio_ = plainLabel();
    person->addWidget(name_, 0, 1);
    person->addWidget(user_, 1, 1);
    person->addWidget(bio_, 2, 1);
    person->setColumnStretch(1, 1);
    layout->addLayout(person);
    message_ = plainLabel();
    message_->setAlignment(Qt::AlignCenter);
    layout->addWidget(message_);
    passwordLabel_ = plainLabel("Password:");
    layout->addWidget(passwordLabel_);
    password_ = new QLineEdit;
    password_->setObjectName("loginPassword");
    password_->setEchoMode(QLineEdit::Password);
    password_->setMaxLength(128);
    password_->setPlaceholderText("Enter your simulator password");
    layout->addWidget(password_);
    error_ = plainLabel();
    error_->setObjectName("loginError");
    error_->setStyleSheet("color:#8b3429;background:transparent");
    error_->setMinimumHeight(24);
    layout->addWidget(error_);
    auto *buttons = new QHBoxLayout;
    auto *off = new QPushButton("Shut down");
    off->setObjectName("loginShutdown");
    buttons->addWidget(off);
    submit_ = new QPushButton("Sign in");
    submit_->setObjectName("loginSubmit");
    submit_->setDefault(true);
    buttons->addWidget(submit_);
    layout->addLayout(buttons);
    centerLayout->addWidget(card, 0, Qt::AlignHCenter);
    centerLayout->addStretch();
    auto *notice = plainLabel("Local identity / virtual disk / no network account required");
    notice->setAlignment(Qt::AlignCenter);
    notice->setStyleSheet("color:#e5e6c8;background:transparent");
    outer->addWidget(notice);
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &LoginScreen::updateClock);
    connect(submit_, &QPushButton::clicked, this, &LoginScreen::signIn);
    connect(password_, &QLineEdit::returnPressed, this, &LoginScreen::signIn);
    connect(off, &QPushButton::clicked, this, &LoginScreen::shutdownRequested);
    connect(system_, &SimSystem::settingsChanged, this, &LoginScreen::refresh);
    connect(system_, &SimSystem::changed, this, &LoginScreen::refresh);
    refresh();
    setFocusProxy(password_);
}
void LoginScreen::refresh() {
    name_->setText(system_->setting("displayName", "Guest").toString().left(40));
    user_->setText("login: " + virtualUserName(system_));
    bio_->setText(system_->setting("profileBio").toString().left(160));
    bio_->setVisible(!bio_->text().isEmpty());
    message_->setText(system_->setting("loginMessage", DefaultMessage).toString().left(160));
    name_->setMinimumHeight(qMax(0, name_->heightForWidth(330)));
    bio_->setMinimumHeight(qMax(0, bio_->heightForWidth(330)));
    message_->setMinimumHeight(qMax(0, message_->heightForWidth(440)));
    avatar_->setPixmap(avatarPixmap(system_, avatar_->width()));
    background_ = virtualImage(system_, system_->setting("loginWallpaperPath").toString());
    const bool required = hasLoginPassword(system_);
    passwordLabel_->setVisible(required);
    password_->setVisible(required);
    if (!required)
        password_->clear();
    updateClock();
    update();
}
void LoginScreen::signIn() {
    if (!verifyLoginPassword(system_, password_->text())) {
        error_->setText("Incorrect password. Try again.");
        password_->clear();
        password_->setFocus();
        return;
    }
    error_->clear();
    password_->clear();
    emit authenticated();
}
void LoginScreen::updateClock() { clock_->setText(simulatedTime(system_).toString("dd MMM yyyy  HH:mm:ss")); }
void LoginScreen::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#172b2a"));
    if (!background_.isNull()) {
        const QImage scaled =
            background_.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawImage((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
        painter.fillRect(rect(), QColor(0, 0, 0, 65));
    } else {
        painter.setPen(QColor("#254438"));
        for (int y = 0; y < height(); y += 24)
            painter.drawLine(0, y, width(), y);
        for (int x = 0; x < width(); x += 24)
            painter.drawLine(x, 0, x, height());
    }
}
void LoginScreen::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    refresh();
    error_->clear();
    password_->clear();
    timer_->start();
    if (hasLoginPassword(system_))
        password_->setFocus();
    else
        submit_->setFocus();
}
void LoginScreen::hideEvent(QHideEvent *event) {
    timer_->stop();
    password_->clear();
    QWidget::hideEvent(event);
}

QWidget *createIdentityPanel(SimSystem *system, QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName("identityPanel");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *intro = plainLabel("auroraSYNC / personalize your local simulator identity and sign-in screen.");
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *name = new QLineEdit(system->setting("displayName", "Guest").toString());
    name->setObjectName("identityDisplayName");
    name->setMaxLength(40);
    auto *user = new QLineEdit(virtualUserName(system));
    user->setObjectName("identityUsername");
    user->setMaxLength(16);
    auto *bio = new QLineEdit(system->setting("profileBio").toString());
    bio->setMaxLength(160);
    auto *message = new QLineEdit(system->setting("loginMessage", DefaultMessage).toString());
    message->setMaxLength(160);
    auto *enabled = new QCheckBox("Show auroraSYNC before opening the desktop");
    enabled->setObjectName("loginEnabled");
    enabled->setChecked(system->setting("loginEnabled", true).toBool());
    form->addRow("Display name", name);
    form->addRow("Unix login", user);
    form->addRow("Profile note", bio);
    form->addRow("Welcome message", message);
    form->addRow("Startup", enabled);
    layout->addLayout(form);
    auto *status = plainLabel();
    status->setObjectName("identityStatus");
    auto *save = new QPushButton("Save identity");
    save->setObjectName("identitySave");
    layout->addWidget(save);
    QObject::connect(save, &QPushButton::clicked, content, [=] {
        if (name->text().trimmed().isEmpty()) {
            status->setText("Enter a display name.");
            return;
        }
        if (!QRegularExpression("^[a-z][a-z0-9_-]{0,15}$").match(user->text()).hasMatch()) {
            status->setText("Use 1–16 lowercase letters, numbers, _ or -; start with a letter.");
            return;
        }
        QString error;
        if (!applySettings(system,
                           {{"displayName", name->text().trimmed()},
                            {"username", user->text()},
                            {"profileBio", bio->text()},
                            {"loginMessage", message->text()},
                            {"loginEnabled", enabled->isChecked()}},
                           &error))
            status->setText(error);
        else
            status->setText("Identity saved. The virtual home remains /home/guest.");
    });
    auto *photos = new QHBoxLayout;
    auto *avatar = new QLabel;
    avatar->setFixedSize(64, 64);
    auto updateAvatar = [system, avatar] { avatar->setPixmap(avatarPixmap(system, 64)); };
    updateAvatar();
    photos->addWidget(avatar);
    auto addImport = [&](const QString &title, IdentityImage kind, const char *id) {
        auto *button = new QPushButton(title);
        button->setObjectName(id);
        photos->addWidget(button);
        QObject::connect(button, &QPushButton::clicked, content, [=] {
            const QString path = QFileDialog::getOpenFileName(
                content, "Import an identity image", {}, "Images (*.png *.jpg *.jpeg *.bmp *.webp *.ppm)");
            if (path.isEmpty())
                return;
            QString error;
            if (!importIdentityImage(system, path, kind, &error))
                status->setText(error);
            else
                status->setText("Image copied to the virtual disk. Your original file was not changed.");
        });
    };
    addImport("Choose profile photo…", IdentityImage::Avatar, "identityImportAvatar");
    addImport("Choose login background…", IdentityImage::LoginBackground, "identityImportBackground");
    layout->addLayout(photos);
    QObject::connect(system, &SimSystem::settingsChanged, content, updateAvatar);
    QObject::connect(system, &SimSystem::changed, content, updateAvatar);
    auto *reset = new QPushButton("Reset login appearance");
    reset->setObjectName("identityReset");
    layout->addWidget(reset);
    QObject::connect(reset, &QPushButton::clicked, content, [=] {
        QString error;
        if (!applySettings(system,
                           {{"displayName", "Guest"},
                            {"username", "guest"},
                            {"profileBio", ""},
                            {"loginMessage", DefaultMessage},
                            {"avatarPath", ""},
                            {"loginWallpaperPath", ""},
                            {"loginEnabled", true}},
                           &error)) {
            status->setText(error);
            return;
        }
        name->setText("Guest");
        user->setText("guest");
        bio->clear();
        message->setText(DefaultMessage);
        enabled->setChecked(true);
        status->setText("Login appearance reset. Your password was not changed.");
    });
    auto *passwordTitle = plainLabel("Simulator password / optional");
    layout->addWidget(passwordTitle);
    auto *passwordForm = new QFormLayout;
    auto *current = new QLineEdit;
    current->setObjectName("identityCurrentPassword");
    auto *replacement = new QLineEdit;
    replacement->setObjectName("identityNewPassword");
    auto *confirm = new QLineEdit;
    confirm->setObjectName("identityConfirmPassword");
    for (auto *field : {current, replacement, confirm}) {
        field->setEchoMode(QLineEdit::Password);
        field->setMaxLength(128);
    }
    passwordForm->addRow("Current password", current);
    passwordForm->addRow("New password", replacement);
    passwordForm->addRow("Confirm new password", confirm);
    layout->addLayout(passwordForm);
    auto *passwordButtons = new QHBoxLayout;
    auto *change = new QPushButton("Set / change password");
    change->setObjectName("identityChangePassword");
    auto *remove = new QPushButton("Remove password");
    remove->setObjectName("identityRemovePassword");
    passwordButtons->addWidget(change);
    passwordButtons->addWidget(remove);
    layout->addLayout(passwordButtons);
    QObject::connect(change, &QPushButton::clicked, content, [=] {
        if (replacement->text().isEmpty()) {
            status->setText("Enter a new password, or use Remove password.");
            return;
        }
        if (replacement->text() != confirm->text()) {
            status->setText("The new passwords do not match.");
            return;
        }
        QString error;
        if (!changeLoginPassword(system, current->text(), replacement->text(), &error))
            status->setText(error);
        else {
            current->clear();
            replacement->clear();
            confirm->clear();
            status->setText("Simulator password updated.");
        }
    });
    QObject::connect(remove, &QPushButton::clicked, content, [=] {
        QString error;
        if (!changeLoginPassword(system, current->text(), {}, &error))
            status->setText(error);
        else {
            current->clear();
            replacement->clear();
            confirm->clear();
            status->setText("Password removed. Sign in without a password next time.");
        }
    });
    layout->addWidget(status);
    auto *notice =
        plainLabel("This is a local simulator login, not Linux authentication or an online account. "
                   "Your virtual state is stored in your own user directory.");
    layout->addWidget(notice);
    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}
