#ifndef TILESETDIVIDERITEM_H
#define TILESETDIVIDERITEM_H

// CUSTOM ENGINE: the line between the primary and the secondary tileset on a sheet -- in the Tileset Editor (the Paint and Behavior pages of
// both tabs and the source tiles) and in the main window (the porytile palette of the Porymap view, the metatile palette of the Finalmap).
// It is an item of its own on top of the sheet, not part of the sheet's picture: a cosmetic pen keeps it exactly as thick ON SCREEN at every
// zoom (a line baked into the pixmap shrinks with the zoom and is hardly there when the sheet is zoomed out), and no redraw of the sheet can
// paint over it.

#include <QGraphicsLineItem>
#include <QPainter>
#include <QPen>
#include <QColor>

class TilesetDividerItem : public QGraphicsLineItem {
public:
    static int thickness() { return 4; }   // logical screen pixels (on a 2x display: 8 device pixels)
    static QColor color() { return QColor(225, 25, 25); }

    explicit TilesetDividerItem(QGraphicsItem *parent) : QGraphicsLineItem(parent) {
        QPen pen(color(), thickness());
        pen.setCosmetic(true);
        pen.setCapStyle(Qt::FlatCap);
        setPen(pen);
        setZValue(20);   // above the marks, the brush ghost and the frames of the sheet
        setAcceptedMouseButtons(Qt::NoButton);
        hide();
    }

    // `y`: the pixel row of the sheet where the secondary tileset starts; `width`: the width of the sheet.
    void place(qreal y, qreal width, bool show) {
        prepareGeometryChange();   // (the bounding rect depends on the size of the sheet below, which may have changed while the line did not)
        setLine(0, y, width, y);
        setVisible(show && width > 0);
    }

    // (a cosmetic pen is `thickness()` screen pixels wide, which is more item units than the line's own rectangle when the view is zoomed out, so the
    // rectangle is widened -- but never beyond the sheet it lies on: a scene that sizes itself from its items must not grow, and a view without
    // scroll bars must not shift, because of a line at the edge of a small sheet)
    QRectF boundingRect() const override {
        QRectF box = QGraphicsLineItem::boundingRect().adjusted(0, -8 * thickness(), 0, 8 * thickness());
        if (parentItem())
            box = box.intersected(parentItem()->boundingRect());
        return box;
    }

    // A cosmetic pen counts DEVICE pixels: on a 2x display it would be half as thick as on a 1x display, so the width follows the device pixel ratio.
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override {
        QPen p = pen();
        p.setWidthF(thickness() * painter->device()->devicePixelRatioF());
        painter->setPen(p);
        painter->drawLine(line());
    }
};

#endif // TILESETDIVIDERITEM_H
