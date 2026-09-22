#ifndef BEHAVIOROVERLAYITEM_H
#define BEHAVIOROVERLAYITEM_H

// CUSTOM ENGINE: the map-canvas overlay of the main window's "Behaviors" tab. It shows, on top of the map, the behavior every field of the
// pre-map has as the small coloured icon with its two hex digits from the project's behavior sheet (graphics/porymap/behavior_sheet.png, see
// ProjectSheets). What a field shows is decided by the pre-map item (PreMapPixmapItem::effectiveBehaviorAt): a behavior PLACED on the
// Behaviors tab wins, otherwise the field takes the behavior of its porytiles (the topmost visible layer whose porytile has a behavior other
// than 0; layers hidden with their eye toggle are left out). EVERY field shows its behavior, 0x00 (MB_NORMAL) included, in its own
// colour with its two digits -- like every field of the Elevation tab shows its number. A placed behavior is drawn with a white frame
// so it can be told from an inherited one. How strongly the numbers cover the map is the item's opacity (the tab's Opacity slider).
// Purely a view: it never accepts the mouse -- the pre-map item underneath takes the pencil, the bucket and the eyedropper of the tab.

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QObject>
#include <cstdint>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <functional>

class Layout;
class PreMap;
class PreMapPixmapItem;

class BehaviorOverlayItem : public QObject, public QGraphicsPixmapItem {
    Q_OBJECT
public:
    BehaviorOverlayItem(Layout *layout, PreMap *preMap, PreMapPixmapItem *layers, const QMap<QString, uint32_t> *behaviorIdByName)
        : QObject(nullptr), layout(layout), preMap(preMap), layers(layers), behaviorIdByName(behaviorIdByName) {
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(false);
        setVisible(false);
    }

    // The 16x16-cell behavior sheet the icons are cut from (owned by the Editor).
    void setSheet(const QImage *sheet) { this->sheet = sheet; }

    // Inactive = hidden and not drawn (the other tabs); activating draws it.
    void setActive(bool active);
    bool isActive() const { return this->active; }

    // The frame the pencil shows on the field under the cursor. It is a child of this item (so it is painted ABOVE the numbers, which cover
    // every field) and ignores the item's opacity. (-1, -1) hides it.
    void setHoverField(const QPoint &field);
    QGraphicsRectItem *hoverFrame() const { return this->frame; }

    void draw();                              // everything
    void drawRegion(const QRect &mapPixels);  // only the fields under these map pixels (a stroke of the pencil)

    // The behavior shown at a map pixel (0 = none).
    uint32_t behaviorIdAt(const QPoint &mapPixel) const;

    // Number of icons drawn by the last full draw().
    int cellCount() const { return this->lastCellCount; }

private:
    // Calls visit(cellRect, behaviorId, placed) for every field that shows a behavior (placed 0x00 included), in reading order.
    void forEachCell(const QRect &fields, const std::function<void(const QRect &, uint32_t, bool)> &visit) const;
    uint32_t shownAt(int x, int y) const;
    void paintCells(const QRect &fields);

    Layout *layout;
    PreMap *preMap;
    PreMapPixmapItem *layers;                              // decides what a field shows (eye flags, placed behaviors)
    const QMap<QString, uint32_t> *behaviorIdByName;
    const QImage *sheet = nullptr;
    QImage image;
    QGraphicsRectItem *frame = nullptr;   // (a child: deleted with this item)
    bool active = false;
    int lastCellCount = 0;
};

#endif // BEHAVIOROVERLAYITEM_H
