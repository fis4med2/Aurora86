#include "login.h"
#include "system.h"
#include <QBuffer>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

class LoginTest final : public QObject {
    Q_OBJECT
  private slots:
    void credentialsAreSaltedAndPersisted() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QVERIFY(!hasLoginPassword(&system));
        QVERIFY(verifyLoginPassword(&system, {}));
        QString error;
        QVERIFY(setLoginPassword(&system, "a correct passphrase", &error));
        QVERIFY(error.isEmpty());
        QVERIFY(hasLoginPassword(&system));
        const auto first = system.setting("loginCredentials").toMap();
        QCOMPARE(first.value("salt").toString().size(), 32);
        QCOMPARE(first.value("hash").toString().size(), 64);
        QVERIFY(!verifyLoginPassword(&system, "incorrect"));
        QVERIFY(verifyLoginPassword(&system, "a correct passphrase"));
        QVERIFY(setLoginPassword(&system, "a correct passphrase", &error));
        QVERIFY(first.value("salt") != system.setting("loginCredentials").toMap().value("salt"));
        QFile state(folder.filePath("vfs.json"));
        QVERIFY(state.open(QIODevice::ReadOnly));
        QVERIFY(!state.readAll().contains("a correct passphrase"));
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        QVERIFY(verifyLoginPassword(&restored, "a correct passphrase"));
        const auto previous = restored.setting("loginCredentials");
        QVERIFY(!changeLoginPassword(&restored, "wrong", "replacement", &error));
        QCOMPARE(restored.setting("loginCredentials"), previous);
        QVERIFY(changeLoginPassword(&restored, "a correct passphrase", "replacement", &error));
        QVERIFY(!verifyLoginPassword(&restored, "a correct passphrase"));
        QVERIFY(verifyLoginPassword(&restored, "replacement"));
        QVERIFY(!setLoginPassword(&restored, QString(129, 'x'), &error));
        QVERIFY(verifyLoginPassword(&restored, "replacement"));
        QVERIFY(changeLoginPassword(&restored, "replacement", {}, &error));
        QVERIFY(!hasLoginPassword(&restored));
        QVERIFY(verifyLoginPassword(&restored, {}));
    }
    void malformedCredentialsFailClosed() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        QVERIFY(setLoginPassword(&system, "valid"));
        auto record = system.setting("loginCredentials").toMap();
        record.insert("iterations", 2000000000);
        system.setSetting("loginCredentials", record);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!verifyLoginPassword(&system, "valid"));
        QVERIFY(timer.elapsed() < 1000);
        record.insert("iterations", 120000);
        record.insert("salt", "not-hexadecimal");
        system.setSetting("loginCredentials", record);
        QVERIFY(!verifyLoginPassword(&system, "valid"));
        system.setSetting("loginCredentials", "damaged record");
        QVERIFY(hasLoginPassword(&system));
        QVERIFY(!verifyLoginPassword(&system, {}));
    }
    void signInAcceptsOnlyCorrectCredentials() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        LoginScreen login(&system);
        login.resize(960, 680);
        login.show();
        QSignalSpy authenticated(&login, &LoginScreen::authenticated);
        auto *submit = login.findChild<QPushButton *>("loginSubmit");
        auto *password = login.findChild<QLineEdit *>("loginPassword");
        QVERIFY(submit);
        QVERIFY(password);
        submit->click();
        QCOMPARE(authenticated.count(), 1);
        QVERIFY(setLoginPassword(&system, "launch-code"));
        password->setText("wrong");
        submit->click();
        QCOMPARE(authenticated.count(), 1);
        QVERIFY(password->text().isEmpty());
        QVERIFY(!login.findChild<QLabel *>("loginError")->text().isEmpty());
        password->setText("launch-code");
        QTest::keyClick(password, Qt::Key_Return);
        QCOMPARE(authenticated.count(), 2);
        QVERIFY(password->text().isEmpty());
        system.setSetting("username", "pilot_86");
        QCOMPARE(login.findChild<QLabel *>("loginUser")->text(), QString("login: pilot_86"));
        system.setSetting("displayName", "<b>Ada</b>");
        auto *name = login.findChild<QLabel *>("loginDisplayName");
        QCOMPARE(name->textFormat(), Qt::PlainText);
        QCOMPARE(name->text(), QString("<b>Ada</b>"));
    }
    void identityPanelValidatesAndPreservesPasswordOnAppearanceReset() {
        QTemporaryDir folder;
        SimSystem system(folder.path());
        QVERIFY(system.load());
        std::unique_ptr<QWidget> panel(createIdentityPanel(&system));
        auto *name = panel->findChild<QLineEdit *>("identityDisplayName");
        auto *user = panel->findChild<QLineEdit *>("identityUsername");
        auto *save = panel->findChild<QPushButton *>("identitySave");
        QVERIFY(name && user && save);
        name->setText("Ada");
        user->setText("bad user");
        save->click();
        QCOMPARE(virtualUserName(&system), QString("guest"));
        QVERIFY(panel->findChild<QLabel *>("identityStatus")->text().contains("lowercase"));
        user->setText("pilot_86");
        panel->findChild<QCheckBox *>("loginEnabled")->setChecked(false);
        save->click();
        QCOMPARE(virtualUserName(&system), QString("pilot_86"));
        QCOMPARE(system.setting("displayName").toString(), QString("Ada"));
        QVERIFY(!system.setting("loginEnabled").toBool());
        auto *replacement = panel->findChild<QLineEdit *>("identityNewPassword");
        auto *confirm = panel->findChild<QLineEdit *>("identityConfirmPassword");
        auto *change = panel->findChild<QPushButton *>("identityChangePassword");
        replacement->setText("secret");
        confirm->setText("different");
        change->click();
        QVERIFY(!hasLoginPassword(&system));
        confirm->setText("secret");
        change->click();
        QVERIFY(verifyLoginPassword(&system, "secret"));
        panel->findChild<QPushButton *>("identityReset")->click();
        QCOMPARE(virtualUserName(&system), QString("guest"));
        QVERIFY(verifyLoginPassword(&system, "secret"));
        SimSystem restored(folder.path());
        QVERIFY(restored.load());
        QCOMPARE(restored.setting("displayName").toString(), QString("Guest"));
        QVERIFY(verifyLoginPassword(&restored, "secret"));
    }
    void imageImportsRespectQuotaAndPreserveHostFiles() {
        QTemporaryDir folder;
        SimSystem system(folder.filePath("state"));
        QVERIFY(system.load());
        // Exercise the same synchronous readers that a visible login screen uses.
        LoginScreen login(&system);
        QImage picture(400, 200, QImage::Format_RGB32);
        picture.fill(QColor("#5977a1"));
        const QString source = folder.filePath("portrait.png");
        QVERIFY(picture.save(source));
        QFile host(source);
        QVERIFY(host.open(QIODevice::ReadOnly));
        const QByteArray original = host.readAll();
        host.close();
        QString error;
        QVERIFY2(importIdentityImage(&system, source, IdentityImage::Avatar, &error), qPrintable(error));
        const QString avatar = system.setting("avatarPath").toString();
        const QByteArray originalAvatar = system.read(avatar);
        QCOMPARE(QImage::fromData(originalAvatar).size(), QSize(128, 128));
        QVERIFY(host.open(QIODevice::ReadOnly));
        QCOMPARE(host.readAll(), original);
        host.close();
        QImage background(1600, 900, QImage::Format_RGB32);
        background.fill(QColor("#142d3a"));
        const QString backdrop = folder.filePath("background.png");
        QVERIFY(background.save(backdrop));
        QVERIFY(importIdentityImage(&system, backdrop, IdentityImage::LoginBackground, &error));
        const QImage reduced = QImage::fromData(system.read(system.setting("loginWallpaperPath").toString()));
        QVERIFY(reduced.width() <= 960 && reduced.height() <= 640);
        QFile bad(folder.filePath("bad.png"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        QVERIFY(!importIdentityImage(&system, bad.fileName(), IdentityImage::Avatar, &error));
        QCOMPARE(system.read(avatar), originalAvatar);
        const QString filler = "/home/guest/filler";
        const int room = SimSystem::DiskBytes - system.usedBytes() - 64 - filler.toUtf8().size();
        QVERIFY(system.write(filler, QByteArray(room, 'x')));
        QRandomGenerator random(12345);
        for (int y = 0; y < picture.height(); ++y)
            for (int x = 0; x < picture.width(); ++x)
                picture.setPixel(x, y, random.generate() | 0xff000000);
        QVERIFY(picture.save(source));
        QVERIFY(!importIdentityImage(&system, source, IdentityImage::Avatar, &error));
        QCOMPARE(system.setting("avatarPath").toString(), avatar);
        QCOMPARE(system.read(avatar), originalAvatar);
        SimSystem restored(folder.filePath("state"));
        QVERIFY(restored.load());
        QCOMPARE(restored.read(avatar), originalAvatar);
    }
};
QTEST_MAIN(LoginTest)
#include "login_test.moc"
