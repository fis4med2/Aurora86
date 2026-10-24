#include "hellgate.h"
#include "system.h"

#include <QImage>
#include <QQueue>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

namespace {
constexpr int CellCount = 24;

double angleDelta(double a, double b) {
    double d = a - b;
    while (d > M_PI)
        d -= 2 * M_PI;
    while (d < -M_PI)
        d += 2 * M_PI;
    return d;
}

// Drives the game through the same key events and ticks the 33 ms timer uses.
void hold(HellgateGame &game, int key, double seconds) {
    QTest::keyPress(&game, Qt::Key(key));
    for (double t = 0; t < seconds; t += 0.02)
        game.step(0.02);
    QTest::keyRelease(&game, Qt::Key(key));
}

void turnTo(HellgateGame &game, double target) {
    for (int guard = 0; guard < 400; ++guard) {
        const double error = angleDelta(target, game.playerAngle());
        if (std::abs(error) < 0.03)
            break;
        const int key = error > 0 ? Qt::Key_Right : Qt::Key_Left;
        QTest::keyPress(&game, Qt::Key(key));
        game.step(0.02);
        QTest::keyRelease(&game, Qt::Key(key));
    }
}

// Breadth-first path over the cells the game itself says are walkable, so the
// route is never hand-scripted.
QVector<QPoint> route(const HellgateGame &game, QPoint from, QPoint to) {
    QVector<QPoint> parents(CellCount * CellCount, QPoint(-1, -1));
    QSet<int> seen{from.y() * CellCount + from.x()};
    QQueue<QPoint> queue;
    queue.enqueue(from);
    while (!queue.isEmpty()) {
        const QPoint at = queue.dequeue();
        if (at == to)
            break;
        for (const QPoint step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
            const QPoint next(at.x() + step.x(), at.y() + step.y());
            if (next.x() < 0 || next.y() < 0 || next.x() >= CellCount || next.y() >= CellCount)
                continue;
            const int key = next.y() * CellCount + next.x();
            if (seen.contains(key) || !game.passable(next.x(), next.y()))
                continue;
            seen.insert(key);
            parents[key] = at;
            queue.enqueue(next);
        }
    }
    QVector<QPoint> path;
    QPoint at = to;
    if (!seen.contains(to.y() * CellCount + to.x()) || to == from)
        return path;
    while (at != from) {
        path.prepend(at);
        at = parents[at.y() * CellCount + at.x()];
    }
    return path;
}

bool walkTo(HellgateGame &game, QPointF target) {
    for (int guard = 0; guard < 1500; ++guard) {
        const QPointF position = game.playerPosition();
        if (std::hypot(position.x() - target.x(), position.y() - target.y()) < 0.3)
            return true;
        const QPoint from(int(std::floor(position.x())), int(std::floor(position.y())));
        const QPoint to(int(std::floor(target.x())), int(std::floor(target.y())));
        const QVector<QPoint> path = route(game, from, to);
        const QPoint next = from == to ? to : (path.isEmpty() ? QPoint(-1, -1) : path.first());
        if (next.x() < 0)
            return false;
        const QPointF waypoint(next.x() + 0.5, next.y() + 0.5);
        const double wanted = std::atan2(waypoint.y() - position.y(), waypoint.x() - position.x());
        const double error = angleDelta(wanted, game.playerAngle());
        if (std::abs(error) > 0.1) {
            const int key = error > 0 ? Qt::Key_Right : Qt::Key_Left;
            QTest::keyPress(&game, Qt::Key(key));
            game.step(0.02);
            QTest::keyRelease(&game, Qt::Key(key));
            continue;
        }
        QTest::keyPress(&game, Qt::Key_W);
        game.step(0.02);
        QTest::keyRelease(&game, Qt::Key_W);
    }
    return false;
}

int distinctColours(const QImage &image) {
    QSet<QRgb> seen;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = 0; x < image.width(); x += 2)
            seen.insert(image.pixel(x, y) | 0xff000000);
    return seen.size();
}
} // namespace

class HellgateTest : public QObject {
    Q_OBJECT
  private slots:
    void mapIsCompleteAndConnected() {
        const QStringList &map = hellgateMap();
        QCOMPARE(map.size(), CellCount);
        int starts = 0, gates = 0, drones = 0;
        for (const QString &row : map) {
            QCOMPARE(row.size(), CellCount);
            for (const QChar cell : row) {
                if (cell == 'P')
                    ++starts;
                else if (cell == 'X')
                    ++gates;
                else if (cell == 'g')
                    ++drones;
            }
        }
        QCOMPARE(starts, 1);
        QCOMPARE(gates, 1);
        QVERIFY(drones >= 4);

        // Every walkable cell must be reachable with the gates open, otherwise a
        // drone or the exit could be sealed behind a wall.
        int px = -1, py = -1;
        for (int y = 0; y < map.size(); ++y)
            for (int x = 0; x < map.at(y).size(); ++x)
                if (map.at(y).at(x) == 'P') {
                    px = x;
                    py = y;
                }
        QSet<int> seen;
        QQueue<QPoint> queue;
        queue.enqueue(QPoint(px, py));
        seen.insert(py * CellCount + px);
        while (!queue.isEmpty()) {
            const QPoint at = queue.dequeue();
            for (const QPoint step : {QPoint(1, 0), QPoint(-1, 0), QPoint(0, 1), QPoint(0, -1)}) {
                const int nx = at.x() + step.x(), ny = at.y() + step.y();
                if (nx < 0 || ny < 0 || nx >= CellCount || ny >= CellCount)
                    continue;
                const QChar cell = map.at(ny).at(nx);
                if (cell == '#' || cell == '=')
                    continue;
                const int key = ny * CellCount + nx;
                if (seen.contains(key))
                    continue;
                seen.insert(key);
                queue.enqueue(QPoint(nx, ny));
            }
        }
        for (int y = 0; y < map.size(); ++y)
            for (int x = 0; x < map.at(y).size(); ++x) {
                const QChar cell = map.at(y).at(x);
                if (cell != '#' && cell != '=')
                    QVERIFY2(seen.contains(y * CellCount + x),
                             qPrintable(QString("sealed cell at %1,%2").arg(x).arg(y)));
            }
    }

    void frameRendersAWall() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        HellgateGame game(&system);
        game.resize(760, 520);

        const QImage frame = game.renderFrame();
        QCOMPARE(frame.size(), QSize(320, 200));
        QVERIFY(distinctColours(frame) >= 12);
        // The start cell faces a wall, so the middle of the horizon is not floor.
        const QRgb horizon = frame.pixel(160, 100);
        const QRgb ceiling = frame.pixel(160, 2);
        QVERIFY(horizon != ceiling);

        const double before = game.playerAngle();
        QTest::keyPress(&game, Qt::Key_Right);
        game.step(0.6);
        QTest::keyRelease(&game, Qt::Key_Right);
        QVERIFY(std::abs(angleDelta(game.playerAngle(), before)) > 0.5);
        QVERIFY(game.renderFrame() != frame);
    }

    void walkingStopsAtWalls() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        HellgateGame game(&system);
        hold(game, Qt::Key_W, 3.0);
        const QPointF position = game.playerPosition();
        QVERIFY(position.x() > 4.5);   // it moved forward
        QVERIFY(position.x() < 7.0);   // but not through the wall at column 7
        QVERIFY(game.passable(int(position.x()), int(position.y())));
    }

    void firingSpendsAmmoAndMissesEmptyAir() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        HellgateGame game(&system);
        QCOMPARE(game.ammo(), 24);
        QCOMPARE(game.dronesLeft(), 5);
        QVERIFY(!game.fire()); // the corridor ahead is empty
        QCOMPARE(game.ammo(), 23);
        QVERIFY(!game.fire());
        QCOMPARE(game.ammo(), 22);
        QVERIFY(!game.finished());
    }

    void gatesBlockUntilOpened() {
        QTemporaryDir dir;
        SimSystem system(dir.path());
        QVERIFY(system.load());
        HellgateGame game(&system);

        QCOMPARE(hellgateMap().at(4).at(15), QChar('+'));
        QVERIFY(!game.passable(15, 4));
        QVERIFY(!game.openDoor()); // nothing within reach at the start

        // The exit sits beyond the sealed gate, so with the gate shut there is
        // no route from the start cell to it.
        QVERIFY(route(game, QPoint(4, 3), QPoint(20, 19)).isEmpty());

        // Walk up to the gate with the route finder. The keep-out radius means
        // the walker stops one cell short, which is exactly the interaction range.
        QVERIFY(walkTo(game, QPointF(14.5, 4.5)));
        QVERIFY(!game.passable(15, 4));
        turnTo(game, 0); // face the gate, like a player would
        QVERIFY(game.openDoor());
        for (int i = 0; i < 40; ++i)
            game.step(0.02);
        QVERIFY(game.passable(15, 4));
        QVERIFY(walkTo(game, QPointF(16.5, 4.5)));
        QVERIFY(game.playerPosition().x() > 15.5);
    }
};

QTEST_MAIN(HellgateTest)
#include "hellgate_test.moc"
