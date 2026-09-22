#ifndef TILESETEDITORMETATILESELECTOR_H
#define TILESETEDITORMETATILESELECTOR_H

#include "selectablepixmapitem.h"
#include "tileset.h"
#include "tilebrush.h"
#include "tilesetdivideritem.h"
#include <QGraphicsRectItem>
#include <QGraphicsPathItem>
#include <QSet>
#include <QPoint>
#include <functional>

class Layout;

class TilesetEditorMetatileSelector: public SelectablePixmapItem {
    Q_OBJECT
public:
    TilesetEditorMetatileSelector(int numMetatilesWide, Tileset *primaryTileset, Tileset *secondaryTileset, Layout *layout, BlockKind kind = BlockKind::Metatile);
    // CUSTOM ENGINE: which kind of block the sheet shows and edits (metatiles: 3 layers; porytiles: 1 layer, no layer bar). Switching rebuilds the sheet.
    void setKind(BlockKind kind);
    BlockKind blockKind() const { return this->kind; }
    Layout *layout = nullptr;

    void draw() override;
    void drawMetatile(uint16_t metatileId);
    void drawSelectedMetatile();

    bool select(uint16_t metatileId);
    void setTilesets(Tileset*, Tileset*);
    uint16_t getSelectedMetatileId() const { return this->selectedMetatileId; }
    QPoint getMetatileIdCoordsOnWidget(uint16_t metatileId) const;

    // CUSTOM ENGINE: the layers of the sheet. The active layer is the one that will be painted; hidden layers (the eyes) are left out of the
    // pictures, whichever layer is active. Editor-only state: it never writes the layout's own layer order.
    void setActiveLayer(int layer);
    int activeLayer() const { return this->activeLayerIndex; }
    void setLayerVisible(int layer, bool visible);
    bool isLayerVisible(int layer) const { return !this->hiddenLayers.contains(layer); }
    QList<int> visibleLayerOrder() const;

    // ---- CUSTOM ENGINE: painting on the sheet. The mouse works on the 8x8 tiles (the grid lines stay 16x16). Left button: stamp the brush at
    // the tile under the mouse, dragging stamps along the way (one stroke = strokeStarted ... strokeFinished). Right button: pick -- the single
    // tile at once, a rectangle when dragged, no Ctrl/Cmd needed. Nothing is ever erased by a mouse button: the blank tile 0 is the eraser.
    // The item only REPORTS what the mouse did, the Tileset Editor changes the metatiles.
    // There are no tools to switch between: holding Cmd (Ctrl in Qt) turns the mouse into a selection, with either button. Cmd+click adds or removes
    // ONE tile, Cmd+drag adds a rectangle (so several separate areas can be marked), Cmd+Shift+click / Cmd+Shift+drag takes tiles OUT of the
    // selection again (shown in red), Esc (the editor) deselects everything. The marked tiles are for clearing (Delete) on the active layer.
    // The sheet has two modes. Paint: the mouse works on 8x8 tiles (above). Behavior: the mouse works on whole 16x16 fields and only SELECTS them:
    // click = this field, Cmd+click = add/remove a field, left drag = a rectangle (Cmd: added), right click / right drag = add a field / a rectangle
    // (the right button never removes), Cmd+Shift (either button) = take fields out; the behavior chooser then acts on all selected fields.
    enum class SheetMode { Paint, Behavior };
    void setMode(SheetMode mode);
    SheetMode mode() const { return this->sheetMode; }
    // A view filter (the Behavior page): a field matches when it has the chosen behavior (-1: any) AND the chosen label state (any / with a label /
    // without a label). Fields that match stay exactly as they are; every other field gets the crossed-out pink circle of "Show Unused Metatiles" (drawn
    // the same way, half transparent) and nothing else -- its picture is not darkened. It only changes how the sheet looks.
    enum class LabelFilter { Any, WithLabel, WithoutLabel };
    void setFieldFilter(int behavior, LabelFilter labels);
    bool fieldFilterActive() const { return this->filterBehavior >= 0 || this->filterLabels != LabelFilter::Any; }
    int fieldFilterBehavior() const { return this->filterBehavior; }
    LabelFilter fieldFilterLabels() const { return this->filterLabels; }
    bool fieldMatchesFilter(uint16_t metatileId) const;
    QList<uint16_t> selectedCells() const;                    // Behavior mode: the selected metatile ids, in reading order
    void setSelectedCells(const QSet<uint16_t> &metatileIds);
    const QSet<QPoint> &tileSelection() const { return this->tileMarks; }
    void clearTileSelection();
    void setBrush(const TileBrush &brush);   // for the ghost that follows the mouse (a null brush: no ghost)
    // Cmd / Cmd+Shift held over the sheet change the cursor, the frame under the mouse and the brush ghost. The mouse movement reports them; the
    // editor also passes on the modifiers of a key press / release, so the feedback follows the key without the mouse having to move.
    void refreshModifiers(Qt::KeyboardModifiers modifiers);
    void setPaintingEnabled(bool enabled);
    bool paintingActive() const { return this->paintingEnabled && !this->inSwapMode && this->sheetMode == SheetMode::Paint; }
    // Returns why painting is refused right now (hidden layer, no brush yet) or an empty string. Set by the editor.
    std::function<QString()> paintBlocker;
    void setMaxPickRows(int rows) { this->maxPickRows = qMax(1, rows); }         // a right-drag pick is at most this many tiles tall (like the source sheet)
    QPoint tileAt(const QPointF &itemPos) const;                                 // the 8x8 tile under an item position
    int tilePixels() const { return this->cellWidth / 2; }
    QSize sheetTiles() const { return QSize(this->numMetatilesWide * 2, numRows() * 2); }
    // Which metatile and which of its 4 tiles a sheet tile is (false on the padding between the tilesets and outside the sheet).
    bool tileSlot(const QPoint &tile, uint16_t *metatileId, int *subIndex) const;
    void drawMetatiles(const QSet<uint16_t> &metatileIds);   // several cells, ONE pixmap update

    void setSwapMode(bool enabled);
    void addToSwapSelection(uint16_t metatileId);
    void removeFromSwapSelection(uint16_t metatileId);
    void clearSwapSelection();

    bool hasCursor() const { return this->prevCellPos != QPoint(-1,-1); }
    uint16_t metatileIdUnderCursor() const { return this->lastHoveredMetatileId; }

    QVector<uint16_t> usedMetatiles;
    bool selectorShowUnused = false;
    bool selectorShowCounts = false;
    bool showGrid = false;
    bool showDivider = false;
    bool showBehavior = false; // "Display Behavior": the behavior of every field as a hexadecimal number
    qreal behaviorOpacity = 0.75;   // how strongly those numbers (colour + digits) cover the sheet, 0..1 (the Behavior page's Opacity slider)

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent*) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent*) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent*) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent*) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent*) override;
    bool sceneEvent(QEvent*) override;

private:
    const int numMetatilesWide;
    QImage baseImage;
    QPixmap basePixmap;
    Tileset *primaryTileset = nullptr;
    Tileset *secondaryTileset = nullptr;
    uint16_t selectedMetatileId = 0;
    QPoint prevCellPos = QPoint(-1,-1);

    QList<uint16_t> swapMetatileIds;
    uint16_t lastHoveredMetatileId = 0;
    bool inSwapMode = false;
    BlockKind kind = BlockKind::Metatile;
    int activeLayerIndex = 1;
    QSet<int> hiddenLayers;
    int filterBehavior = -1;
    LabelFilter filterLabels = LabelFilter::Any;

    // painting
    int maxPickRows = 64;
    void selectUnderMouse(const QPoint &tile);
    SheetMode sheetMode = SheetMode::Paint;
    int markUnit() const;                       // item pixels of what the mouse works on: a tile (Paint) or a field (Behavior)
    QPoint unitAt(const QPointF &itemPos) const;
    bool unitValid(const QPoint &unit) const;
    QSize sheetUnits() const;
    QSet<QPoint> tileMarks;
    QGraphicsPathItem *marksItem = nullptr;
    TilesetDividerItem *dividerItem = nullptr;   // the line between the primary and the secondary tileset (an item on top of the sheet)
    bool hoverCmd = false;
    bool hoverSubtract = false;   // Cmd+Shift held over the sheet
    bool paintingEnabled = true;
    TileBrush brush;
    struct PaintState {
        bool stroking = false;
        Qt::MouseButton button = Qt::NoButton;
        QPoint lastTile;          // left: where the last stamp was
        QPoint anchorTile;        // right: where the pick started
        QPointF pressScreen;
        bool moved = false;       // right: the mouse left the click radius, so it is a region now
        QRect band;
        bool marking = false;     // the stroke marks tiles (selection) instead of stamping or picking
        bool additive = false;    // Cmd: what was marked before stays
        bool subtract = false;    // Cmd+Shift: the tiles the stroke covers are taken OUT of the marks
        bool toggleOff = false;   // Cmd+click on a marked tile: a click without a drag unmarks it
        QSet<QPoint> base;        // the marks at the press
    } paint;
    QGraphicsPixmapItem *ghostItem = nullptr;
    QGraphicsRectItem *frameItem = nullptr;
    QGraphicsRectItem *bandItem = nullptr;
    QPoint hoverTile = QPoint(-1, -1);
    void updateOverlays();
    void updateCursor();
    void beginMark(const QPoint &tile, Qt::MouseButton button, bool additive, bool canToggleOff = true, bool subtract = false);
    void applyMark(const QRect &rect);
    void setMarks(const QSet<QPoint> &marks);
    void finishPaint();
    void legacyPress(QGraphicsSceneMouseEvent*);
    void legacyMove(QGraphicsSceneMouseEvent*);
    void legacyRelease(QGraphicsSceneMouseEvent*);

    void updateBasePixmap();
    uint16_t posToMetatileId(int x, int y, bool *ok = nullptr) const;
    uint16_t posToMetatileId(const QPoint &pos, bool *ok = nullptr) const;
    QPoint metatileIdToPos(uint16_t metatileId, bool *ok = nullptr) const;
    bool isValidMetatileId(uint16_t metatileId) const;
    int numRows(int numMetatiles) const;
    int numRows() const;
    void drawGrid();
    void drawDivider();
public:
    // The line between the primary and the secondary tileset (also used by the tile sheet and the palettes of the main window): thick and red, an item
    // of its own (TilesetDividerItem) that is as thick on screen at every zoom.
    static int dividerThickness() { return TilesetDividerItem::thickness(); }
    static QColor dividerColor() { return TilesetDividerItem::color(); }
    TilesetDividerItem *dividerLine() const { return this->dividerItem; }
private:
    void drawFilters();
    void drawUnused();
    void drawCounts();
    void drawBehaviors();
    void drawFieldFilter();
    QPixmap unusedMarker() const;
    int numPrimaryMetatilesRounded() const;

signals:
    void hoveredMetatileChanged(uint16_t);
    void hoveredMetatileCleared();
    void selectedMetatileChanged(uint16_t);
    void swapRequested(uint16_t, uint16_t);
    void hoveredSlotChanged(uint16_t metatileId, int subIndex);
    void strokeStarted();
    void stampRequested(const QPoint &originTile);
    void strokeFinished();
    void tilePicked(const QPoint &tile);
    void regionPicked(const QRect &tiles);
    void paintRefused(const QString &reason);
    void tileSelectionChanged();
};

#endif // TILESETEDITORMETATILESELECTOR_H
