#include "screensaver.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRandomGenerator>
#include <QShowEvent>

namespace {
constexpr int StarCount = 90;
constexpr double LogoWidth = 0.42;  // fraction of the widget width
constexpr double LogoHeight = 0.16;
} // namespace

ScreenSaver::ScreenSaver(QWidget *parent) : QWidget(parent)
{
    setObjectName("screenSaver");
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::BlankCursor);
    setStyleSheet("background:#0b1514;");
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &ScreenSaver::advance);
    m_clock.start();
    seedStars();
}

void ScreenSaver::seedStars()
{
    m_stars.clear();
    for (int i = 0; i < StarCount; ++i) {
        Star star;
        star.x = QRandomGenerator::global()->generateDouble();
        star.y = QRandomGenerator::global()->generateDouble();
        star.speed = 0.0015 + QRandomGenerator::global()->generateDouble() * 0.006;
        star.size = 1 + int(QRandomGenerator::global()->bounded(3));
        m_stars.append(star);
    }
}

void ScreenSaver::start()
{
    if (!parentWidget())
        return;
    seedStars();
    m_logoX = 0.35;
    m_logoY = 0.4;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    m_timer.start();
}

void ScreenSaver::stop()
{
    m_timer.stop();
    hide();
    unsetCursor();
}

void ScreenSaver::showEvent(QShowEvent *event)
{
    if (m_stars.isEmpty())
        seedStars();
    QWidget::showEvent(event);
}

void ScreenSaver::advance()
{
    const double step = m_timer.interval() / 1000.0;
    for (auto &star : m_stars) {
        star.x -= star.speed * step * 40;
        if (star.x < 0) {
            star.x = 1.0;
            star.y = QRandomGenerator::global()->generateDouble();
            star.speed = 0.0015 + QRandomGenerator::global()->generateDouble() * 0.006;
        }
    }
    const double width = qMax(1, this->width());
    const double height = qMax(1, this->height());
    m_logoX += m_logoDX * step * 40;
    m_logoY += m_logoDY * step * 40;
    if (m_logoX < 0.02) {
        m_logoX = 0.02;
        m_logoDX = -m_logoDX;
    }
    if (m_logoX + LogoWidth > 0.98) {
        m_logoX = 0.98 - LogoWidth;
        m_logoDX = -m_logoDX;
    }
    if (m_logoY < 0.04) {
        m_logoY = 0.04;
        m_logoDY = -m_logoDY;
    }
    if (m_logoY + LogoHeight > 0.94) {
        m_logoY = 0.94 - LogoHeight;
        m_logoDY = -m_logoDY;
    }
    Q_UNUSED(width);
    Q_UNUSED(height);
    update();
}

void ScreenSaver::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#0b1514"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#243a36"));
    for (const auto &star : m_stars) {
        painter.setBrush(star.size == 1 ? QColor("#3d5a52")
                                        : star.size == 2 ? QColor("#6c8a76") : QColor("#b6cc95"));
        painter.drawEllipse(QPointF(star.x * width(), star.y * height()), star.size, star.size);
    }
    const QRectF logo(m_logoX * width(), m_logoY * height(), LogoWidth * width(), LogoHeight * height());
    QFont font(QStringLiteral("DejaVu Sans Mono"));
    font.setPixelSize(qMax(16, int(logo.height() * 0.5)));
    font.setLetterSpacing(QFont::PercentageSpacing, 130);
    painter.setFont(font);
    painter.setPen(QColor("#b6cc95"));
    painter.drawText(logo, Qt::AlignCenter, "AURORA / 86");
    QFont small(QStringLiteral("DejaVu Sans Mono"));
    small.setPixelSize(qMax(10, int(logo.height() * 0.22)));
    painter.setFont(small);
    painter.setPen(QColor("#547361"));
    painter.drawText(QRectF(logo.x(), logo.y() + logo.height() * 0.72, logo.width(), logo.height() * 0.4),
                     Qt::AlignCenter, "A16 PERSONAL WORKSTATION / 1986");
    // The classic hint in the corner.
    painter.setPen(QColor("#3d5a52"));
    painter.drawText(QRectF(0, height() - 34, width() - 24, 24), Qt::AlignRight | Qt::AlignVCenter,
                     "PRESS ANY KEY");
}

void ScreenSaver::keyPressEvent(QKeyEvent *event)
{
    event->accept();
    emit dismissed();
}

void ScreenSaver::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    emit dismissed();
}

void ScreenSaver::mouseMoveEvent(QMouseEvent *event)
{
    event->accept();
    if (m_clock.elapsed() > 400)
        emit dismissed();
}
