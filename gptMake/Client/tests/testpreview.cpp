#include "testpreview.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>

bool createLandscape(const QString &path, bool alternate)
{
    QImage image(1200, 675, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    QLinearGradient sky(0, 0, 0, 440);
    sky.setColorAt(0, QColor(alternate ? "#567e92" : "#648e92"));
    sky.setColorAt(.53, QColor("#a9c8b5"));
    sky.setColorAt(1, QColor("#f1d196"));
    painter.fillRect(image.rect(), sky);

    const QPointF sun(alternate ? 370 : 840, 175);
    QRadialGradient sunlight(sun, 100);
    sunlight.setColorAt(0, QColor(255, 235, 163, 220));
    sunlight.setColorAt(1, QColor(255, 228, 167, 0));
    painter.setBrush(sunlight);
    painter.drawEllipse(sun, 100, 100);
    painter.setBrush(QColor("#f7dea6"));
    painter.drawEllipse(sun, 36, 36);

    const auto ridge = [&painter](const QPolygonF &vertices, const QColor &top, const QColor &bottom, int topY) {
        QPainterPath shape;
        shape.addPolygon(vertices);
        shape.closeSubpath();
        QLinearGradient fill(0, topY, 0, 510);
        fill.setColorAt(0, top);
        fill.setColorAt(1, bottom);
        painter.fillPath(shape, fill);
    };
    ridge(QPolygonF(QVector<QPointF>{{0, 320}, {116, 246}, {208, 267}, {339, 200}, {467, 250},
              {531, 231}, {691, 295}, {800, 264}, {946, 306}, {1200, 241}, {1200, 580}, {0, 580}}),
          QColor("#749b93"), QColor("#bdd0b0"), 200);
    ridge(QPolygonF(QVector<QPointF>{{0, 383}, {120, 289}, {260, 332}, {489, 388}, {674, 370},
              {813, 337}, {969, 214}, {1088, 273}, {1200, 218}, {1200, 560}, {0, 560}}),
          QColor("#507d76"), QColor("#93b7a5"), 215);
    ridge(QPolygonF(QVector<QPointF>{{0, 321}, {104, 336}, {188, 386}, {305, 414}, {519, 460},
              {772, 450}, {938, 406}, {1102, 331}, {1200, 322}, {1200, 580}, {0, 580}}),
          QColor("#355f5c"), QColor("#6c998a"), 320);

    QPainterPath water;
    water.moveTo(550, 443);
    water.cubicTo(646, 441, 690, 455, 774, 452);
    water.cubicTo(882, 448, 903, 470, 1032, 476);
    water.lineTo(1200, 492);
    water.lineTo(1200, 675);
    water.lineTo(117, 675);
    water.cubicTo(363, 632, 378, 550, 648, 521);
    water.cubicTo(823, 501, 738, 476, 621, 464);
    water.closeSubpath();
    QLinearGradient river(0, 440, 0, 675);
    river.setColorAt(0, QColor("#aac2a5"));
    river.setColorAt(.36, QColor("#699d91"));
    river.setColorAt(1, QColor("#396c6b"));
    painter.fillPath(water, river);

    painter.save();
    painter.setClipPath(water);
    QRandomGenerator random(147);
    for (int i = 0; i < 280; ++i) {
        const qreal y = 454 + random.bounded(221);
        const qreal x = random.bounded(1200);
        const int alpha = 18 + random.bounded(45);
        painter.setPen(QPen(QColor(225, 240, 205, alpha), y > 560 ? 1.4 : .9, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(x, y), QPointF(x + 9 + random.bounded(56), y));
    }
    painter.restore();

    QPainterPath foreground;
    foreground.moveTo(0, 474);
    foreground.cubicTo(100, 472, 150, 530, 241, 533);
    foreground.cubicTo(319, 540, 411, 566, 343, 586);
    foreground.cubicTo(230, 612, 206, 640, 118, 675);
    foreground.lineTo(0, 675);
    foreground.closeSubpath();
    painter.fillPath(foreground, QColor("#244b48"));

    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 42; ++i) {
        const int x = random.bounded(315);
        const int base = 534 + x / 5 + random.bounded(50);
        const int height = 18 + random.bounded(63);
        painter.setBrush(QColor(25 + random.bounded(16), 55 + random.bounded(18), 51 + random.bounded(14)));
        for (int layer = 0; layer < 4; ++layer) {
            const int y = base - height + layer * height / 6;
            const int width = height / 9 + layer * 2;
            painter.drawPolygon(QPolygon(QVector<QPoint>{{x, y}, {x - width, y + height / 3},
                                                         {x + width, y + height / 3}}));
        }
    }
    painter.end();
    return image.save(path);
}
