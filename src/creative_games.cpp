#include "creative_games.h"
#include "audio.h"
#include "system.h"

#include <QtWidgets>
#include <algorithm>
#include <array>
#include <functional>

namespace {
const QColor ink("#172b2a"), paper("#efe5bd"), leaf("#8fac74"), gold("#d9a65c");

class GameBoard final : public QWidget {
  public:
    std::function<void(QPainter &, const QRect &)> draw;
    std::function<void(QPoint)> clicked;
    explicit GameBoard(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumSize(380, 210);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setObjectName("creativeBoard");
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), ink);
        p.setRenderHint(QPainter::Antialiasing, false);
        if (draw)
            draw(p, rect());
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && clicked)
            clicked(event->position().toPoint());
    }
};

class Puzzle : public QWidget {
  public:
    Puzzle(SimSystem *system, const QString &title, const QString &instructions, QWidget *parent)
        : QWidget(parent), system_(system) {
        setFocusPolicy(Qt::StrongFocus);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        layout->setSpacing(7);
        auto *heading = new QLabel(title);
        QFont bold = heading->font();
        bold.setBold(true);
        heading->setFont(bold);
        layout->addWidget(heading);
        status_ = new QLabel;
        status_->setObjectName("gameStatus");
        status_->setWordWrap(true);
        layout->addWidget(status_);
        board_ = new GameBoard;
        layout->addWidget(board_, 1);
        auto *help = new QLabel(instructions);
        help->setWordWrap(true);
        layout->addWidget(help);
        row_ = new QHBoxLayout;
        layout->addLayout(row_);
        setMinimumSize(500, 390);
        resize(600, 420);
    }

  protected:
    SimSystem *system_;
    GameBoard *board_;
    QLabel *status_;
    QHBoxLayout *row_;
    QPushButton *control(const QString &text, std::function<void()> action) {
        auto *button = new QPushButton(text);
        button->setFocusPolicy(Qt::NoFocus);
        row_->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, action] {
            action();
            setFocus();
        });
        return button;
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        setFocus();
    }
    void mousePressEvent(QMouseEvent *event) override {
        setFocus();
        QWidget::mousePressEvent(event);
    }
};

struct LaserLevel {
    int entry;
    int exit;
    QList<QPoint> mirrors;
};
const std::array<LaserLevel, 6> laserLevels = {{{4, 4, {{2, 4}, {2, 1}, {6, 1}, {6, 4}}},
                                                {1, 2, {{1, 1}, {1, 4}, {5, 4}, {5, 2}}},
                                                {4, 1, {{2, 4}, {2, 0}, {5, 0}, {5, 3}, {7, 3}, {7, 1}}},
                                                {0, 3, {{1, 0}, {1, 4}, {3, 4}, {3, 1}, {6, 1}, {6, 3}}},
                                                {2, 3, {{2, 2}, {2, 5}, {4, 5}, {4, 0}, {6, 0}, {6, 3}}},
                                                {5, 0, {{1, 5}, {1, 1}, {3, 1}, {3, 4}, {7, 4}, {7, 0}}}}};
QPoint direction(QPoint delta) {
    return {delta.x() == 0 ? 0 : delta.x() > 0 ? 1 : -1, delta.y() == 0 ? 0 : delta.y() > 0 ? 1 : -1};
}
QPoint reflected(QPoint d, bool slash) { return slash ? QPoint(-d.y(), -d.x()) : QPoint(d.y(), d.x()); }

class Prism final : public Puzzle {
  public:
    Prism(SimSystem *system, QWidget *parent)
        : Puzzle(
              system, "PRISMA / laboratório de luz",
              "Clique num espelho para girar. Ou use 1–6. Leve o laser até o receptor à direita. R reinicia.",
              parent) {
        restart_ = control("Reiniciar nível", [this] { reset(false); });
        next_ = control("Próximo nível", [this] {
            if (won_) {
                level_ = (level_ + 1) % int(laserLevels.size());
                reset(false);
            }
        });
        const auto state = system_->setting("game/prism").toMap();
        level_ = qBound(0, state.value("level", 0).toInt(), int(laserLevels.size()) - 1);
        cleared_ = qBound(0, state.value("cleared", 0).toInt(), int(laserLevels.size()));
        reset(true);
        board_->draw = [this](QPainter &p, const QRect &r) { draw(p, r); };
        board_->clicked = [this](QPoint point) {
            for (int i = 0; i < mirrors().size(); ++i)
                if (cell(mirrors()[i]).contains(point)) {
                    rotate(i);
                    break;
                }
            setFocus();
        };
    }

  private:
    int level_ = 0, cleared_ = 0, moves_ = 0;
    QList<bool> slash_;
    QList<QPoint> beam_;
    bool won_ = false;
    QPushButton *restart_, *next_;
    const QList<QPoint> &mirrors() const { return laserLevels[level_].mirrors; }
    QRect grid() const {
        int size = qMin((board_->width() - 56) / 9, (board_->height() - 18) / 6);
        return {(board_->width() - size * 9) / 2, (board_->height() - size * 6) / 2, size * 9, size * 6};
    }
    QRect cell(QPoint point) const {
        QRect g = grid();
        int size = g.width() / 9;
        return {g.x() + point.x() * size, g.y() + point.y() * size, size, size};
    }
    void reset(bool restore) {
        slash_.clear();
        moves_ = 0;
        for (int i = 0; i < mirrors().size(); ++i) {
            QPoint incoming =
                direction(mirrors()[i] - (i ? mirrors()[i - 1] : QPoint(-1, laserLevels[level_].entry)));
            QPoint outgoing = direction(
                (i + 1 < mirrors().size() ? mirrors()[i + 1] : QPoint(9, laserLevels[level_].exit)) -
                mirrors()[i]);
            const bool solution = reflected(incoming, true) == outgoing;
            slash_.append(solution ^ ((i + level_) % 2 == 0));
        }
        if (restore) {
            auto state = system_->setting("game/prism").toMap();
            QString saved = state.value("mirrors").toString();
            if (saved.size() == slash_.size() && saved.remove('0').remove('1').isEmpty()) {
                const QString bits = state.value("mirrors").toString();
                for (int i = 0; i < bits.size(); ++i)
                    slash_[i] = bits[i] == '1';
                moves_ = qBound(0, state.value("moves", 0).toInt(), 99999);
            }
        }
        refresh();
        save();
    }
    void trace() {
        beam_.clear();
        QPoint at(-1, laserLevels[level_].entry), d(1, 0);
        beam_ << at;
        won_ = false;
        QSet<QString> seen;
        for (int steps = 0; steps < 400; ++steps) {
            at += d;
            beam_ << at;
            if (at.x() < 0 || at.x() > 8 || at.y() < 0 || at.y() > 5) {
                won_ = at == QPoint(9, laserLevels[level_].exit) && d == QPoint(1, 0);
                break;
            }
            QString key = QString("%1,%2,%3,%4").arg(at.x()).arg(at.y()).arg(d.x()).arg(d.y());
            if (seen.contains(key))
                break;
            seen.insert(key);
            int index = mirrors().indexOf(at);
            if (index >= 0)
                d = reflected(d, slash_[index]);
        }
    }
    void refresh() {
        trace();
        if (won_)
            cleared_ = qMax(cleared_, level_ + 1);
        status_->setText(
            won_ ? QString("Receptor aceso! %1 giros. %2")
                       .arg(moves_)
                       .arg(level_ == 5 ? "Todos os níveis concluídos!" : "Pode seguir ao próximo nível.")
                 : QString("Nível %1/6  |  giros %2  |  concluídos %3/6")
                       .arg(level_ + 1)
                       .arg(moves_)
                       .arg(cleared_));
        next_->setEnabled(won_);
        next_->setText(level_ == 5 ? "Novo ciclo" : "Próximo nível");
        board_->update();
    }
    void save() {
        QString bits;
        for (bool value : slash_)
            bits += value ? '1' : '0';
        system_->setSetting(
            "game/prism",
            QVariantMap{{"level", level_}, {"mirrors", bits}, {"moves", moves_}, {"cleared", cleared_}});
    }
    void rotate(int index) {
        if (index < 0 || index >= slash_.size() || won_)
            return;
        slash_[index] = !slash_[index];
        ++moves_;
        refresh();
        save();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_6)
            rotate(event->key() - Qt::Key_1);
        else if (event->key() == Qt::Key_R)
            reset(false);
        else if (event->key() == Qt::Key_Return && won_)
            next_->click();
        else
            Puzzle::keyPressEvent(event);
    }
    void draw(QPainter &p, const QRect &) {
        const QRect g = grid();
        int side = g.width() / 9;
        p.setPen(QColor("#35574b"));
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 9; ++x)
                p.drawRect(cell({x, y}));
        QPolygon ray;
        for (QPoint point : beam_)
            ray << cell(point).center();
        p.setPen(QPen(gold, 3));
        p.drawPolyline(ray);
        for (int i = 0; i < mirrors().size(); ++i) {
            QRect box = cell(mirrors()[i]).adjusted(3, 3, -3, -3);
            p.fillRect(box, leaf);
            p.setPen(QPen(ink, 3));
            if (slash_[i])
                p.drawLine(box.bottomLeft() + QPoint(4, -4), box.topRight() + QPoint(-4, 4));
            else
                p.drawLine(box.topLeft() + QPoint(4, 4), box.bottomRight() + QPoint(-4, -4));
            p.setPen(paper);
            p.drawText(box.adjusted(1, -1, 0, 0), Qt::AlignTop | Qt::AlignLeft, QString::number(i + 1));
        }
        QRect source = cell({-1, laserLevels[level_].entry});
        p.setPen(Qt::NoPen);
        p.setBrush(gold);
        p.drawPolygon(QPolygon({source.center() + QPoint(-7, -9), source.center() + QPoint(-7, 9),
                                source.center() + QPoint(8, 0)}));
        QRect receiver = cell({9, laserLevels[level_].exit});
        p.setBrush(won_ ? gold : ink);
        p.setPen(QPen(leaf, 2));
        p.drawRect(receiver.adjusted(side / 3, 5, -side / 3, -5));
    }
};

class Garden final : public Puzzle {
  public:
    Garden(SimSystem *system, QWidget *parent)
        : Puzzle(system, "JARDIM ORBITAL / estufa de bordo",
                 "Cada rega alterna a planta e suas vizinhas em cruz. Deixe todas verdes antes de acabar a "
                 "água. Clique ou setas + Espaço.",
                 parent) {
        control("Replantar / R", [this] { reset(false); });
        next_ = control("Próxima estufa", [this] {
            if (won_) {
                level_ = (level_ + 1) % 8;
                reset(false);
            }
        });
        const auto state = system_->setting("game/garden").toMap();
        level_ = qBound(0, state.value("level", 0).toInt(), 7);
        cleared_ = qBound(0, state.value("cleared", 0).toInt(), 8);
        reset(true);
        board_->draw = [this](QPainter &p, const QRect &r) { draw(p, r); };
        board_->clicked = [this](QPoint point) {
            QRect g = grid();
            if (g.contains(point)) {
                int size = g.width() / 5;
                selected_ = ((point.y() - g.y()) / size) * 5 + (point.x() - g.x()) / size;
                water();
            }
            setFocus();
        };
    }

  private:
    int level_ = 0, cleared_ = 0, water_ = 0, selected_ = 0;
    std::array<bool, 20> plants_{};
    bool won_ = false;
    QPushButton *next_;
    int budget() const { return level_ + 5; }
    QRect grid() const {
        int size = qMin((board_->width() - 20) / 5, (board_->height() - 16) / 4);
        return {(board_->width() - size * 5) / 2, (board_->height() - size * 4) / 2, size * 5, size * 4};
    }
    QRect cell(int index) const {
        QRect g = grid();
        int size = g.width() / 5;
        return {g.x() + index % 5 * size, g.y() + index / 5 * size, size, size};
    }
    void toggle(int index) {
        int x = index % 5, y = index / 5;
        for (QPoint d : QList<QPoint>{{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
            int xx = x + d.x(), yy = y + d.y();
            if (xx >= 0 && xx < 5 && yy >= 0 && yy < 4)
                plants_[yy * 5 + xx] = !plants_[yy * 5 + xx];
        }
    }
    void reset(bool restore) {
        plants_.fill(true);
        water_ = budget();
        for (int i = 0; i < level_ + 2; ++i)
            toggle((i * 7 + level_ * 3) % 20);
        if (std::all_of(plants_.begin(), plants_.end(), [](bool b) { return b; }))
            toggle(0);
        if (restore) {
            const auto state = system_->setting("game/garden").toMap();
            QString bits = state.value("plants").toString(), valid = bits;
            if (bits.size() == 20 && valid.remove('0').remove('1').isEmpty()) {
                for (int i = 0; i < 20; ++i)
                    plants_[i] = bits[i] == '1';
                water_ = qBound(0, state.value("water", budget()).toInt(), budget());
            }
        }
        refresh();
        save();
    }
    void refresh() {
        int count = std::count(plants_.begin(), plants_.end(), true);
        won_ = count == 20;
        if (won_)
            cleared_ = qMax(cleared_, level_ + 1);
        status_->setText(won_ ? QString("Estufa restaurada! Água restante: %1. %2")
                                    .arg(water_)
                                    .arg(level_ == 7 ? "Missão completa!" : "Próxima estufa liberada.")
                         : water_ == 0 ? "Sem água. Replante para tentar outra estratégia."
                                       : QString("Estufa %1/8  |  água %2  |  plantas verdes %3/20")
                                             .arg(level_ + 1)
                                             .arg(water_)
                                             .arg(count));
        next_->setEnabled(won_);
        next_->setText(level_ == 7 ? "Nova missão" : "Próxima estufa");
        board_->update();
    }
    void save() {
        QString bits;
        for (bool b : plants_)
            bits += b ? '1' : '0';
        system_->setSetting(
            "game/garden",
            QVariantMap{{"level", level_}, {"cleared", cleared_}, {"plants", bits}, {"water", water_}});
    }
    void water() {
        if (won_ || water_ == 0)
            return;
        toggle(selected_);
        --water_;
        refresh();
        save();
    }
    void keyPressEvent(QKeyEvent *event) override {
        int key = event->key();
        if (key == Qt::Key_Left)
            selected_ = selected_ % 5 ? selected_ - 1 : selected_;
        else if (key == Qt::Key_Right)
            selected_ = selected_ % 5 < 4 ? selected_ + 1 : selected_;
        else if (key == Qt::Key_Up)
            selected_ = selected_ >= 5 ? selected_ - 5 : selected_;
        else if (key == Qt::Key_Down)
            selected_ = selected_ < 15 ? selected_ + 5 : selected_;
        else if (key == Qt::Key_Space)
            water();
        else if (key == Qt::Key_R)
            reset(false);
        else if ((key == Qt::Key_Return || key == Qt::Key_Enter) && won_)
            next_->click();
        else
            Puzzle::keyPressEvent(event);
        board_->update();
    }
    void draw(QPainter &p, const QRect &) {
        for (int i = 0; i < 20; ++i) {
            QRect r = cell(i).adjusted(3, 3, -3, -3);
            p.fillRect(r, plants_[i] ? QColor("#294d3d") : QColor("#3d3626"));
            p.setPen(QPen(i == selected_ ? paper : leaf, i == selected_ ? 3 : 1));
            p.drawRect(r);
            QPoint c = r.center();
            int size = r.width() / 5;
            p.setPen(QPen(plants_[i] ? leaf : gold, 3));
            p.drawLine(c + QPoint(0, size), c - QPoint(0, size));
            p.drawLine(c, c + QPoint(-size, -size));
            p.drawLine(c, c + QPoint(size, -size));
            if (plants_[i]) {
                p.fillRect(QRect(c + QPoint(-size - 3, -size - 2), QSize(7, 5)), leaf);
                p.fillRect(QRect(c + QPoint(size - 3, -size - 2), QSize(7, 5)), leaf);
            }
            p.setPen(gold);
            p.drawLine(c + QPoint(-size, size + 4), c + QPoint(size, size + 4));
        }
    }
};

class Echo final : public Puzzle {
  public:
    Echo(SimSystem *system, QWidget *parent)
        : Puzzle(system, "ECO / arquivo de sinais",
                 "Observe e ouça a sequência. Depois repita com 1–4 ou clique nos sinais. R repete a "
                 "sequência; Enter inicia outra partida.",
                 parent) {
        speaker_ = new Speaker(system, this);
        best_ = qMax(0, system_->setting("game/echoBest", 0).toInt());
        control("Nova partida", [this] { start(); });
        replay_ = control("Repetir sinal / R", [this] {
            if (!sequence_.isEmpty())
                demonstrate();
        });
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
        board_->draw = [this](QPainter &p, const QRect &r) { draw(p, r); };
        board_->clicked = [this](QPoint point) {
            for (int i = 0; i < 4; ++i)
                if (pad(i).contains(point))
                    answer(i);
            setFocus();
        };
        refresh();
    }

  protected:
    void hideEvent(QHideEvent *event) override {
        remaining_ = timer_.isActive() ? qMax(1, timer_.remainingTime()) : -1;
        timer_.stop();
        Puzzle::hideEvent(event);
    }
    void showEvent(QShowEvent *event) override {
        Puzzle::showEvent(event);
        if (remaining_ > 0) {
            timer_.start(remaining_);
            remaining_ = -1;
        }
    }

  private:
    enum Stage { Ready, Gap, Flash, Input, Feedback, RoundWon, Failure };
    Stage stage_ = Ready;
    Speaker *speaker_;
    QTimer timer_;
    QPushButton *replay_;
    QList<int> sequence_;
    int index_ = 0, active_ = -1, best_ = 0, completed_ = 0, remaining_ = -1;
    QRect pad(int index) const {
        int width = qMin(150, (board_->width() - 36) / 2), height = qMin(100, (board_->height() - 28) / 2);
        int x = (board_->width() - 2 * width - 12) / 2, y = (board_->height() - 2 * height - 12) / 2;
        return {x + (index % 2) * (width + 12), y + (index / 2) * (height + 12), width, height};
    }
    void start() {
        timer_.stop();
        sequence_ = {QRandomGenerator::global()->bounded(4)};
        completed_ = 0;
        demonstrate();
    }
    void later(int delay) {
        if (isVisible())
            timer_.start(delay);
        else
            remaining_ = delay;
    }
    void demonstrate() {
        timer_.stop();
        index_ = 0;
        active_ = -1;
        stage_ = Gap;
        later(450);
        refresh();
    }
    void tone(int index) { speaker_->play("echo" + QString::number(index + 1)); }
    void tick() {
        if (!isVisible())
            return;
        switch (stage_) {
        case Gap:
            if (index_ < sequence_.size()) {
                active_ = sequence_[index_];
                stage_ = Flash;
                tone(active_);
                later(450);
            } else {
                active_ = -1;
                index_ = 0;
                stage_ = Input;
            }
            break;
        case Flash:
            active_ = -1;
            ++index_;
            stage_ = Gap;
            later(180);
            break;
        case Feedback:
            active_ = -1;
            if (index_ == sequence_.size()) {
                completed_ = sequence_.size();
                if (completed_ > best_) {
                    best_ = completed_;
                    system_->setSetting("game/echoBest", best_);
                }
                stage_ = RoundWon;
                later(650);
            } else
                stage_ = Input;
            break;
        case RoundWon:
            if (sequence_.size() >= 64) {
                stage_ = Ready;
                sequence_.clear();
            } else {
                sequence_.append(QRandomGenerator::global()->bounded(4));
                demonstrate();
            }
            break;
        default:
            break;
        }
        refresh();
    }
    void answer(int value) {
        if (stage_ != Input)
            return;
        active_ = value;
        tone(value);
        if (value != sequence_[index_]) {
            stage_ = Failure;
            speaker_->play("error");
            timer_.stop();
        } else {
            ++index_;
            stage_ = Feedback;
            later(180);
        }
        refresh();
    }
    void refresh() {
        QString text;
        if (stage_ == Ready)
            text = QString("Pronto para transmitir? Recorde: %1 sinais.").arg(best_);
        else if (stage_ == Failure)
            text = QString("Sinal trocado! Completou %1. Recorde %2. R tenta a mesma sequência.")
                       .arg(completed_)
                       .arg(best_);
        else if (stage_ == Input || stage_ == Feedback)
            text = QString("Sua vez: %1/%2  |  rodadas %3  |  recorde %4")
                       .arg(qMin(index_ + 1, int(sequence_.size())))
                       .arg(sequence_.size())
                       .arg(completed_)
                       .arg(best_);
        else if (stage_ == RoundWon)
            text = QString("Sequência correta! %1 sinais. Preparando o próximo...").arg(completed_);
        else
            text = QString("Observe os %1 sinais...  |  recorde %2").arg(sequence_.size()).arg(best_);
        status_->setText(text);
        replay_->setEnabled(stage_ == Input || stage_ == Failure);
        board_->update();
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->isAutoRepeat())
            return;
        int key = event->key();
        if (key >= Qt::Key_1 && key <= Qt::Key_4)
            answer(key - Qt::Key_1);
        else if (key == Qt::Key_R) {
            if (stage_ == Input || stage_ == Failure)
                demonstrate();
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter)
            start();
        else
            Puzzle::keyPressEvent(event);
    }
    void draw(QPainter &p, const QRect &) {
        std::array<QColor, 4> colors = {leaf, gold, QColor("#7ba6a0"), paper};
        for (int i = 0; i < 4; ++i) {
            QRect r = pad(i);
            bool lit = active_ == i;
            p.fillRect(r, lit ? colors[i] : QColor("#29433b"));
            p.setPen(QPen(colors[i], lit ? 4 : 2));
            p.drawRect(r);
            p.setPen(lit ? ink : colors[i]);
            QFont font = this->font();
            font.setBold(true);
            font.setPixelSize(qMax(20, font.pixelSize() + 6));
            p.setFont(font);
            p.drawText(r, Qt::AlignCenter, QString::number(i + 1));
        }
        if (stage_ == Failure) {
            p.setPen(gold);
            p.drawLine(pad(active_).topLeft(), pad(active_).bottomRight());
        }
        if (stage_ == Ready) {
            p.fillRect(QRect(18, board_->height() / 2 - 20, board_->width() - 36, 40), ink);
            p.setPen(paper);
            p.setFont(font());
            p.drawText(QRect(18, board_->height() / 2 - 20, board_->width() - 36, 40), Qt::AlignCenter,
                       "ENTER / NOVA PARTIDA");
        }
    }
};
} // namespace
QWidget *createPrismGame(SimSystem *system, QWidget *parent) { return new Prism(system, parent); }
QWidget *createGardenGame(SimSystem *system, QWidget *parent) { return new Garden(system, parent); }
QWidget *createEchoGame(SimSystem *system, QWidget *parent) { return new Echo(system, parent); }
