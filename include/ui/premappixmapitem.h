#ifndef PREMAPPIXMAPITEM_H
#define PREMAPPIXMAPITEM_H

// CUSTOM ENGINE: the map-canvas item of the Porymap (design) view. It draws the pre-map's 3 layers of Porytiles over the backdrop colour (what
// the GBA shows where every layer is colour 0: palette 0 colour 0, or the project's transparency colour -- the same rule as a metatile in the
// Finalmap) and edits the ACTIVE layer, or, on the Behaviors tab (Mode::Behaviors), the behavior grid:
//  * Pencil: left click / drag paints the porytile selection of the palette (a block; a drag lays the block side by side on the block grid
//    anchored at the press, the mouse path is interpolated so a fast drag leaves no gaps; Cmd/Ctrl locks the drag to one axis).
//    The block may hang over the edge of the map: the part on the map is placed, the rest is dropped. The item reacts a few fields BEYOND the
//    map (a block anchored outside still reaches in), and a preview of the block follows the mouse: the part on the map firmly, the part
//    over the edge faintly (it is not placed). Moving a selection and pasting work the same way.
//    Right click = eyedropper of the active layer (the porytile under the mouse becomes the selection), right drag = a rectangle of porytiles.
//    Nothing is ever erased by a mouse button: erasing is painting Porytile 0 (the default fill), the same rule as in the Tileset Editor.
//  * Bucket: fills the connected area of equal porytiles with the selection (pattern anchored at the click).
//  * Eyedropper tool: left click picks like the right button does.
//  * Pointer: a drag selects a rectangle of fields of the active layer, a drag on the selection moves it (whole fields), Delete / Backspace
//    sets it to Porytile 0, the arrows nudge it, Esc clears it, Copy / Paste use an in-memory clipboard (paste lands at the hovered field).
//  * Shift: a drag moves ALL three layers (and the behaviors) together by whole fields (nothing wraps around).
//  * Behaviors tab: the pencil places the behavior chosen in the list on the fields under the mouse (Auto takes a placement away), the bucket
//    fills the connected area of fields that SHOW the same behavior, the right button / eyedropper picks the behavior a field shows.
//  * One press..release is one "stroke". The item applies a stroke to the PreMap right away and reports it afterwards with strokeFinished(),
//    so the owner can auto-save it / push a single undo step. Nothing is written to disk here.
//  * Drawing is incremental: every layer has its own canvas and each porytile picture is rendered once into a cache, so one painted field
//    costs one 16x16 blit instead of re-rendering the map.
// Purely editor-side -- never touches map.bin/metatiles.bin; see PreMap (core/premap.h) for the data it edits.

#include "core/premap.h"
#include <QGraphicsObject>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneHoverEvent>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QRect>
#include <QSet>
#include <climits>
#include <QSize>

class Layout;
class Editor;

// The porytiles the pencil paints with: a block of dims.width() x dims.height() ids in reading order (what the palette has selected).
struct PorytileSelection {
    QSize dims;
    QList<uint16_t> ids;
    bool isValid() const { return dims.width() > 0 && dims.height() > 0 && ids.size() == dims.width() * dims.height(); }
    uint16_t at(int x, int y) const { return ids.value(y * dims.width() + x, 0); }
};

// One changed field of a stroke: which layer (PreMap::kBehaviorLayer = the behavior grid), where, what it was and what it is now.
struct PreMapCellEdit {
    int layer;
    int x, y;
    uint16_t before, after;
};

struct PreMapStroke {
    QString text;
    QList<PreMapCellEdit> edits;   // in the order they happened (a field can appear more than once: the last one wins)
    bool behaviors = false;        // a stroke of the Behaviors tab (its own undo history)
};

class PreMapPixmapItem : public QGraphicsObject {
    Q_OBJECT

public:
    PreMapPixmapItem(Layout *layout, PreMap *preMap, Editor *editor);

    // The map plus a margin around it: the pencil (and the preview of a block) works there too, so a big block can be anchored beyond the
    // edge and still reach onto the map. Drawing of the layers stays inside mapRect().
    QRectF boundingRect() const override;
    QRectF mapRect() const;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    // How firmly the preview of a block shows the part that lands on the map / the part that hangs over its edge.
    static constexpr qreal kPreviewOnMap = 0.85;
    static constexpr qreal kPreviewOffMap = 0.45;
    // Test / inspection hook: the preview of `block` with its top-left field at `origin` (may be outside the map) as a picture of the block's own
    // size (16 px per field), transparent where the block is (see paintBlock).
    QImage blockPreview(const PorytileSelection &block, QPoint origin);
    // Where the mouse is over the item (any field, also outside the map; valid = the mouse is over the item).
    bool pointerOver() const { return this->pointerValid; }
    QPoint pointerField() const { return this->pointerCell; }

    // Active = takes clicks and hover; inactive = mouse passes through.
    void setActive(bool active) {
        this->active = active;
        updateAcceptance();
        if (!active) { endStroke(); setHover(QPoint(-1, -1)); setPointer(QPoint(INT_MIN, INT_MIN)); clearSelection(); }
    }
    bool isActive() const { return this->active; }
    // Hover reporting without taking clicks.
    void setHoverReporting(bool on) { this->hoverReporting = on; updateAcceptance(); }

    // What the tools edit: the porytiles of the active layer (Porytiles tab) or the behavior grid (Behaviors tab).
    enum class Mode { Porytiles, Behaviors };
    void setMode(Mode mode);
    Mode mode() const { return this->editMode; }

    Layout *layout;
    PreMap *preMap;
    Editor *editor;

    // CUSTOM ENGINE: pure editor-view convenience (the "eye" toggle per layer tab) -- never persisted, never affects the saved pre-map.
    bool bottomVisible = true;
    bool middleVisible = true;
    bool topVisible = true;
    bool &visibilityFlag(int layerIndex); // 0=bottom, 1=middle, 2=top
    void setLayerVisible(int layerIndex, bool visible); // flag + repaint; no re-render

    // Re-renders everything from the PreMap. Needed after the grid was changed from outside (undo, shift, resize, load).
    void draw();
    // The tilesets changed (Tileset Editor saved): the porytile pictures are rendered again.
    void onTilesetsChanged();

    // --- behaviors -------------------------------------------------------------------------------------------------------------------------------
    // What a field SHOWS: its placed behavior, else the behavior of its porytiles (the topmost visible layer whose porytile has a behavior
    // other than 0; layers hidden with their eye are left out). 0 = none.
    uint32_t effectiveBehaviorAt(int x, int y) const;
    uint32_t derivedBehaviorAt(int x, int y, int *fromLayer = nullptr) const;   // the porytiles' share of it (fromLayer: which layer decided, -1 none)

    // --- the pointer's selection (fields of the active layer) ----------------------------------------------------------------------------
    bool handleKey(QKeyEvent *event);   // true = consumed
    bool copySelection();               // in-memory clipboard; false = nothing selected
    bool pasteAtHover();                // pastes at the hovered field of the active layer; false = nothing to paste
    void clearSelection();
    int selectionCount() const { return this->selection.isValid() ? this->selection.width() * this->selection.height() : 0; }
    int selectionLayer() const { return this->selectedLayer; }
    QRect selectedRect() const { return this->selection; }
    PorytileSelection clipboardContents() const { return this->clipboard; }

    // Test / inspection hooks: the three layer canvases (transparent where a porytile is colour 0), the composite as the map shows it
    // (backdrop + visible layers), a layer rendered from scratch (must always equal its canvas), the picture of one porytile as the canvases
    // use it, and the backdrop colour.
    QImage layerImage(int layerIndex) const;
    QImage toImage() const;
    QImage renderLayerFromScratch(int layerIndex);
    QImage porytileImage(uint16_t id);
    QColor backdropColor() const;
    struct Stats {
        int fullRenders = 0;   // draw() runs
        int cellRenders = 0;   // single fields rendered into a canvas
        int pictureBuilds = 0; // porytile pictures rendered into the cache
    };
    Stats stats;

signals:
    void hoveredMetatile(const QPoint &field); // 16x16 map field under the cursor (only while active or hover-reporting)
    void hoverCellChanged(const QPoint &field); // the same, but also (-1, -1) when the cursor leaves the map
    void strokeFinished(const PreMapStroke &stroke); // once per press..release (or tool action) that changed something
    void regionChanged(const QRect &pixelRect);      // part of the map picture (or of the behavior grid) changed
    void layerVisibilityChanged();
    void porytilePicked(const PorytileSelection &selection); // the eyedropper (right button / Pick tool) found porytiles under the cursor
    void behaviorPicked(uint16_t stored, uint32_t shown);    // Behaviors tab: the eyedropper found a field (its placed value or Auto, and what it shows)
    void selectionChanged();
    void shiftRequested(const QPoint &delta); // the Shift tool was released: move everything by this many fields
    void strokeActiveChanged(bool active);    // a stroke started / ended (Undo is off while it runs)
    void drawn(); // emitted after every full draw(), so views built on the pre-map (Behaviors overlay) can refresh

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    bool sceneEvent(QEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override;

private:
    struct Stroke {
        bool active = false;
        Qt::MouseButton button = Qt::NoButton;
        int layer = 1;
        PorytileSelection brush;
        QPoint origin;              // field where the press happened
        QPoint last;                // last visited field
        int lockedAxis = 0;         // 0 free, 1 horizontal, 2 vertical (Cmd/Ctrl)
        QSet<qint64> visited;       // block-grid slots already stamped in this stroke
        QRect pickBand;             // right button: the rectangle picked so far
        PreMapStroke record;
        QRect dirty;                // pixels changed since the last flush
    };
    struct Drag {                   // the pointer and the Shift tool
        bool active = false;
        bool shifting = false;
        bool moving = false;
        QPoint originCell;
        QPoint delta;
        QRect band;
    };

    void updateAcceptance() {
        setAcceptedMouseButtons(this->active ? (Qt::LeftButton | Qt::RightButton) : Qt::NoButton);
        setAcceptHoverEvents(this->active || this->hoverReporting);
    }

    static QPoint cellAt(const QPointF &itemPos);
    QSize mapCells() const;
    QImage &canvas(int layerIndex);
    void ensureCanvas(int layerIndex);
    void renderLayer(QImage &target, int layerIndex);
    void renderCell(int layerIndex, int x, int y);
    QRect cellPixels(int x, int y) const;
    int activeLayer() const;
    PorytileSelection currentBrush() const;   // the palette's porytiles, or (Behaviors tab) the chosen behavior as a 1x1 brush
    bool editingBehaviors() const { return this->editMode == Mode::Behaviors; }
    bool layerVisible(int layerIndex) const { return layerIndex == 0 ? this->bottomVisible : (layerIndex == 2 ? this->topVisible : this->middleVisible); }

    // writes one field of a grid inside a stroke record (renders it, remembers it for undo); false = nothing to change
    bool writeCell(PreMapStroke &record, int layer, int x, int y, uint16_t id, QRect &dirty);
    void finishStroke(PreMapStroke &record, const QRect &dirty);

    void beginStroke(QGraphicsSceneMouseEvent *event);
    void continueStroke(QGraphicsSceneMouseEvent *event);
    void endStroke();
    void stampAt(const QPoint &cell);
    void flush();
    void setHover(const QPoint &cell);
    void setPointer(const QPoint &cell);
    void paintBlock(QPainter *painter, const PorytileSelection &block, QPoint origin);   // the preview of a block: firm on the map, faint beyond its edge
    bool onMap(const QPoint &cell) const;

    // tools
    PorytileSelection pickRect(int layer, const QRect &cells) const;
    void pickAt(const QPoint &cell);
    void bucketAt(const QPoint &cell);
    void selectPress(QGraphicsSceneMouseEvent *event);
    void shiftPress(QGraphicsSceneMouseEvent *event);
    void dragContinue(QGraphicsSceneMouseEvent *event);
    void dragEnd(QGraphicsSceneMouseEvent *event);
    void moveSelection(QPoint delta, const QString &text);
    void deleteSelection();
    bool layerEditable(int layerIndex, const QPoint &screenPos = QPoint());

    bool active = false;
    bool hoverReporting = false;
    Mode editMode = Mode::Porytiles;
    bool dirty = true;              // the canvases must be rendered before the next paint
    QImage layerCanvas[PreMap::kLayers];
    QHash<uint16_t, QImage> pictures;
    Stroke stroke;
    Drag drag;
    QPoint hover = QPoint(-1, -1);          // the map field under the mouse, (-1,-1) when the mouse is not on the map
    QPoint pointerCell;                     // the field under the mouse, wherever it is (the margin around the map included)
    bool pointerValid = false;
    QRect selection;                // fields of the active layer (invalid = none)
    int selectedLayer = -1;
    PorytileSelection clipboard;
};

#endif // PREMAPPIXMAPITEM_H
