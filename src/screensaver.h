#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

// Classic 1986 screen saver: a starfield with the AURORA wordmark bouncing
// across the screen. Any key, click or mouse movement dismisses it.
class ScreenSaver : public QWidget {
    Q_OBJECT
  public:
    explicit ScreenSaver(QWidget *parent = nullptr);
    void start();
    void stop();
    bool running() const { return isVisible(); }

  signals:
    void dismissed();

  protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;

  private:
    struct Star {
        double x = 0;
        double y = 0;
        double speed = 0;
        int size = 1;
    };
    QTimer m_timer;
    QElapsedTimer m_clock;
    QList<Star> m_stars;
    double m_logoX = 0.35, m_logoY = 0.4, m_logoDX = 0.0032, m_logoDY = 0.0021;
    void seedStars();
    void advance();
};
