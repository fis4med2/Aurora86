#include "hellgate.h"
#include "system.h"

#include <QFocusEvent>
#include <QHideEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPolygon>
#include <QRandomGenerator>
#include <QShowEvent>
#include <QTimer>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr int ViewWidth = 320;
constexpr int ViewHeight = 200;
constexpr int CellCount = 24;
constexpr double Fov = 0.66;   // camera plane length
constexpr double Radius = 0.22; // player collision radius
constexpr int HudHeight = 46;

const QColor ink("#0b1514"), cream("#efe5bd"), green("#9fbd85"), amber("#d9a65c"),
    rust("#b0512f");

inline QRgb dim(QRgb colour, double factor) {
    return qRgb(qBound(0, int(qRed(colour) * factor), 255),
                qBound(0, int(qGreen(colour) * factor), 255),
                qBound(0, int(qBlue(colour) * factor), 255));
}

QImage brickTexture() {
    QImage texture(64, 64, QImage::Format_RGB32);
    texture.fill(QColor("#33513f"));
    QPainter p(&texture);
    p.setPen(QPen(QColor("#1b2b24"), 2));
    for (int row = 0; row < 4; ++row) {
        const int y = row * 16;
        p.drawLine(0, y + 15, 63, y + 15);
        const int offset = (row % 2) ? 16 : 0;
        for (int x = offset; x < 64; x += 32)
            p.drawLine(x, y, x, y + 15);
    }
    p.setPen(QColor("#41624c"));
    for (int i = 0; i < 90; ++i) {
        const int x = int(QRandomGenerator::global()->bounded(64));
        const int y = int(QRandomGenerator::global()->bounded(64));
        p.drawPoint(x, y);
    }
    p.end();
    return texture;
}

QImage metalTexture() {
    QImage texture(64, 64, QImage::Format_RGB32);
    texture.fill(QColor("#2c453c"));
    QPainter p(&texture);
    p.setPen(QPen(QColor("#16241f"), 2));
    for (int x = 0; x < 64; x += 16)
        p.drawLine(x, 0, x, 63);
    p.setPen(QColor("#3d5c4f"));
    for (int x = 8; x < 64; x += 16)
        p.drawLine(x, 0, x, 63);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d9a65c"));
    for (int y = 8; y < 64; y += 24)
        for (int x = 8; x < 64; x += 16)
            p.drawEllipse(QPoint(x, y), 2, 2);
    p.end();
    return texture;
}

QImage gateTexture() {
    QImage texture(64, 64, QImage::Format_RGB32);
    texture.fill(QColor("#4c3d22"));
    QPainter p(&texture);
    p.fillRect(0, 0, 64, 5, amber);
    p.fillRect(0, 59, 64, 5, amber);
    p.setPen(QPen(QColor("#2a2213"), 2));
    for (int y = 8; y < 60; y += 12)
        p.drawLine(2, y, 61, y);
    p.setPen(QColor("#8a6b32"));
    p.drawLine(31, 5, 31, 59);
    p.setPen(QColor("#efe5bd"));
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(13);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(0, 24, 64, 16), Qt::AlignCenter, "GATE");
    p.end();
    return texture;
}

QImage droneSprite() {
    QImage sprite(48, 64, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);
    QPainter p(&sprite);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#6d2c1c"));
    p.drawEllipse(QRectF(10, 2, 28, 26));      // head
    p.drawPolygon(QPolygonF({{10, 8}, {2, 0}, {8, 18}}));   // left horn
    p.drawPolygon(QPolygonF({{38, 8}, {46, 0}, {40, 18}})); // right horn
    p.setBrush(QColor("#4a1f14"));
    p.drawRoundedRect(QRectF(8, 26, 32, 26), 5, 5); // torso
    p.setBrush(QColor("#8f3d24"));
    p.drawRect(QRectF(2, 30, 8, 18));   // arms
    p.drawRect(QRectF(38, 30, 8, 18));
    p.setBrush(QColor("#3a1710"));
    p.drawRect(QRectF(14, 50, 7, 13)); // legs
    p.drawRect(QRectF(27, 50, 7, 13));
    p.setBrush(QColor("#ffd27f"));
    p.drawRect(QRectF(16, 10, 6, 5));  // eyes
    p.drawRect(QRectF(26, 10, 6, 5));
    p.setBrush(QColor("#d9a65c"));
    p.drawRect(QRectF(22, 30, 4, 16)); // core
    p.setBrush(QColor("#1b0d09"));
    p.drawRect(QRectF(18, 20, 12, 3)); // mouth
    p.end();
    return sprite;
}

QImage healthSprite() {
    QImage sprite(32, 32, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);
    QPainter p(&sprite);
    p.setPen(Qt::NoPen);
    p.setBrush(cream);
    p.drawRect(QRectF(4, 8, 24, 18));
    p.setBrush(QColor("#2b4335"));
    p.drawRect(QRectF(4, 8, 24, 4));
    p.setBrush(rust);
    p.drawRect(QRectF(14, 12, 4, 12));
    p.drawRect(QRectF(10, 16, 12, 4));
    p.end();
    return sprite;
}

QImage ammoSprite() {
    QImage sprite(32, 32, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);
    QPainter p(&sprite);
    p.setPen(QPen(QColor("#0f1b17"), 2));
    p.setBrush(QColor("#3d5c4f"));
    p.drawRect(QRectF(4, 12, 24, 14));
    p.setPen(Qt::NoPen);
    p.setBrush(amber);
    for (int i = 0; i < 4; ++i)
        p.drawRect(QRectF(7 + i * 5, 5, 3, 9));
    p.end();
    return sprite;
}

QImage portalSprite() {
    QImage sprite(48, 64, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);
    QPainter p(&sprite);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(amber, 3));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(6, 6, 36, 56), 16, 16);
    p.setPen(QPen(QColor("#ffd27f"), 2));
    p.drawRoundedRect(QRectF(11, 12, 26, 48), 12, 12);
    p.fillRect(QRectF(16, 22, 16, 32), QColor("#5c4a28"));
    p.setPen(QColor("#efe5bd"));
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(11);
    p.setFont(font);
    p.drawText(QRect(8, 34, 32, 12), Qt::AlignCenter, "EXIT");
    p.end();
    return sprite;
}
} // namespace

const QStringList &hellgateMap() {
    static const QStringList rows = {
        "########################",
        "#......##.....a##......#",
        "#g.....##.=....##.=....#",
        "#a........g....##......#",
        "#..P...##......+.......#",
        "#......##....g.##......#",
        "####.############......#",
        "####+###############+###",
        "#......##..g...#####.###",
        "#.=....##.=....##......#",
        "#..............##.=....#",
        "#......##......+.......#",
        "#.g....##......##......#",
        "#......#####+####......#",
        "####.#######.#######+###",
        "####+#######h#######.###",
        "#......##......#####+###",
        "#.=....##......##......#",
        "#..............##......#",
        "#...a..##......h+......#",
        "#......##########..X...#",
        "#......##########......#",
        "########################",
        "########################"};

    return rows;
}

HellgateGame::HellgateGame(SimSystem *system, QWidget *parent) : QWidget(parent), system_(system) {
    setObjectName("hellgate");
    setMinimumSize(620, 460);
    resize(780, 560);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    map_ = hellgateMap();
    buildArtwork();
    resetGame();
    timer_ = new QTimer(this);
    timer_->setInterval(33);
    connect(timer_, &QTimer::timeout, this, [this] {
        if (!isVisible())
            return;
        step(timer_->interval() / 1000.0);
        update();
    });
}

void HellgateGame::buildArtwork() {
    walls_[1] = brickTexture();
    walls_[2] = metalTexture();
    walls_[3] = gateTexture();
    droneSprite_ = droneSprite();
    healthSprite_ = healthSprite();
    ammoSprite_ = ammoSprite();
    gateSprite_ = portalSprite();
}

void HellgateGame::resetGame() {
    drones_.clear();
    pickups_.clear();
    doorOpen_.clear();
    doorHold_.clear();
    health_ = 100;
    ammo_ = 24;
    kills_ = 0;
    elapsed_ = 0;
    muzzle_ = damageFlash_ = 0;
    dead_ = won_ = paused_ = false;
    keyForward_ = keyBack_ = keyLeft_ = keyRight_ = false;
    keyTurnLeft_ = keyTurnRight_ = keyRun_ = false;
    best_ = system_ ? system_->setting("game/hellgateBest", 0.0).toDouble() : 0.0;
    posX_ = posY_ = 1.5;
    dirX_ = 1;
    dirY_ = 0;
    planeX_ = 0;
    planeY_ = Fov;
    for (int y = 0; y < int(map_.size()); ++y) {
        const QString &row = map_.at(y);
        for (int x = 0; x < int(row.size()); ++x) {
            const QChar cell = row.at(x);
            if (cell == 'P') {
                posX_ = x + 0.5;
                posY_ = y + 0.5;
            } else if (cell == 'g') {
                Drone drone;
                drone.x = x + 0.5;
                drone.y = y + 0.5;
                drone.hp = 3;
                drone.stray = 0.5 + 1.5 * QRandomGenerator::global()->generateDouble();
                drones_.append(drone);
            } else if (cell == 'h' || cell == 'a') {
                Pickup pickup;
                pickup.x = x + 0.5;
                pickup.y = y + 0.5;
                pickup.kind = cell == 'h' ? 0 : 1;
                pickups_.append(pickup);
            }
        }
    }
    update();
}

double HellgateGame::playerAngle() const { return std::atan2(dirY_, dirX_); }

int HellgateGame::dronesLeft() const {
    int left = 0;
    for (const auto &drone : drones_)
        if (drone.alive)
            ++left;
    return left;
}

bool HellgateGame::solid(double x, double y) const {
    const int mx = int(std::floor(x));
    const int my = int(std::floor(y));
    if (my < 0 || my >= map_.size() || mx < 0 || mx >= map_.at(my).size())
        return true;
    const QChar cell = map_.at(my).at(mx);
    if (cell == '#' || cell == '=')
        return true;
    if (cell == '+')
        return doorOpen_.value(my * CellCount + mx, 0.0) < 0.9;
    return false;
}

bool HellgateGame::passable(int cellX, int cellY) const { return !solid(cellX + 0.5, cellY + 0.5); }

void HellgateGame::collide(double nx, double ny) {
    const auto free = [this](double x, double y) {
        return !solid(x + Radius, y + Radius) && !solid(x - Radius, y + Radius) &&
               !solid(x + Radius, y - Radius) && !solid(x - Radius, y - Radius);
    };
    if (free(nx, ny)) {
        posX_ = nx;
        posY_ = ny;
        return;
    }
    if (free(nx, posY_))
        posX_ = nx;
    if (free(posX_, ny))
        posY_ = ny;
}

void HellgateGame::moveBy(double distance, double strafe) {
    const double dx = dirX_ * distance + planeX_ * strafe * 1.4;
    const double dy = dirY_ * distance + planeY_ * strafe * 1.4;
    collide(posX_ + dx, posY_ + dy);
}

void HellgateGame::updateDoors(double seconds) {
    const auto keys = doorOpen_.keys();
    for (int key : keys) {
        double hold = doorHold_.value(key, 0.0);
        double open = doorOpen_.value(key, 0.0);
        if (hold > 0) {
            hold = qMax(0.0, hold - seconds);
            open = qMin(1.0, open + seconds * 3.0);
            doorHold_[key] = hold;
        } else {
            open = qMax(0.0, open - seconds * 2.0);
        }
        doorOpen_[key] = open;
    }
}

bool HellgateGame::openDoor() {
    for (double d = 0.35; d <= 1.6; d += 0.12) {
        const int mx = int(std::floor(posX_ + dirX_ * d));
        const int my = int(std::floor(posY_ + dirY_ * d));
        if (my < 0 || my >= map_.size() || mx < 0 || mx >= map_.at(my).size())
            continue;
        if (map_.at(my).at(mx) == '+') {
            const int key = my * CellCount + mx;
            doorHold_[key] = 6.0;
            doorOpen_[key] = qMax(0.12, doorOpen_.value(key, 0.0));
            return true;
        }
    }
    return false;
}

bool HellgateGame::lineOfSight(double x, double y) const {
    double dx = x - posX_, dy = y - posY_;
    const double distance = std::hypot(dx, dy);
    if (distance < 0.05)
        return true;
    dx /= distance;
    dy /= distance;
    for (double d = 0.15; d < distance - 0.1; d += 0.1)
        if (solid(posX_ + dx * d, posY_ + dy * d))
            return false;
    return true;
}

void HellgateGame::updateDrones(double seconds) {
    for (auto &drone : drones_) {
        if (!drone.alive)
            continue;
        drone.flash = qMax(0.0, drone.flash - seconds * 3.0);
        drone.cooldown = qMax(0.0, drone.cooldown - seconds);
        const double dx = posX_ - drone.x, dy = posY_ - drone.y;
        const double distance = std::hypot(dx, dy);
        if (distance < 1.25) {
            if (drone.cooldown <= 0 && !dead_) {
                const int damage = 4 + int(QRandomGenerator::global()->bounded(5));
                health_ = qMax(0, health_ - damage);
                damageFlash_ = 1.0;
                drone.cooldown = 1.15;
                if (health_ == 0)
                    dead_ = true;
            }
            continue;
        }
        double stepX = 0, stepY = 0;
        if (!dead_ && distance < 13 && lineOfSight(drone.x, drone.y)) {
            stepX = dx / distance * 1.45 * seconds;
            stepY = dy / distance * 1.45 * seconds;
        } else {
            drone.stray -= seconds;
            if (drone.stray <= 0) {
                drone.stray = 1.2 + 1.6 * QRandomGenerator::global()->generateDouble();
                const double angle = QRandomGenerator::global()->generateDouble() * 2 * M_PI;
                drone.wanderX = std::cos(angle);
                drone.wanderY = std::sin(angle);
            }
            stepX = drone.wanderX * 0.55 * seconds;
            stepY = drone.wanderY * 0.55 * seconds;
        }
        if (!solid(drone.x + stepX, drone.y + stepY)) {
            drone.x += stepX;
            drone.y += stepY;
        }
    }
}

bool HellgateGame::fire() {
    if (dead_ || won_ || paused_ || ammo_ <= 0)
        return false;
    --ammo_;
    muzzle_ = 0.1;
    double bestAngle = 0.13;
    int target = -1;
    for (int i = 0; i < drones_.size(); ++i) {
        const auto &drone = drones_.at(i);
        if (!drone.alive)
            continue;
        const double dx = drone.x - posX_, dy = drone.y - posY_;
        if (std::hypot(dx, dy) > 14 || !lineOfSight(drone.x, drone.y))
            continue;
        double angle = std::atan2(dy, dx) - std::atan2(dirY_, dirX_);
        while (angle > M_PI)
            angle -= 2 * M_PI;
        while (angle < -M_PI)
            angle += 2 * M_PI;
        if (std::abs(angle) < bestAngle) {
            bestAngle = std::abs(angle);
            target = i;
        }
    }
    if (target < 0)
        return false;
    auto &drone = drones_[target];
    drone.flash = 1.0;
    --drone.hp;
    if (drone.hp <= 0) {
        drone.alive = false;
        ++kills_;
    }
    return true;
}

void HellgateGame::step(double seconds) {
    if (dead_ || won_ || paused_)
        return;
    elapsed_ += seconds;
    muzzle_ = qMax(0.0, muzzle_ - seconds);
    damageFlash_ = qMax(0.0, damageFlash_ - seconds * 1.6);
    const double speed = (keyRun_ ? 3.0 : 1.85) * seconds;
    if (keyForward_)
        moveBy(speed, 0);
    if (keyBack_)
        moveBy(-speed, 0);
    if (keyLeft_)
        moveBy(0, -speed);
    if (keyRight_)
        moveBy(0, speed);
    const double turn = 2.3 * seconds;
    if (keyTurnLeft_ || keyTurnRight_) {
        const double angle = keyTurnLeft_ ? -turn : turn;
        const double oldX = dirX_, oldP = planeX_;
        const double c = std::cos(angle), s = std::sin(angle);
        dirX_ = dirX_ * c - dirY_ * s;
        dirY_ = oldX * s + dirY_ * c;
        planeX_ = planeX_ * c - planeY_ * s;
        planeY_ = oldP * s + planeY_ * c;
    }
    updateDoors(seconds);
    updateDrones(seconds);
    for (auto &pickup : pickups_) {
        if (pickup.taken)
            continue;
        if (std::hypot(pickup.x - posX_, pickup.y - posY_) < 0.55) {
            pickup.taken = true;
            if (pickup.kind == 0)
                health_ = qMin(100, health_ + 25);
            else
                ammo_ = qMin(99, ammo_ + 8);
        }
    }
    const int cx = int(std::floor(posX_)), cy = int(std::floor(posY_));
    if (cy >= 0 && cy < map_.size() && cx >= 0 && cx < map_.at(cy).size() &&
        map_.at(cy).at(cx) == 'X') {
        won_ = true;
        if (best_ <= 0 || elapsed_ < best_) {
            best_ = elapsed_;
            if (system_)
                system_->setSetting("game/hellgateBest", best_);
        }
    }
}

QImage HellgateGame::renderFrame() const {
    QImage frame(ViewWidth, ViewHeight, QImage::Format_RGB32);
    // Ceiling and floor: flat bands with a moving distance grid, no texture memory.
    for (int y = 0; y < ViewHeight; ++y) {
        auto *line = reinterpret_cast<QRgb *>(frame.scanLine(y));
        if (y < ViewHeight / 2) {
            const double t = double(y) / (ViewHeight / 2);
            // Dark ceiling with a faint haze near the horizon so depth reads.
            const QRgb colour = qRgb(int(9 + t * 20), int(18 + t * 30), int(18 + t * 26));
            for (int x = 0; x < ViewWidth; ++x)
                line[x] = colour;
        } else {
            const double rowDistance = (0.5 * ViewHeight) / (y - ViewHeight * 0.5);
            const double t = qBound(0.0, 1.0 - rowDistance / 14.0, 1.0);
            const bool band = int(rowDistance * 2.0) % 2 == 0;
            const int lift = band ? 5 : 0;
            const QRgb colour = qRgb(int(12 + t * 24) + lift, int(22 + t * 40) + lift,
                                     int(19 + t * 26) + lift);
            for (int x = 0; x < ViewWidth; ++x)
                line[x] = colour;
        }
    }

    std::vector<double> depth(ViewWidth, 1e30);
    for (int x = 0; x < ViewWidth; ++x) {
        const double cameraX = 2.0 * x / ViewWidth - 1.0;
        const double rayX = dirX_ + planeX_ * cameraX;
        const double rayY = dirY_ + planeY_ * cameraX;
        int mapX = int(std::floor(posX_));
        int mapY = int(std::floor(posY_));
        const double deltaX = rayX == 0 ? 1e30 : std::abs(1.0 / rayX);
        const double deltaY = rayY == 0 ? 1e30 : std::abs(1.0 / rayY);
        const int stepX = rayX < 0 ? -1 : 1;
        const int stepY = rayY < 0 ? -1 : 1;
        double sideX = rayX < 0 ? (posX_ - mapX) * deltaX : (mapX + 1.0 - posX_) * deltaX;
        double sideY = rayY < 0 ? (posY_ - mapY) * deltaY : (mapY + 1.0 - posY_) * deltaY;
        QChar tile;
        int side = 0;
        for (int guard = 0; guard < 96; ++guard) {
            if (sideX < sideY) {
                sideX += deltaX;
                mapX += stepX;
                side = 0;
            } else {
                sideY += deltaY;
                mapY += stepY;
                side = 1;
            }
            if (mapY < 0 || mapY >= map_.size() || mapX < 0 || mapX >= map_.at(mapY).size()) {
                tile = '#';
                break;
            }
            tile = map_.at(mapY).at(mapX);
            const bool door = tile == '+';
            const bool open = door && doorOpen_.value(mapY * CellCount + mapX, 0.0) >= 0.9;
            if ((tile == '#' || tile == '=' || (door && !open)))
                break;
            tile = QChar();
        }
        if (tile.isNull())
            continue;
        const double distance = side == 0 ? (sideX - deltaX) : (sideY - deltaY);
        depth[x] = qMax(0.01, distance);
        const int lineHeight = int(ViewHeight / depth[x]);
        const int drawStart = ViewHeight / 2 - lineHeight / 2;
        const int floorX = mapX, floorY = mapY;
        double wallX = side == 0 ? posY_ + depth[x] * rayY : posX_ + depth[x] * rayX;
        wallX -= std::floor(wallX);
        int texX = int(wallX * 64.0);
        if (side == 0 && rayX > 0)
            texX = 63 - texX;
        if (side == 1 && rayY < 0)
            texX = 63 - texX;
        const int texture = tile == '=' ? 2 : tile == '+' ? 3 : 1;
        if (tile == '+')
            texX = (texX + int(doorOpen_.value(floorY * CellCount + floorX, 0.0) * 63.0)) % 64;
        texX = qBound(0, texX, 63);
        const double shade = qBound(0.20, 1.0 - depth[x] / 15.0, 1.0) * (side == 1 ? 0.70 : 1.0);
        const double step = 64.0 / lineHeight;
        double texPos = (drawStart - ViewHeight / 2 + lineHeight / 2) * step;
        for (int y = drawStart; y < drawStart + lineHeight; ++y) {
            const int texY = int(texPos) & 63;
            texPos += step;
            if (y < 0 || y >= ViewHeight)
                continue;
            const QRgb colour = reinterpret_cast<const QRgb *>(walls_[texture].constScanLine(texY))[texX];
            reinterpret_cast<QRgb *>(frame.scanLine(y))[x] = dim(colour, shade);
        }
    }

    struct Billboard {
        double x, y, distance;
        const QImage *sprite;
        double scale;
        double tint; // 0 normal, 1 hit flash
    };
    std::vector<Billboard> billboards;
    for (const auto &drone : drones_) {
        if (!drone.alive)
            continue;
        billboards.push_back({drone.x, drone.y, std::hypot(drone.x - posX_, drone.y - posY_),
                              &droneSprite_, 0.72, drone.flash});
    }
    for (const auto &pickup : pickups_) {
        if (pickup.taken)
            continue;
        billboards.push_back({pickup.x, pickup.y, std::hypot(pickup.x - posX_, pickup.y - posY_),
                              pickup.kind == 0 ? &healthSprite_ : &ammoSprite_, 0.26, 0.0});
    }
    for (int y = 0; y < int(map_.size()); ++y)
        for (int x = 0; x < int(map_.at(y).size()); ++x)
            if (map_.at(y).at(x) == 'X')
                billboards.push_back({x + 0.5, y + 0.5, std::hypot(x + 0.5 - posX_, y + 0.5 - posY_),
                                      &gateSprite_, 0.95, 0.0});
    std::sort(billboards.begin(), billboards.end(), [](const Billboard &a, const Billboard &b) {
        return a.distance > b.distance;
    });
    const double invDet = 1.0 / (planeX_ * dirY_ - dirX_ * planeY_);
    for (const auto &item : billboards) {
        const double spriteX = item.x - posX_, spriteY = item.y - posY_;
        const double transformX = invDet * (dirY_ * spriteX - dirX_ * spriteY);
        const double transformY = invDet * (-planeY_ * spriteX + planeX_ * spriteY);
        if (transformY <= 0.25)
            continue;
        const int screenX = int((ViewWidth / 2.0) * (1.0 + transformX / transformY));
        const int wallHeight = int(ViewHeight / transformY);
        const int height = qMax(1, int(wallHeight * item.scale));
        const int width = qMax(1, height * item.sprite->width() / item.sprite->height());
        const int bottom = ViewHeight / 2 + wallHeight / 2;
        const int top = bottom - height;
        const int left = screenX - width / 2;
        const double shade = qBound(0.28, 1.0 - item.distance / 16.0, 1.0);
        for (int px = 0; px < width; ++px) {
            const int x = left + px;
            if (x < 0 || x >= ViewWidth || transformY >= depth[x])
                continue;
            const int texX = px * item.sprite->width() / width;
            for (int py = 0; py < height; ++py) {
                const int y = top + py;
                if (y < 0 || y >= ViewHeight)
                    continue;
                const QRgb source =
                    reinterpret_cast<const QRgb *>(item.sprite->constScanLine(py * item.sprite->height() / height))[texX];
                if (qAlpha(source) < 110)
                    continue;
                QRgb colour = dim(qRgb(qRed(source), qGreen(source), qBlue(source)), shade);
                if (item.tint > 0) {
                    const double t = qBound(0.0, item.tint, 1.0);
                    colour = qRgb(qBound(0, int(qRed(colour) + 150 * t), 255),
                                  qBound(0, int(qGreen(colour) + 60 * t), 255),
                                  qBound(0, int(qBlue(colour) + 20 * t), 255));
                }
                reinterpret_cast<QRgb *>(frame.scanLine(y))[x] = colour;
            }
        }
    }
    return frame;
}

void HellgateGame::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), ink);
    const QRect view(0, 0, width(), height() - HudHeight);
    const QSize scaled = QSize(ViewWidth, ViewHeight).scaled(view.size(), Qt::KeepAspectRatio);
    const QRect target(view.x() + (view.width() - scaled.width()) / 2,
                       view.y() + (view.height() - scaled.height()) / 2, scaled.width(), scaled.height());
    p.drawImage(target, renderFrame());
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (damageFlash_ > 0) {
        QColor hurt(rust);
        hurt.setAlpha(int(qMin(0.55, damageFlash_ * 0.55) * 255));
        p.fillRect(target, hurt);
    }

    // The A16 does not have a GPU, so the "weapon" is a few polygons.
    const int gunX = target.center().x();
    const int gunBottom = target.bottom();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#111d1a"));
    p.drawPolygon(QPolygon({{gunX - 36, gunBottom}, {gunX - 26, gunBottom - 72},
                            {gunX - 9, gunBottom - 100}, {gunX + 9, gunBottom - 100},
                            {gunX + 26, gunBottom - 72}, {gunX + 36, gunBottom}}));
    p.setBrush(QColor("#1d2f28"));
    p.drawPolygon(QPolygon({{gunX - 23, gunBottom}, {gunX - 16, gunBottom - 66},
                            {gunX + 16, gunBottom - 66}, {gunX + 23, gunBottom}}));
    p.setBrush(QColor("#2b4335"));
    p.drawRect(QRect(gunX - 7, gunBottom - 118, 14, 30));
    p.setBrush(QColor("#54736a"));
    p.drawRect(QRect(gunX - 5, gunBottom - 122, 10, 7));
    p.setPen(QPen(QColor("#0b1514"), 2));
    p.drawLine(gunX - 26, gunBottom - 72, gunX + 26, gunBottom - 72);
    p.setPen(QPen(QColor("#8fac74"), 1));
    p.drawLine(gunX - 7, gunBottom - 118, gunX - 7, gunBottom - 90);
    if (muzzle_ > 0) {
        p.setBrush(QColor("#ffd27f"));
        p.drawEllipse(QPoint(gunX, gunBottom - 128), 15, 13);
        p.setBrush(QColor("#fff6d8"));
        p.drawEllipse(QPoint(gunX, gunBottom - 128), 7, 6);
    }
    p.setPen(QPen(cream, 1));
    p.drawLine(target.center().x() - 6, target.center().y(), target.center().x() - 2, target.center().y());
    p.drawLine(target.center().x() + 2, target.center().y(), target.center().x() + 6, target.center().y());
    p.drawLine(target.center().x(), target.center().y() - 6, target.center().x(), target.center().y() - 2);
    p.drawLine(target.center().x(), target.center().y() + 2, target.center().x(), target.center().y() + 6);

    const QRect hud(0, height() - HudHeight, width(), HudHeight);
    p.fillRect(hud, QColor("#0f1b17"));
    p.setPen(QColor("#2b4335"));
    p.drawLine(hud.topLeft(), hud.topRight());
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(14);
    p.setFont(font);
    p.setPen(cream);
    const QString clock = QString("%1:%2").arg(int(elapsed_) / 60, 2, 10, QChar('0'))
                              .arg(int(elapsed_) % 60, 2, 10, QChar('0'));
    p.drawText(hud.adjusted(14, 6, 0, 0), Qt::AlignLeft | Qt::AlignTop,
               QString("HEALTH %1   AMMO %2   DRONES %3   GATE %4")
                   .arg(health_, 3)
                   .arg(ammo_, 2)
                   .arg(dronesLeft(), 2)
                   .arg(clock));
    p.setPen(QColor("#8cae8b"));
    p.drawText(hud.adjusted(14, 25, -14, 0), Qt::AlignLeft | Qt::AlignTop,
               "W/S MOVE   A/D STRAFE   \u2190/\u2192 TURN   SPACE FIRE   E DOOR   P PAUSE");
    p.setPen(amber);
    p.drawText(hud.adjusted(0, 6, -14, 0), Qt::AlignRight | Qt::AlignTop, "A16 / CORRIDOR 13");
    if (best_ > 0)
        p.drawText(hud.adjusted(0, 25, -14, 0), Qt::AlignRight | Qt::AlignTop,
                   QString("BEST %1:%2").arg(int(best_) / 60, 2, 10, QChar('0'))
                       .arg(int(best_) % 60, 2, 10, QChar('0')));

    if (paused_ || dead_ || won_) {
        const QRect box = target.adjusted(target.width() / 8, target.height() / 4,
                                          -target.width() / 8, -target.height() / 4);
        p.fillRect(box, QColor(11, 21, 20, 232));
        p.setPen(amber);
        p.drawRect(box);
        QFont title("DejaVu Sans Mono");
        title.setPixelSize(24);
        title.setBold(true);
        p.setFont(title);
        p.setPen(won_ ? QColor("#ffd27f") : dead_ ? rust : cream);
        p.drawText(box.adjusted(0, 22, 0, 0), Qt::AlignHCenter | Qt::AlignTop,
                   won_ ? "GATE REACHED" : dead_ ? "SIGNAL LOST" : "PAUSED");
        QFont body("DejaVu Sans Mono");
        body.setPixelSize(13);
        p.setFont(body);
        p.setPen(cream);
        const QString detail =
            won_ ? QString("You crossed corridor 13 in %1:%2.\n%3 drones retired.   R to run it again")
                       .arg(int(elapsed_) / 60, 2, 10, QChar('0'))
                       .arg(int(elapsed_) % 60, 2, 10, QChar('0'))
                       .arg(kills_)
                 : dead_ ? QString("The drones found you first.\n%1 retired.   R to try again").arg(kills_)
                         : QString("P to continue.\nElapsed %1:%2")
                               .arg(int(elapsed_) / 60, 2, 10, QChar('0'))
                               .arg(int(elapsed_) % 60, 2, 10, QChar('0'));
        p.drawText(box.adjusted(18, 66, -18, -14), Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, detail);
    }
}

void HellgateGame::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
        keyForward_ = true;
        break;
    case Qt::Key_S:
    case Qt::Key_Down:
        keyBack_ = true;
        break;
    case Qt::Key_A:
        keyLeft_ = true;
        break;
    case Qt::Key_D:
        keyRight_ = true;
        break;
    case Qt::Key_Left:
        keyTurnLeft_ = true;
        break;
    case Qt::Key_Right:
        keyTurnRight_ = true;
        break;
    case Qt::Key_Shift:
        keyRun_ = true;
        break;
    case Qt::Key_Space:
    case Qt::Key_Control:
        fire();
        break;
    case Qt::Key_E:
        openDoor();
        break;
    case Qt::Key_P:
        if (!dead_ && !won_)
            paused_ = !paused_;
        break;
    case Qt::Key_R:
        if (dead_ || won_)
            resetGame();
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    update();
}

void HellgateGame::keyReleaseEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up:
        keyForward_ = false;
        break;
    case Qt::Key_S:
    case Qt::Key_Down:
        keyBack_ = false;
        break;
    case Qt::Key_A:
        keyLeft_ = false;
        break;
    case Qt::Key_D:
        keyRight_ = false;
        break;
    case Qt::Key_Left:
        keyTurnLeft_ = false;
        break;
    case Qt::Key_Right:
        keyTurnRight_ = false;
        break;
    case Qt::Key_Shift:
        keyRun_ = false;
        break;
    default:
        QWidget::keyReleaseEvent(event);
    }
}

void HellgateGame::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (timer_)
        timer_->start();
    setFocus();
}

void HellgateGame::hideEvent(QHideEvent *event) {
    if (timer_)
        timer_->stop();
    QWidget::hideEvent(event);
}

void HellgateGame::focusOutEvent(QFocusEvent *event) {
    keyForward_ = keyBack_ = keyLeft_ = keyRight_ = false;
    keyTurnLeft_ = keyTurnRight_ = keyRun_ = false;
    QWidget::focusOutEvent(event);
}

QWidget *createHellgateGame(SimSystem *system, QWidget *parent) {
    return new HellgateGame(system, parent);
}
