#include "games.h"
#include "system.h"

#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const QColor dark("#172b2a"), cream("#efe5bd"), green("#8fac74"), amber("#d9a65c");

class Game : public QWidget {
  public:
    explicit Game(SimSystem *system, QWidget *parent) : QWidget(parent), system(system) {
        setMinimumSize(560, 420);
        resize(620, 465);
        setFocusPolicy(Qt::StrongFocus);
        auto *restart = new QPushButton("RESTART", this);
        restart->setGeometry(464, 10, 144, 36);
        restart->setFocusPolicy(Qt::NoFocus);
        restart->setStyleSheet("QPushButton {color:#172b2a;background:#efe5bd;border:2px solid "
                               "#172b2a;padding:4px;} QPushButton:pressed {background:#d9a65c;}");
        connect(restart, &QPushButton::clicked, this, [this] {
            reset();
            setFocus();
            update();
        });
        connect(&timer, &QTimer::timeout, this, [this] {
            if (isVisible())
                tick();
        });
        timer.setInterval(30);
        button = restart;
    }

  protected:
    SimSystem *system;
    QTimer timer;
    QPushButton *button;
    bool paused = false, ended = false, started = false;
    int score = 0, best = 0;
    virtual void reset() = 0;
    virtual void tick() = 0;
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        timer.start();
        setFocus();
    }
    void hideEvent(QHideEvent *event) override {
        timer.stop();
        QWidget::hideEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        QWidget::mousePressEvent(event);
    }
    void resizeEvent(QResizeEvent *event) override {
        button->setGeometry(width() - 156, 10, 144, 36);
        QWidget::resizeEvent(event);
    }
    void prepare(QPainter &p, const QString &title, const QString &instructions) {
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setRenderHint(QPainter::TextAntialiasing, true);
        p.fillRect(rect(), cream);
        p.scale(width() / 560.0, height() / 420.0);
        QFont font = this->font();
        font.setStyleHint(QFont::TypeWriter);
        font.setPixelSize(qBound(14, font.pixelSize(), 20));
        font.setStyleStrategy(QFont::PreferAntialias);
        p.setFont(font);
        p.setPen(dark);
        p.drawText(20, 25, title);
        p.drawText(20, 51,
                   QString("SCORE %1   BEST %2").arg(score, 4, 10, QChar('0')).arg(best, 4, 10, QChar('0')));
        p.fillRect(18, 68, 524, 244, dark);
        p.setPen(green);
        p.drawRect(18, 68, 523, 243);
        p.setPen(dark);
        p.drawText(QRect(20, 326, 520, 84), Qt::AlignLeft | Qt::TextWordWrap, instructions);
        p.translate(0, 10);
    }
    void overlay(QPainter &p, const QString &title, const QString &subtitle) {
        p.fillRect(82, 132, 396, 102, dark);
        p.setPen(cream);
        p.drawRect(82, 132, 396, 102);
        p.drawText(QRect(90, 138, 380, 32), Qt::AlignCenter, title);
        p.setPen(amber);
        p.drawText(QRect(90, 174, 380, 52), Qt::AlignCenter | Qt::TextWordWrap, subtitle);
    }
};

class Circuit final : public Game {
  public:
    Circuit(SimSystem *system, QWidget *parent) : Game(system, parent) {
        best = system->setting("game/circuitBest", 0).toInt();
        timer.setInterval(110);
        reset();
    }

  private:
    std::vector<QPoint> cable;
    QPoint direction{1, 0}, nextDirection{1, 0}, target;
    bool won = false;
    void reset() override {
        cable = {{8, 6}, {7, 6}, {6, 6}, {5, 6}};
        direction = nextDirection = QPoint(1, 0);
        score = 0;
        started = paused = ended = won = false;
        timer.setInterval(110);
        placeTarget();
    }
    void placeTarget() {
        std::vector<QPoint> available;
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 26; ++x)
                if (std::find(cable.begin(), cable.end(), QPoint(x, y)) == cable.end())
                    available.emplace_back(x, y);
        if (available.empty()) {
            won = ended = true;
            return;
        }
        target = available[QRandomGenerator::global()->bounded(int(available.size()))];
    }
    void tick() override {
        if (!started || paused || ended)
            return;
        direction = nextDirection;
        const QPoint head = cable.front() + direction;
        const bool eating = head == target;
        const auto last = eating ? cable.end() : cable.end() - 1;
        if (head.x() < 0 || head.x() >= 26 || head.y() < 0 || head.y() >= 12 ||
            std::find(cable.begin(), last, head) != last) {
            ended = true;
            update();
            return;
        }
        cable.insert(cable.begin(), head);
        if (eating) {
            score += 10;
            if (score > best) {
                best = score;
                system->setSetting("game/circuitBest", best);
            }
            placeTarget();
            timer.setInterval(std::max(55, 110 - score / 5));
        } else
            cable.pop_back();
        update();
    }
    void keyPressEvent(QKeyEvent *event) override {
        const int key = event->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            reset();
            update();
            return;
        }
        if (key == Qt::Key_Space) {
            if (started && !ended)
                paused = !paused;
            update();
            return;
        }
        QPoint requested;
        if (key == Qt::Key_Left || key == Qt::Key_A)
            requested = {-1, 0};
        else if (key == Qt::Key_Right || key == Qt::Key_D)
            requested = {1, 0};
        else if (key == Qt::Key_Up || key == Qt::Key_W)
            requested = {0, -1};
        else if (key == Qt::Key_Down || key == Qt::Key_S)
            requested = {0, 1};
        else {
            QWidget::keyPressEvent(event);
            return;
        }
        if (requested + direction != QPoint(0, 0))
            nextDirection = requested;
        started = true;
        update();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        prepare(
            p, "CIRCUIT / 01",
            "ARROWS/WASD: move  SPACE: pause\nCollect contacts; avoid walls and the cable.\nENTER: restart");
        p.setPen(QColor("#243a35"));
        for (int x = 20; x <= 540; x += 20)
            p.drawLine(x, 60, x, 300);
        for (int y = 60; y <= 300; y += 20)
            p.drawLine(20, y, 540, y);
        auto cell = [](QPoint c) { return QRect(20 + c.x() * 20 + 3, 60 + c.y() * 20 + 3, 14, 14); };
        for (size_t i = 0; i < cable.size(); ++i) {
            const QRect r = cell(cable[i]);
            p.fillRect(r, i == 0 ? cream : green);
            if (i > 0) {
                const QPoint a = cell(cable[i - 1]).center(), b = r.center();
                p.setPen(QPen(green, 6));
                p.drawLine(a, b);
            }
        }
        const QRect r = cell(cable.front());
        p.fillRect(r.adjusted(4, 4, -4, -4), dark);
        const QRect t = cell(target);
        p.setPen(QPen(amber, 2));
        p.drawRect(t);
        p.fillRect(t.adjusted(4, 4, -4, -4), amber);
        if (ended)
            overlay(p, won ? "NETWORK COMPLETE" : "SIGNAL LOST", "ENTER for a new connection");
        else if (paused)
            overlay(p, "CIRCUIT PAUSED", "SPACE to continue");
        else if (!started)
            overlay(p, "MAKE A CONNECTION", "Press an arrow key to begin");
    }
};

class Meteor final : public Game {
  public:
    Meteor(SimSystem *system, QWidget *parent) : Game(system, parent) {
        best = system->setting("game/meteorBest", 0).toInt();
        reset();
    }

  private:
    struct Rock {
        float x, y, speed;
        int size;
    };
    std::vector<Rock> rocks;
    std::vector<QPointF> shots;
    float ship = 260, spawn = 0, cooldown = 0;
    int lives = 3, flashes = 0;
    bool left = false, right = false, firing = false;
    void reset() override {
        rocks.clear();
        shots.clear();
        score = 0;
        lives = 3;
        ship = 260;
        spawn = 0.5F;
        cooldown = 0;
        flashes = 0;
        paused = ended = started = left = right = firing = false;
    }
    void tick() override {
        if (!started || paused || ended)
            return;
        ship = std::clamp(ship + (int(right) - int(left)) * 6.0F, 14.0F, 506.0F);
        cooldown -= 0.03F;
        if (firing && cooldown <= 0) {
            shots.emplace_back(ship, 212);
            cooldown = 0.18F;
        }
        spawn -= 0.03F;
        if (spawn <= 0) {
            rocks.push_back({float(QRandomGenerator::global()->bounded(24, 496)), -10, 1.4F + score / 180.0F,
                             QRandomGenerator::global()->bounded(9, 15)});
            spawn = std::max(0.22F, 0.85F - score / 700.0F);
        }
        for (auto &shot : shots)
            shot.ry() -= 8;
        for (auto &rock : rocks)
            rock.y += rock.speed;
        for (auto &rock : rocks) {
            for (auto &shot : shots) {
                if (std::abs(rock.x - shot.x()) < rock.size && std::abs(rock.y - shot.y()) < rock.size + 4) {
                    rock.y = 1000;
                    shot.setY(-100);
                    score += 10;
                    if (score > best) {
                        best = score;
                        system->setSetting("game/meteorBest", best);
                    }
                    break;
                }
            }
            if (rock.y != 1000 &&
                (rock.y > 235 || (rock.y > 201 && std::abs(rock.x - ship) < rock.size + 12))) {
                --lives;
                flashes = 5;
                rock.y = 1000;
            }
        }
        rocks.erase(std::remove_if(rocks.begin(), rocks.end(), [](const Rock &r) { return r.y > 240; }),
                    rocks.end());
        shots.erase(std::remove_if(shots.begin(), shots.end(), [](const QPointF &s) { return s.y() < 0; }),
                    shots.end());
        if (lives <= 0) {
            lives = 0;
            ended = true;
            left = right = firing = false;
        }
        if (flashes > 0)
            --flashes;
        update();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->isAutoRepeat())
            return;
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            reset();
            update();
            return;
        case Qt::Key_P:
            if (started && !ended)
                paused = !paused;
            update();
            return;
        case Qt::Key_Left:
        case Qt::Key_A:
            left = true;
            break;
        case Qt::Key_Right:
        case Qt::Key_D:
            right = true;
            break;
        case Qt::Key_Space:
        case Qt::Key_Up:
        case Qt::Key_W:
            firing = true;
            break;
        default:
            QWidget::keyPressEvent(event);
            return;
        }
        started = true;
        update();
    }
    void keyReleaseEvent(QKeyEvent *event) override {
        if (event->isAutoRepeat())
            return;
        switch (event->key()) {
        case Qt::Key_Left:
        case Qt::Key_A:
            left = false;
            break;
        case Qt::Key_Right:
        case Qt::Key_D:
            right = false;
            break;
        case Qt::Key_Space:
        case Qt::Key_Up:
        case Qt::Key_W:
            firing = false;
            break;
        default:
            QWidget::keyReleaseEvent(event);
            return;
        }
    }
    void focusOutEvent(QFocusEvent *event) override {
        left = right = firing = false;
        QWidget::focusOutEvent(event);
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        prepare(p, "ORBITAL WATCH / 02",
                "ARROWS/A/D: move  SPACE: fire\nP: pause  ENTER: restart\nProtect the station. Shields: " +
                    QString::number(lives));
        p.save();
        p.translate(20, 60);
        p.setPen(green);
        for (int i = 0; i < 35; ++i)
            p.drawPoint((i * 83 + 13) % 520, (i * 47 + 7) % 225);
        p.setPen(QPen(green, 1));
        p.drawLine(0, 237, 520, 237);
        p.setPen(amber);
        p.drawText(10, 18, "SHIELDS " + QString(lives, QChar('+')));
        for (const auto &rock : rocks) {
            const int x = int(rock.x), y = int(rock.y), s = rock.size;
            p.setPen(amber);
            p.setBrush(dark);
            QPolygon shape;
            shape << QPoint(x - s, y - s / 2) << QPoint(x - s / 2, y - s) << QPoint(x + s / 2, y - s)
                  << QPoint(x + s, y) << QPoint(x + s / 2, y + s) << QPoint(x - s, y + s / 2);
            p.drawPolygon(shape);
            p.drawLine(x - 4, y - 2, x + 4, y + 3);
        }
        p.setPen(QPen(cream, 3));
        for (const auto &shot : shots)
            p.drawLine(QPointF(shot.x(), shot.y() - 6), shot);
        const QColor hull = flashes % 2 ? amber : cream;
        const int x = int(ship);
        p.fillRect(x - 3, 210, 6, 17, hull);
        p.fillRect(x - 8, 216, 16, 9, hull);
        p.fillRect(x - 13, 223, 26, 5, hull);
        p.fillRect(x - 5, 229, 3, 6, amber);
        p.fillRect(x + 2, 229, 3, 6, amber);
        p.restore();
        if (ended)
            overlay(p, "STATION OUT OF RANGE", "ENTER for a new patrol");
        else if (paused)
            overlay(p, "PATROL PAUSED", "P to continue");
        else if (!started)
            overlay(p, "ORBITAL WATCH", "SPACE starts the patrol");
    }
};
} // namespace

QWidget *createCircuitGame(SimSystem *system, QWidget *parent) { return new Circuit(system, parent); }
QWidget *createMeteorGame(SimSystem *system, QWidget *parent) { return new Meteor(system, parent); }
