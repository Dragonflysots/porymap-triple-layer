#ifndef TILESETEDITORTILESELECTOR_H
#define TILESETEDITORTILESELECTOR_H

#include "selectablepixmapitem.h"
#include "tileset.h"
#include "tilebrush.h"
#include "tilesetdivideritem.h"

class TilesetEditorTileSelector: public SelectablePixmapItem {
    Q_OBJECT
public:
    TilesetEditorTileSelector(Tileset *primaryTileset, Tileset *secondaryTileset)
        : SelectablePixmapItem(16, 16, Metatile::tileWidth(), Metatile::tileWidth()) {
        this->primaryTileset = primaryTileset;
        this->secondaryTileset = secondaryTileset;
        this->numTilesWide = 16;
        this->paletteId = 0;
        this->xFlip = false;
        this->yFlip = false;
        this->paletteChanged = false;
        setAcceptHoverEvents(true);
    }
    QSize getSelectionDimensions() const override;
    void setMaxSelectionSize(int width, int height) override;
    void draw() override;
    void select(uint16_t metatileId);
    void highlight(uint16_t metatileId);
    void setTilesets(Tileset*, Tileset*);
    void setPaletteId(int);
    void setTileFlips(bool, bool);
    // What a click stamps: the source selection (current palette, flips applied) or the picked brush (flips applied on top).
    TileBrush brush();
    QList<Tile> getSelectedTiles() { return brush().tiles(); }
    // A brush picked from a sheet becomes the selection exactly as it is: the flip boxes are reset (signal flipsReset) and the palette
    // box no longer overrides its palettes until the user changes it.
    void setPicked(const TileBrush&);
    void setExternalSelection(int, int, const QList<Tile>&);
    QPoint getTileCoordsOnWidget(uint16_t);
    QImage buildPrimaryTilesIndexedImage();
    QImage buildSecondaryTilesIndexedImage();

    QVector<uint16_t> usedTiles;
    bool showUnused = false;
    bool showDivider = false;
    TilesetDividerItem *dividerItem = nullptr;   // the line between the primary and the secondary tiles (an item on top of the sheet, created on the first draw)

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent*) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent*) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent*) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;

private:
    QPixmap basePixmap;
    bool externalSelection = false;
    TileBrush externalBrush;
    QPoint prevCellPos = QPoint(-1,-1);

    Tileset *primaryTileset;
    Tileset *secondaryTileset;
    int numTilesWide;
    int paletteId;
    bool xFlip;
    bool yFlip;
    bool paletteChanged;
    void updateSelectedTiles();
    uint16_t getTileId(int x, int y);
    QPoint getTileCoords(uint16_t);
    QList<QRgb> getCurPaletteTable();
    QImage buildImage(int tileIdStart, int numTiles);
    void updateBasePixmap();
    void drawUnused();

signals:
    void hoveredTileChanged(uint16_t);
    void hoveredTileCleared();
    void selectedTilesChanged();
    void flipsReset();
};

#endif // TILESETEDITORTILESELECTOR_H
