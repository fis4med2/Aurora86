#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class SimSystem;

enum class IdentityImage { Avatar, LoginBackground };

QString virtualUserName(SimSystem *system);
bool hasLoginPassword(SimSystem *system);
bool verifyLoginPassword(SimSystem *system, const QString &password);
bool setLoginPassword(SimSystem *system, const QString &password, QString *error = nullptr);
bool changeLoginPassword(SimSystem *system, const QString &current, const QString &replacement,
                         QString *error = nullptr);
bool importIdentityImage(SimSystem *system, const QString &localPath, IdentityImage kind,
                         QString *error = nullptr);
QWidget *createIdentityPanel(SimSystem *system, QWidget *parent = nullptr);

class LoginScreen final : public QWidget {
    Q_OBJECT
  public:
    explicit LoginScreen(SimSystem *system, QWidget *parent = nullptr);

  signals:
    void authenticated();
    void shutdownRequested();

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    SimSystem *system_;
    QLabel *avatar_, *name_, *user_, *bio_, *message_, *error_, *clock_, *passwordLabel_;
    QLineEdit *password_;
    QPushButton *submit_;
    QTimer *timer_;
    QImage background_;
    void refresh();
    void signIn();
    void updateClock();
};
