#include "actionbutton.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QStyleOptionButton>

namespace {

constexpr int IconSize = 17;
constexpr int IconGap = 6;
constexpr int SidePadding = 8;

struct PaintColors {
    QColor top;
    QColor bottom;
    QColor border;
    QColor text;
    QColor icon;
    QColor detail;
};

PaintColors colors(ActionButton::Tone tone, bool enabled, bool hovered, bool pressed)
{
    using Tone = ActionButton::Tone;
    if (!enabled) {
        return {QColor("#f5f7fa"), QColor("#ebeff4"), QColor("#dfe5ed"),
                QColor("#4f6176"), QColor("#60768e"), QColor("#dce5ef")};
    }

    const bool active = hovered || pressed;
    if (tone == Tone::Primary) {
        return {QColor(pressed ? "#0a3b68" : active ? "#225e94" : "#1b5285"),
                QColor(active ? "#125a69" : "#166878"), QColor("#164f73"),
                Qt::white, Qt::white, QColor(255, 255, 255, 42)};
    }
    if (tone == Tone::Stop) {
        return {QColor("#fffaf6"), QColor(pressed ? "#f6d7c9" : active ? "#fae8df" : "#f9eee8"),
                QColor("#edcec0"), QColor("#783c2d"), QColor("#a44635"), QColor("#eec8b6")};
    }
    if (tone == Tone::Shortcut) {
        return {QColor(active ? "#f7fbff" : "#fcfdff"),
                QColor(pressed ? "#d3e4f6" : active ? "#e3effb" : "#ecf4fa"),
                QColor(active ? "#a4bfdc" : "#d5e3ef"), QColor("#214668"),
                QColor("#24658a"), QColor("#d2e4f2")};
    }

    return {QColor("#ffffff"), QColor(pressed ? "#dce9f4" : active ? "#e9f3fb" : "#f2f6fa"),
            QColor(active ? "#a7bfd4" : "#d3dfea"), QColor("#243f57"),
            QColor("#366082"), QColor("#dde9f5")};
}

void drawContours(QPainter &painter, const QRectF &box, const QPainterPath &shape, const QColor &color)
{
    painter.save();
    painter.setClipPath(shape);
    painter.setPen(QPen(color, 1.0));
    painter.setBrush(Qt::NoBrush);

    const qreal edge = box.right();
    const qreal baseline = box.bottom();
    for (int offset : {0, 5, 10}) {
        QPainterPath contour;
        contour.moveTo(edge - 61 + offset, baseline + 7);
        contour.cubicTo(edge - 43 + offset, baseline - 8, edge - 25 + offset, baseline - 4,
                        edge - 14 + offset, baseline - 17);
        contour.cubicTo(edge - 7 + offset, baseline - 25, edge, baseline - 23,
                        edge + 7, baseline - 29 + offset);
        painter.drawPath(contour);
    }
    painter.restore();
}

void drawGlyph(QPainter &painter, ActionButton::Glyph glyph, const QRectF &area, const QColor &color)
{
    using Glyph = ActionButton::Glyph;
    painter.save();
    painter.translate(area.topLeft());
    painter.scale(area.width() / 24, area.height() / 24);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    switch (glyph) {
    case Glyph::Start:
        painter.setBrush(color);
        painter.drawPolygon(QPolygonF({{8, 4}, {19, 12}, {8, 20}}));
        break;
    case Glyph::Stop:
        painter.setBrush(color);
        painter.drawRoundedRect(QRectF(6, 6, 12, 12), 2, 2);
        break;
    case Glyph::Add:
        painter.drawRoundedRect(QRectF(4, 4, 16, 16), 3, 3);
        painter.drawLine(QPointF(8, 12), QPointF(16, 12));
        painter.drawLine(QPointF(12, 8), QPointF(12, 16));
        break;
    case Glyph::Folder:
        painter.drawPolygon(QPolygonF({{3, 6}, {10, 6}, {12, 8}, {21, 8}, {21, 19}, {3, 19}}));
        break;
    case Glyph::Remove:
        painter.drawLine(QPointF(6, 12), QPointF(18, 12));
        break;
    case Glyph::Send:
        painter.drawPolygon(QPolygonF({{3, 10}, {21, 4}, {16, 21}, {11, 14}}));
        painter.drawLine(QPointF(11, 14), QPointF(21, 4));
        break;
    case Glyph::Video:
        painter.drawRoundedRect(QRectF(3, 5, 18, 14), 3, 3);
        painter.drawPolygon(QPolygonF({{10, 9}, {15, 12}, {10, 15}}));
        break;
    case Glyph::Image:
        painter.drawRoundedRect(QRectF(3, 4, 18, 16), 3, 3);
        painter.drawEllipse(QPointF(8, 9), 1.3, 1.3);
        painter.drawPolyline(QPolygonF({{5, 17}, {10, 13}, {13, 15}, {16, 11}, {20, 16}}));
        break;
    case Glyph::Notice:
        painter.drawRoundedRect(QRectF(5, 4, 14, 16), 2, 2);
        painter.drawLine(QPointF(8, 9), QPointF(16, 9));
        painter.drawLine(QPointF(8, 13), QPointF(16, 13));
        painter.drawLine(QPointF(8, 16), QPointF(13, 16));
        break;
    case Glyph::Cycle:
        painter.drawArc(QRectF(5, 5, 14, 14), 28 * 16, 140 * 16);
        painter.drawArc(QRectF(5, 5, 14, 14), 207 * 16, 140 * 16);
        painter.drawPolyline(QPolygonF({{15, 8.7}, {18.6, 8.7}, {18.6, 5}}));
        painter.drawPolyline(QPolygonF({{9, 15.3}, {5.4, 15.3}, {5.4, 19}}));
        break;
    case Glyph::Clear:
        painter.drawLine(QPointF(4, 7), QPointF(20, 7));
        painter.drawLine(QPointF(9, 4), QPointF(15, 4));
        painter.drawPolyline(QPolygonF({{6, 9}, {7, 20}, {17, 20}, {18, 9}}));
        painter.drawLine(QPointF(10, 11), QPointF(10, 17));
        painter.drawLine(QPointF(14, 11), QPointF(14, 17));
        break;
    }
    painter.restore();
}

}

ActionButton::ActionButton(const QString &text, Glyph glyph, QWidget *parent, Tone tone)
    : QPushButton(text, parent), m_glyph(glyph), m_tone(tone)
{
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

QSize ActionButton::sizeHint() const
{
    const int width = 2 * SidePadding + IconSize + IconGap + fontMetrics().horizontalAdvance(text());
    const int padding = m_tone == Tone::Shortcut ? 20 : 13;
    return QSize(width, qMax(35, fontMetrics().height() + padding));
}

QSize ActionButton::minimumSizeHint() const
{
    return sizeHint();
}

void ActionButton::paintEvent(QPaintEvent *)
{
    QStyleOptionButton option;
    initStyleOption(&option);
    const bool pressed = option.state.testFlag(QStyle::State_Sunken);
    const auto palette = colors(m_tone, isEnabled(), underMouse(), pressed);

    QPainter painter(this);
    painter.setFont(font());
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF box = QRectF(rect()).adjusted(.5, .5, -.5, -1.5);
    QPainterPath shape;
    shape.addRoundedRect(box, 7, 7);
    QLinearGradient background(box.topLeft(), box.bottomRight());
    background.setColorAt(0, palette.top);
    background.setColorAt(1, palette.bottom);
    painter.fillPath(shape, background);
    if (m_tone != Tone::Quiet) {
        drawContours(painter, box, shape, palette.detail);
    }
    painter.setPen(QPen(palette.border, 1));
    painter.drawPath(shape);

    if (option.state.testFlag(QStyle::State_HasFocus)) {
        painter.setPen(QPen(isEnabled() && m_tone == Tone::Primary
                               ? QColor("#d8efff") : QColor("#2c629e"), 1.5));
        painter.drawRoundedRect(box.adjusted(2, 2, -2, -2), 5, 5);
    }

    const int labelWidth = fontMetrics().horizontalAdvance(text());
    const qreal left = (width() - IconSize - IconGap - labelWidth) / 2.0;
    const qreal shift = pressed ? 1 : 0;
    const QRectF icon(left, (height() - IconSize) / 2.0 + shift, IconSize, IconSize);
    drawGlyph(painter, m_glyph, icon, palette.icon);
    painter.setPen(palette.text);
    const QRectF label(left + IconSize + IconGap, shift, labelWidth + 2, height() - 1);
    painter.drawText(label, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text());
}
