#ifndef METATILESELECTOR_H
#define METATILESELECTOR_H

#include <QPair>
#include "selectablepixmapitem.h"
#include "tilesetdivideritem.h"
#include "map.h"
#include "tileset.h"
#include "maplayout.h"

struct MetatileSelectionItem
{
    bool enabled;
    uint16_t metatileId;

    // Default values + compatibility with older compilers
    MetatileSelectionItem(bool enabled_ = false, uint16_t metatileId_ = 0)
        : enabled(enabled_), metatileId(metatileId_) {}
};

struct CollisionSelectionItem
{
    bool enabled;
    uint16_t collision;
    uint16_t elevation;

    // Default values + compatibility with older compilers
    CollisionSelectionItem(bool enabled_ = false, uint16_t collision_ = 0, uint16_t elevation_ = 0)
        : enabled(enabled_), collision(collision_), elevation(elevation_) {}
};

struct MetatileSelection
{
    QSize dimensions;
    bool hasCollision;
    QList<MetatileSelectionItem> metatileItems;
    QList<CollisionSelectionItem> collisionItems;
};

class MetatileSelector: public SelectablePixmapItem {
    Q_OBJECT
public:
    // CUSTOM ENGINE: kind = which blocks the palette shows (metatiles for the Finalmap, porytiles for the Porymap view); the ids of a selection
    // are ids of that kind (the same id space).
    MetatileSelector(int numMetatilesWide, Layout *layout, BlockKind kind = BlockKind::Metatile)
        : SelectablePixmapItem(Metatile::pixelSize()),
          numMetatilesWide(qMax(numMetatilesWide, 1)),
          kind(kind)
    {
        this->externalSelection = false;
        this->prefabSelection = false;
        this->layout = layout;
        this->selection = MetatileSelection{};
        this->cellPos = QPoint(-1, -1);
        setAcceptHoverEvents(true);
    }

    QSize getSelectionDimensions() const override;
    void draw() override;
    void refresh();
    // The red line between the primary and the secondary tileset (the same one the Tileset Editor's sheets have; View > Show Tileset Divider there
    // switches it): an item on top of the palette, as thick on screen at every zoom.
    TilesetDividerItem *dividerLine() const { return this->dividerItem; }

    bool select(uint16_t metatile);
    void selectFromMap(uint16_t metatileId, uint16_t collision, uint16_t elevation);
    MetatileSelection getMetatileSelection() const { return this->selection; }
    void setPrefabSelection(MetatileSelection selection);
    void setExternalSelection(int, int, const QList<uint16_t>&, const QList<QPair<uint16_t, uint16_t>>&);
    QPoint getMetatileIdCoordsOnWidget(uint16_t metatileId) const;
    void setLayout(Layout *layout);
    BlockKind blockKind() const { return this->kind; }
    bool blockIsValid(uint16_t id) const { return Tileset::blockIsValid(this->kind, id, primaryTileset(), secondaryTileset()); }
    bool isInternalSelection() const { return (!this->externalSelection && !this->prefabSelection); }

    Tileset *primaryTileset() const { return this->layout->tileset_primary; }
    Tileset *secondaryTileset() const { return this->layout->tileset_secondary; }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent*) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent*) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent*) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;
    void drawSelection() override;
private:
    const int numMetatilesWide;
    const BlockKind kind;
    QPixmap basePixmap;
    bool externalSelection;
    bool prefabSelection;
    Layout *layout;
    int externalSelectionWidth;
    int externalSelectionHeight;
    QList<uint16_t> externalSelectedMetatiles;
    MetatileSelection selection;
    QPoint cellPos;
    TilesetDividerItem *dividerItem = nullptr;   // created on the first draw

    void updateBasePixmap();
    void updateDivider();
    void updateSelectedMetatiles();
    void updateExternalSelectedMetatiles();
    uint16_t posToMetatileId(int x, int y, bool *ok = nullptr) const;
    uint16_t posToMetatileId(const QPoint &pos, bool *ok = nullptr) const;
    QPoint metatileIdToPos(uint16_t metatileId, bool *ok = nullptr) const;
    bool positionIsValid(const QPoint &pos) const;
    bool selectionIsValid();
    void hoverChanged();
    int numPrimaryMetatilesRounded() const;

signals:
    void hoveredMetatileSelectionChanged(uint16_t);
    void hoveredMetatileSelectionCleared();
    void selectedMetatilesChanged();
};

#endif // METATILESELECTOR_H
