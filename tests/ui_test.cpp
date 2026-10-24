#include "desktop.h"
#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QFontDatabase>
#include <QImage>
#include <QMdiSubWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

class UiTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");
        QFontDatabase::addApplicationFont(":/fonts/DejaVuSansMono.ttf");
    }
    void readableFontUpdatesExistingAppsAndPersists() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        system.setSetting("fontSize", 20);
        Desktop desktop(&system, true);
        desktop.show();
        QApplication::processEvents();
        auto *initial = desktop.findChild<QMdiSubWindow *>("app_terminal");
        QVERIFY(initial);
        QVERIFY(initial->x() >= 145);
        auto *terminal = desktop.findChild<QPlainTextEdit *>("terminalOutput");
        QVERIFY(terminal);
        QTRY_COMPARE(terminal->font().pixelSize(), 20);
        QCOMPARE(terminal->font().family(), QString("DejaVu Sans Mono"));
        desktop.launch("editor");
        auto *editor = desktop.findChild<QPlainTextEdit *>("editorText");
        QVERIFY(editor);
        system.setSetting("fontSize", 14);
        QTRY_COMPARE(terminal->font().pixelSize(), 14);
        QTRY_COMPARE(editor->font().pixelSize(), 14);
        system.setSetting("fontSize", 20);
        QTRY_COMPARE(editor->font().pixelSize(), 20);
        SimSystem reopened(directory.path());
        QVERIFY(reopened.load());
        QCOMPARE(reopened.setting("fontSize").toInt(), 20);
    }
    void settingsControlsApplyFontAndRestoreWallpaper() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SimSystem system(directory.path());
        QVERIFY(system.load());
        Desktop desktop(&system, true);
        desktop.show();
        QTest::qWait(30);
        auto *area = desktop.findChild<QMdiArea *>("desktopArea");
        QVERIFY(area);
        const QImage original = area->background().textureImage();
        QVERIFY(!original.isNull());
        QImage image(80, 60, QImage::Format_RGB32);
        image.fill(QColor("#d147a3"));
        QByteArray data;
        QBuffer buffer(&data);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        const QString path = "/home/guest/images/teste-wallpaper.png";
        QVERIFY(system.write(path, data));
        system.setSetting("wallpaperPath", path);
        QTRY_VERIFY(area->background().textureImage() != original);
        desktop.launch("settings");
        auto *size = desktop.findChild<QComboBox *>("fontSize");
        QVERIFY(size);
        const int index = size->findData(18);
        QVERIFY(index >= 0);
        size->setCurrentIndex(index);
        QCOMPARE(system.setting("fontSize").toInt(), 18);
        auto *terminal = desktop.findChild<QPlainTextEdit *>("terminalOutput");
        QVERIFY(terminal);
        QTRY_COMPARE(terminal->font().pixelSize(), 18);
        // Return to the original size so the wallpaper brush uses identical text metrics.
        size->setCurrentIndex(size->findData(16));
        auto *restore = desktop.findChild<QPushButton *>("wallpaperRestore");
        QVERIFY(restore);
        restore->click();
        QCOMPARE(system.setting("wallpaperPath").toString(), QString());
        QTRY_COMPARE(area->background().textureImage(), original);
    }
};
QTEST_MAIN(UiTest)
#include "ui_test.moc"
