#pragma once
#include "audio.h"
#include "system.h"
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QStackedWidget>
#include <QTimer>
class LoginScreen;
class QScrollArea;
class Desktop : public QMainWindow {
    Q_OBJECT
  public:
    Desktop(SimSystem *, bool skipBoot = false, QWidget *parent = nullptr, bool bypassLogin = false);
    void launch(const QString &app, const QString &path = {});
    void toggleFullscreen();
    void screenshot(const QString &path);
    QString captureVirtualScreen();
    void lockSession();
    bool isSessionLocked() const { return locked_; }

  protected:
    void closeEvent(QCloseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

  private:
    SimSystem *system_;
    Speaker *speaker_;
    QMdiArea *area_ = nullptr;
    QStackedWidget *screens_ = nullptr;
    QScrollArea *display_ = nullptr;
    LoginScreen *login_ = nullptr;
    QTimer *resolutionTimer_ = nullptr;
    QString activeDisplayPreset_;
    bool bypassLogin_ = false, locked_ = true;
    QLabel *clock_ = nullptr, *meter_ = nullptr;
    QWidget *boot_ = nullptr;
    QTimer *bootTimer_ = nullptr;
    QString activeTheme_;
    bool activeScanlines_ = true;
    int activeFontSize_ = 16;
    QString activeWallpaperPath_, activeWallpaperMode_;
    bool activeWallpaperRetro_ = false;
    QByteArray wallpaperBytes_;
    QImage wallpaperImage_;
    int bootStep_ = 0;
    bool started_ = false;
    void startDesktop();
    void finishBoot();
    void showLogin();
    void applyDisplayMode();
    void recordResolution();
    void applyTheme();
    void beginBoot();
    void updateMeters();
    void wallpaper();
};
