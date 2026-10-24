#pragma once

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QPaintEvent;
class QShowEvent;
class QTimer;
class SimSystem;

QWidget *createHellgateGame(SimSystem *system, QWidget *parent = nullptr);

// Level one ("corridor 13"). Kept as a free function because the map is also a
// piece of station lore: it is printed by the tests and by the handbook.
const QStringList &hellgateMap();
QStringList hellgateLevel(int index);
int hellgateLevelCount();

// A small textured raycaster in the 1986 tradition: no GPU, no textures on disk,
// every wall column and sprite comes from the simulated A16.
class HellgateGame final : public QWidget {
  public:
    explicit HellgateGame(SimSystem *system, QWidget *parent = nullptr);

    // Simulation and state, exposed so the regression tests can drive the game
    // exactly like the 33 ms timer does.
    void step(double seconds);
    // Practice mode: drones hold their fire so the map can be explored. Toggled
    // with T, and used by the navigation tests.
    void setPeaceful(bool on);
    bool peaceful() const { return peaceful_; }
    void resetGame();
    bool fire();        // true when a drone was hit
    bool openDoor();    // true when a gate was opened or closed
    QImage renderFrame() const;

    QPointF playerPosition() const { return {posX_, posY_}; }
    double playerAngle() const;
    int health() const { return health_; }
    int ammo() const { return ammo_; }
    int kills() const { return kills_; }
    int dronesLeft() const;
    int projectiles() const { return projectiles_.size(); }
    int level() const { return level_; }
    int levelCount() const { return levels_.size(); }
    bool bossAlive() const;
    int bossHealth() const;
    bool passable(int cellX, int cellY) const;
    bool finished() const { return won_ || dead_; }
    bool victory() const { return won_; }
    double elapsedSeconds() const { return elapsed_; }
    QString banner() const { return banner_; }

  protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

  private:
    struct Drone {
        double x = 0, y = 0;
        int hp = 3;
        double cooldown = 0;
        double flash = 0;
        double stray = 0;
        double wanderX = 0, wanderY = 0;
        double aim = 0;
        bool boss = false;
        bool alive = true;
    };
    struct Pickup {
        double x = 0, y = 0;
        int kind = 0; // 0 health, 1 ammo
        bool taken = false;
    };
    struct Bolt {
        double x = 0, y = 0, vx = 0, vy = 0, life = 0;
    };

    SimSystem *system_ = nullptr;
    QTimer *timer_ = nullptr;
    QVector<QStringList> levels_;
    int level_ = 0;
    QStringList map_;
    QImage walls_[5];
    QImage droneSprite_, bossSprite_, boltSprite_, healthSprite_, ammoSprite_, gateSprite_;
    QVector<Drone> drones_;
    QVector<Pickup> pickups_;
    QVector<Bolt> projectiles_;

    double posX_ = 0, posY_ = 0, dirX_ = 1, dirY_ = 0, planeX_ = 0, planeY_ = 0.66;
    double elapsed_ = 0, totalElapsed_ = 0, best_ = 0;
    double muzzle_ = 0, damageFlash_ = 0;
    QHash<int, double> doorOpen_;   // cell key -> 0..1
    QHash<int, double> doorHold_;   // cell key -> seconds left open
    int health_ = 100, ammo_ = 24, kills_ = 0;
    bool dead_ = false, won_ = false, paused_ = false, peaceful_ = false;
    bool keyForward_ = false, keyBack_ = false, keyLeft_ = false, keyRight_ = false;
    bool keyTurnLeft_ = false, keyTurnRight_ = false, keyRun_ = false;
    QString banner_;
    double bannerTime_ = 0;

    void buildMap();
    void buildArtwork();
    void loadLevel(int index, bool keepStats);
    void say(const QString &banner, double seconds = 2.6);
    void collide(double nx, double ny);
    void moveBy(double distance, double strafe);
    void updateDoors(double seconds);
    void updateDrones(double seconds);
    void updateProjectiles(double seconds);
    void finishLevel();
    void fireBolt(const Drone &boss);
    bool lineOfSight(double x, double y) const;
    bool solid(double x, double y) const;
    QString bestKey(int level) const;
};
