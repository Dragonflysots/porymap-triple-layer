#include "tileseteditormetatileselector.h"
#include "imageproviders.h"
#include "project.h"
#include "behaviorcolor.h"
#include "maplayout.h"
#include <QPainter>
#include <QApplication>
#include <QPen>
#include <QtMath>

// TODO: This class has a decent bit of overlap with the MetatileSelector class.
//       They should be refactored to inherit from a single parent class.

TilesetEditorMetatileSelector::TilesetEditorMetatileSelector(int numMetatilesWide, Tileset *primaryTileset, Tileset *secondaryTileset, Layout *layout, BlockKind kind)
  : SelectablePixmapItem(32, 32, 1, 1),
    numMetatilesWide(qMax(numMetatilesWide, 1)) {
    this->kind = kind;
    if (kind == BlockKind::Porytile)
        this->activeLayerIndex = 0;
    this->primaryTileset = primaryTileset;
    this->secondaryTileset = secondaryTileset;
    this->layout = layout;
    setAcceptHoverEvents(true);
    this->usedMetatiles.resize(Project::getNumMetatilesTotal());

    // the ghost of the brush, the frame of the tile under the mouse and the rubber band of a right-drag pick: overlays, never baked into the sheet
    this->ghostItem = new QGraphicsPixmapItem(this);
    this->ghostItem->setOpacity(0.6);
    this->ghostItem->setZValue(10);
    this->ghostItem->setAcceptedMouseButtons(Qt::NoButton);
    this->ghostItem->hide();
    this->frameItem = new QGraphicsRectItem(this);
    this->frameItem->setZValue(11);
    this->frameItem->setAcceptedMouseButtons(Qt::NoButton);
    this->frameItem->hide();
    this->bandItem = new QGraphicsRectItem(this);
    this->bandItem->setZValue(12);
    this->bandItem->setAcceptedMouseButtons(Qt::NoButton);
    this->bandItem->hide();
    this->marksItem = new QGraphicsPathItem(this);
    this->marksItem->setZValue(9);
    this->marksItem->setAcceptedMouseButtons(Qt::NoButton);
    this->marksItem->hide();
    this->dividerItem = new TilesetDividerItem(this);
    updateCursor();
}

int TilesetEditorMetatileSelector::numRows(int numMetatiles) const {
    int numMetatilesHigh = numMetatiles / this->numMetatilesWide;
    if (numMetatiles % this->numMetatilesWide != 0) {
        // Round up height for incomplete last row
        numMetatilesHigh++;
    }
    return numMetatilesHigh;
}

int TilesetEditorMetatileSelector::numRows() const {
    return this->numRows(this->numPrimaryMetatilesRounded() + this->secondaryTileset->numBlocks(this->kind));
}

int TilesetEditorMetatileSelector::numPrimaryMetatilesRounded() const {
    if (!this->primaryTileset)
        return 0;
    return Util::roundUpToMultiple(this->primaryTileset->numBlocks(this->kind), this->numMetatilesWide);
}

void TilesetEditorMetatileSelector::drawMetatile(uint16_t metatileId) {
    bool ok;
    QPoint pos = metatileIdToPos(metatileId, &ok);
    if (!ok)
        return;

    QPainter painter(&this->baseImage);
    QImage metatile_image = getBlockImage(
                this->kind,
                metatileId,
                this->primaryTileset,
                this->secondaryTileset,
                visibleLayerOrder(),
                this->layout->metatileLayerOpacity(),
                true)
            .scaled(this->cellWidth, this->cellHeight);
    painter.drawImage(QPoint(pos.x() * this->cellWidth, pos.y() * this->cellHeight), metatile_image);
    painter.end();

    this->basePixmap = QPixmap::fromImage(this->baseImage);
    draw();
}

void TilesetEditorMetatileSelector::drawSelectedMetatile() {
    drawMetatile(this->selectedMetatileId);
}

void TilesetEditorMetatileSelector::updateBasePixmap() {
    this->baseImage = getBlockSheetImage(this->kind,
                                            this->primaryTileset,
                                            this->secondaryTileset,
                                            this->numMetatilesWide,
                                            visibleLayerOrder(),
                                            this->layout->metatileLayerOpacity(),
                                            QSize(this->cellWidth, this->cellHeight),
                                            true);
    this->basePixmap = QPixmap::fromImage(this->baseImage);
}

void TilesetEditorMetatileSelector::draw() {
    if (this->basePixmap.isNull())
        updateBasePixmap();
    setPixmap(this->basePixmap);

    if (this->showBehavior)
        drawBehaviors();   // (first: it tints EVERY field, so everything else is drawn on top of it -- the grid, the Unused / Counts markers, the view filter)
    drawGrid();
    drawFilters();
    drawFieldFilter();
    drawDivider();   // (the line is an item above the sheet, placed after every redraw: the sheet may have grown or shrunk)

    if (this->inSwapMode) {
        QSet<uint16_t> metatileIds(this->swapMetatileIds.constBegin(), this->swapMetatileIds.constEnd());
        metatileIds.insert(this->lastHoveredMetatileId);
        for (const auto &metatileId : metatileIds) {
            bool ok;
            QPoint pos = metatileIdToPos(metatileId, &ok);
            if (ok) drawSelectionRect(pos, QSize(1,1), Qt::DashLine);
        }
    } else if (this->sheetMode == SheetMode::Paint && isValidMetatileId(this->selectedMetatileId)) {
        drawSelection();
    }
}

bool TilesetEditorMetatileSelector::select(uint16_t metatileId) {
    bool ok;
    QPoint pos = metatileIdToPos(metatileId, &ok);
    if (!ok)
        return false;
    SelectablePixmapItem::select(pos);
    this->selectedMetatileId = metatileId;
    emit selectedMetatileChanged(metatileId);
    return true;
}

void TilesetEditorMetatileSelector::setTilesets(Tileset *primaryTileset, Tileset *secondaryTileset) {
    this->primaryTileset = primaryTileset;
    this->secondaryTileset = secondaryTileset;

    updateBasePixmap();
    draw();
}

QList<int> TilesetEditorMetatileSelector::visibleLayerOrder() const {
    if (this->kind == BlockKind::Porytile)
        return { 0 };   // (a porytile has one layer; the eyes belong to the metatile sheet)
    QList<int> order;
    const QList<int> layoutOrder = this->layout ? this->layout->metatileLayerOrder() : QList<int>({0, 1, 2});
    for (int layer : layoutOrder)
        if (!this->hiddenLayers.contains(layer))
            order.append(layer);
    return order;
}

void TilesetEditorMetatileSelector::setActiveLayer(int layer) {
    this->activeLayerIndex = this->kind == BlockKind::Porytile ? 0 : layer;
}

void TilesetEditorMetatileSelector::setKind(BlockKind kind) {
    if (kind == this->kind)
        return;
    this->kind = kind;
    if (kind == BlockKind::Porytile)
        this->activeLayerIndex = 0;
    this->paint = PaintState();
    setMarks(QSet<QPoint>());
    this->hoverTile = QPoint(-1, -1);
    this->swapMetatileIds.clear();
    updateBasePixmap();
    updateOverlays();
    draw();
}

void TilesetEditorMetatileSelector::setLayerVisible(int layer, bool visible) {
    if (visible == !this->hiddenLayers.contains(layer))
        return;
    if (visible)
        this->hiddenLayers.remove(layer);
    else
        this->hiddenLayers.insert(layer);
    updateBasePixmap();
    draw();
}

void TilesetEditorMetatileSelector::addToSwapSelection(uint16_t metatileId) {
    if (this->swapMetatileIds.contains(metatileId)) {
        return;
    }
    if (this->swapMetatileIds.length() >= 2) {
        this->swapMetatileIds.clear();
    }

    this->swapMetatileIds.append(metatileId);
    draw();

    if (this->swapMetatileIds.length() == 2) {
        emit swapRequested(this->swapMetatileIds.at(0), this->swapMetatileIds.at(1));
    }
}

void TilesetEditorMetatileSelector::removeFromSwapSelection(uint16_t metatileId) {
    if (this->swapMetatileIds.removeOne(metatileId)) {
        draw();
    }
}

void TilesetEditorMetatileSelector::clearSwapSelection() {
    if (this->swapMetatileIds.isEmpty())
        return;
    this->swapMetatileIds.clear();
    draw();
}

// A tile sheet cell is 2x2 tiles; the position of a tile is the position of the mouse in tile units, whatever the zoom.
QPoint TilesetEditorMetatileSelector::tileAt(const QPointF &itemPos) const {
    const int size = tilePixels();
    return QPoint(qFloor(itemPos.x() / size), qFloor(itemPos.y() / size));
}

bool TilesetEditorMetatileSelector::tileSlot(const QPoint &tile, uint16_t *metatileId, int *subIndex) const {
    if (tile.x() < 0 || tile.y() < 0)
        return false;
    const int cellX = tile.x() >> 1, cellY = tile.y() >> 1;
    if (cellX >= this->numMetatilesWide)
        return false;
    bool ok;
    const uint16_t id = posToMetatileId(cellX, cellY, &ok);
    if (!ok)
        return false;
    if (metatileId) *metatileId = id;
    if (subIndex) *subIndex = (tile.y() & 1) * 2 + (tile.x() & 1);   // TL, TR, BL, BR
    return true;
}

void TilesetEditorMetatileSelector::setBrush(const TileBrush &brush) {
    this->brush = brush;
    if (brush.isNull()) {
        this->ghostItem->setPixmap(QPixmap());
    } else {
        this->ghostItem->setPixmap(QPixmap::fromImage(getBrushImage(brush, this->primaryTileset, this->secondaryTileset, tilePixels())));
    }
    updateOverlays();
}

void TilesetEditorMetatileSelector::refreshModifiers(Qt::KeyboardModifiers modifiers) {
    const bool cmd = modifiers & Qt::ControlModifier;   // (Cmd held: the click will select, so the brush ghost goes away and the pointer is an arrow)
    const bool subtract = cmd && (modifiers & Qt::ShiftModifier);
    if (cmd == this->hoverCmd && subtract == this->hoverSubtract)
        return;
    this->hoverCmd = cmd;
    this->hoverSubtract = subtract;
    updateCursor();
    updateOverlays();
}

void TilesetEditorMetatileSelector::setPaintingEnabled(bool enabled) {
    this->paintingEnabled = enabled;
    updateOverlays();
}

void TilesetEditorMetatileSelector::updateOverlays() {
    const bool behaviorMode = this->sheetMode == SheetMode::Behavior;
    const bool over = (paintingActive() || behaviorMode) && this->hoverTile.x() >= 0 && this->hoverTile.y() >= 0;
    const int size = markUnit();
    static const QColor layerColors[3] = { QColor(70, 130, 255), QColor(60, 190, 90), QColor(255, 150, 30) };
    if (over && !this->paint.stroking) {
        this->frameItem->setRect(QRectF(this->hoverTile.x() * size, this->hoverTile.y() * size, size, size));
        QPen pen(this->hoverSubtract ? QColor(230, 60, 60) : behaviorMode ? QColor(255, 255, 255) : layerColors[qBound(0, this->activeLayerIndex, 2)]);   // (red: the click deselects)
        pen.setWidth(2);
        pen.setCosmetic(true);
        this->frameItem->setPen(pen);
        this->frameItem->show();
    } else {
        this->frameItem->hide();
    }
    if (over && !this->hoverCmd && !this->paint.stroking && !this->brush.isNull() && !this->ghostItem->pixmap().isNull()) {
        this->ghostItem->setPos(this->hoverTile.x() * size, this->hoverTile.y() * size);
        this->ghostItem->show();
    } else {
        this->ghostItem->hide();
    }
    if (this->paint.stroking && (this->paint.marking || this->paint.button == Qt::RightButton) && this->paint.moved && !this->paint.band.isNull()) {
        this->bandItem->setRect(QRectF(this->paint.band.x() * size, this->paint.band.y() * size, this->paint.band.width() * size, this->paint.band.height() * size));
        QPen pen(this->paint.subtract ? QColor(230, 60, 60) : QColor(Qt::white));
        pen.setStyle(Qt::DashLine);
        pen.setWidth(2);
        pen.setCosmetic(true);
        this->bandItem->setPen(pen);
        this->bandItem->show();
    } else {
        this->bandItem->hide();
    }
}

int TilesetEditorMetatileSelector::markUnit() const {
    return this->sheetMode == SheetMode::Behavior ? this->cellWidth : tilePixels();
}

QPoint TilesetEditorMetatileSelector::unitAt(const QPointF &itemPos) const {
    const int unit = markUnit();
    return QPoint(qFloor(itemPos.x() / unit), qFloor(itemPos.y() / unit));
}

bool TilesetEditorMetatileSelector::unitValid(const QPoint &unit) const {
    if (this->sheetMode == SheetMode::Paint)
        return tileSlot(unit, nullptr, nullptr);
    if (unit.x() < 0 || unit.y() < 0 || unit.x() >= this->numMetatilesWide)
        return false;
    bool ok;
    posToMetatileId(unit.x(), unit.y(), &ok);
    return ok;
}

QSize TilesetEditorMetatileSelector::sheetUnits() const {
    return this->sheetMode == SheetMode::Behavior ? QSize(this->numMetatilesWide, numRows()) : sheetTiles();
}

void TilesetEditorMetatileSelector::setMode(SheetMode mode) {
    if (mode == this->sheetMode)
        return;
    this->sheetMode = mode;
    this->paint = PaintState();
    this->tileMarks.clear();
    setMarks(QSet<QPoint>());
    this->hoverTile = QPoint(-1, -1);
    updateCursor();
    updateOverlays();
    draw();
}

QList<uint16_t> TilesetEditorMetatileSelector::selectedCells() const {
    QList<QPoint> sorted(this->tileMarks.begin(), this->tileMarks.end());
    std::sort(sorted.begin(), sorted.end(), [](const QPoint &a, const QPoint &b) { return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x(); });
    QList<uint16_t> ids;
    if (this->sheetMode != SheetMode::Behavior)
        return ids;
    for (const QPoint &cell : sorted) {
        bool ok;
        const uint16_t id = posToMetatileId(cell.x(), cell.y(), &ok);
        if (ok)
            ids.append(id);
    }
    return ids;
}

void TilesetEditorMetatileSelector::setSelectedCells(const QSet<uint16_t> &metatileIds) {
    QSet<QPoint> cells;
    for (uint16_t id : metatileIds) {
        bool ok;
        const QPoint pos = metatileIdToPos(id, &ok);
        if (ok)
            cells.insert(pos);
    }
    setMarks(cells);
}

// A click on the sheet always makes the metatile under the mouse the current one (Copy / Cut / Paste act on it), even when the click paints
// nothing (no brush yet, a hidden layer) or only selects tiles (Cmd).
void TilesetEditorMetatileSelector::selectUnderMouse(const QPoint &tile) {
    uint16_t metatileId;
    if (tileSlot(tile, &metatileId, nullptr) && metatileId != this->selectedMetatileId)
        select(metatileId);
}

void TilesetEditorMetatileSelector::updateCursor() {
    if (this->inSwapMode && porymapConfig.prettyCursors) {
        static const QCursor swapCursor = QCursor(QPixmap(":/icons/swap_cursor.ico"), 10, 10);
        setCursor(swapCursor);
    } else if (this->sheetMode == SheetMode::Behavior || this->hoverCmd || this->inSwapMode) {
        setCursor(Qt::ArrowCursor);   // (Cmd held: the mouse selects, it does not paint)
    } else if (porymapConfig.prettyCursors) {
        static const QCursor pencil = QCursor(QPixmap(":/icons/pencil_cursor.ico"), 10, 10);
        setCursor(pencil);
    } else {
        setCursor(Qt::CrossCursor);
    }
}

void TilesetEditorMetatileSelector::clearTileSelection() {
    setMarks(QSet<QPoint>());
}

void TilesetEditorMetatileSelector::setMarks(const QSet<QPoint> &marks) {
    if (marks == this->tileMarks)
        return;
    this->tileMarks = marks;
    // One rectangle per run of neighbouring tiles in a row (fast for a whole sheet of marks).
    QList<QPoint> sorted(marks.begin(), marks.end());
    std::sort(sorted.begin(), sorted.end(), [](const QPoint &a, const QPoint &b) { return a.y() != b.y() ? a.y() < b.y() : a.x() < b.x(); });
    const int size = markUnit();
    QPainterPath path;
    for (int i = 0; i < sorted.size();) {
        int j = i;
        while (j + 1 < sorted.size() && sorted[j + 1].y() == sorted[i].y() && sorted[j + 1].x() == sorted[j].x() + 1)
            j++;
        path.addRect(sorted[i].x() * size, sorted[i].y() * size, (j - i + 1) * size, size);
        i = j + 1;
    }
    // One outline around the whole marked area (the cells' inner edges are not drawn): a red, clearly visible border, and a light red fill.
    this->marksItem->setPath(path.simplified());
    QPen pen(QColor(225, 25, 25));
    pen.setCosmetic(true);
    pen.setWidth(3);
    pen.setJoinStyle(Qt::MiterJoin);
    this->marksItem->setPen(pen);
    this->marksItem->setBrush(QColor(225, 25, 25, 50));
    this->marksItem->setVisible(!marks.isEmpty());
    emit tileSelectionChanged();
}

// Starts marking tiles (Cmd). Additive keeps what was marked, otherwise a new selection replaces it; subtract (Cmd+Shift) takes the tiles the
// stroke covers OUT of what was marked.
void TilesetEditorMetatileSelector::beginMark(const QPoint &tile, Qt::MouseButton button, bool additive, bool canToggleOff, bool subtract) {
    if (!unitValid(tile))
        return;   // (the padding between the tilesets is no tile)
    if (this->sheetMode == SheetMode::Paint && !subtract)
        selectUnderMouse(tile);   // (deselecting leaves the current metatile alone)
    this->paint = PaintState();
    this->paint.stroking = true;
    this->paint.button = button;
    this->paint.marking = true;
    this->paint.additive = additive || subtract;
    this->paint.subtract = subtract;
    this->paint.anchorTile = tile;
    this->paint.base = this->paint.additive ? this->tileMarks : QSet<QPoint>();
    this->paint.toggleOff = canToggleOff && additive && !subtract && this->tileMarks.contains(tile);
    applyMark(QRect(tile, QSize(1, 1)));
}

void TilesetEditorMetatileSelector::applyMark(const QRect &rect) {
    QSet<QPoint> marks = this->paint.base;
    for (int y = rect.top(); y <= rect.bottom(); y++)
        for (int x = rect.left(); x <= rect.right(); x++)
            if (unitValid(QPoint(x, y))) {
                if (this->paint.subtract)
                    marks.remove(QPoint(x, y));
                else
                    marks.insert(QPoint(x, y));
            }
    setMarks(marks);
}

// All the tiles a straight mouse move from a to b passed over (a itself excluded), so a fast drag leaves no gaps.
static QList<QPoint> tilesBetween(QPoint a, QPoint b) {
    QList<QPoint> tiles;
    const int dx = qAbs(b.x() - a.x()), dy = qAbs(b.y() - a.y());
    const int sx = a.x() < b.x() ? 1 : -1, sy = a.y() < b.y() ? 1 : -1;
    int err = dx - dy;
    while (a != b) {
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; a.rx() += sx; }
        if (e2 < dx) { err += dx; a.ry() += sy; }
        tiles.append(a);
    }
    return tiles;
}

void TilesetEditorMetatileSelector::finishPaint() {
    if (!this->paint.stroking)
        return;
    const Qt::MouseButton button = this->paint.button;
    const bool marking = this->paint.marking;
    if (marking && this->paint.toggleOff && !this->paint.moved) {   // Cmd+click on a marked tile: unmark it
        QSet<QPoint> marks = this->paint.base;
        marks.remove(this->paint.anchorTile);
        this->paint = PaintState();
        setMarks(marks);
        updateOverlays();
        return;
    }
    this->paint = PaintState();
    updateOverlays();
    if (!marking && button == Qt::LeftButton)
        emit strokeFinished();
}

void TilesetEditorMetatileSelector::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (this->sheetMode == SheetMode::Behavior) {
        event->accept();
        if (this->paint.stroking)
            return;
        const bool cmd = event->modifiers() & Qt::ControlModifier;
        const bool subtract = cmd && (event->modifiers() & Qt::ShiftModifier);
        const QPoint field = unitAt(event->pos());
        if (event->button() == Qt::LeftButton)
            beginMark(field, Qt::LeftButton, cmd, true, subtract);           // replaces the selection; with Cmd it adds (or removes a marked field); Cmd+Shift removes
        else if (event->button() == Qt::RightButton)
            beginMark(field, Qt::RightButton, true, false, subtract);        // the right button only ever adds (Cmd+Shift: takes out)
        return;
    }
    if (!paintingActive()) {
        legacyPress(event);
        return;
    }
    event->accept();
    if (this->paint.stroking)
        return;   // a second button while one is down does nothing
    const QPoint tile = tileAt(event->pos());
    const bool cmd = event->modifiers() & Qt::ControlModifier;   // (the Cmd key on a Mac)
    if (cmd && (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)) {
        beginMark(tile, event->button(), true, true, event->modifiers() & Qt::ShiftModifier);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const QString reason = this->paintBlocker ? this->paintBlocker() : QString();
        if (!reason.isEmpty()) {
            selectUnderMouse(tile);
            emit paintRefused(reason);
            return;
        }
        uint16_t metatileId; int sub;
        if (!tileSlot(tile, &metatileId, &sub))
            return;   // the padding between the tilesets: nothing to paint on
        if (metatileId != this->selectedMetatileId)
            select(metatileId);   // the metatile under the mouse is the current one (Copy / Paste / label), as ever
        this->paint = PaintState();
        this->paint.stroking = true;
        this->paint.button = Qt::LeftButton;
        this->paint.lastTile = tile;
        emit strokeStarted();
        emit stampRequested(tile);
    } else if (event->button() == Qt::RightButton) {
        this->paint = PaintState();
        this->paint.stroking = true;
        this->paint.button = Qt::RightButton;
        this->paint.anchorTile = tile;
        this->paint.pressScreen = event->screenPos();
        emit tilePicked(tile);   // the single tile at once (a drag turns it into a region)
    }
}

void TilesetEditorMetatileSelector::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (!this->paint.stroking) {
        if (this->sheetMode == SheetMode::Paint && !paintingActive())
            legacyMove(event);
        return;
    }
    if (!(event->buttons() & this->paint.button)) {   // the release got lost: end what was drawn so far
        finishPaint();
        return;
    }
    QPoint tile = this->paint.marking ? unitAt(event->pos()) : tileAt(event->pos());
    if (this->paint.marking) {
        const QSize sheetSize = sheetUnits();
        tile.setX(qBound(0, tile.x(), sheetSize.width() - 1));
        tile.setY(qBound(0, tile.y(), sheetSize.height() - 1));
        const QPoint a = this->paint.anchorTile;
        const QRect rect(QPoint(qMin(a.x(), tile.x()), qMin(a.y(), tile.y())), QPoint(qMax(a.x(), tile.x()), qMax(a.y(), tile.y())));
        if (tile != a)
            this->paint.moved = true;
        if (rect != this->paint.band) {
            this->paint.band = rect;
            applyMark(rect);
            updateOverlays();
        }
        return;
    }
    if (this->paint.button == Qt::LeftButton) {
        if (tile == this->paint.lastTile)
            return;
        for (const QPoint &t : tilesBetween(this->paint.lastTile, tile))
            emit stampRequested(t);
        this->paint.lastTile = tile;
    } else {
        if (!this->paint.moved) {
            if ((event->screenPos() - this->paint.pressScreen).manhattanLength() < QApplication::startDragDistance())
                return;   // still a click
            this->paint.moved = true;
        }
        const QSize sheet = sheetTiles();
        const QPoint a = this->paint.anchorTile;
        tile.setX(qBound(qMax(0, a.x() - (TileBrush::kMaxCols - 1)), tile.x(), qMin(sheet.width() - 1, a.x() + (TileBrush::kMaxCols - 1))));   // at most as wide as the source sheet
        tile.setY(qBound(qMax(0, a.y() - (this->maxPickRows - 1)), tile.y(), qMin(sheet.height() - 1, a.y() + (this->maxPickRows - 1))));   // and no taller than the source sheet
        const QRect band(QPoint(qMin(a.x(), tile.x()), qMin(a.y(), tile.y())), QPoint(qMax(a.x(), tile.x()), qMax(a.y(), tile.y())));
        if (band != this->paint.band) {
            this->paint.band = band;
            updateOverlays();
            emit regionPicked(band);
        }
    }
}

void TilesetEditorMetatileSelector::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (this->paint.stroking) {
        if (event->button() == this->paint.button)
            finishPaint();
        return;
    }
    if (this->sheetMode == SheetMode::Paint && !paintingActive())
        legacyRelease(event);
}

// The grab was lost some other way (window switch, item hidden): the stroke ends here, what was drawn stays.
bool TilesetEditorMetatileSelector::sceneEvent(QEvent *event) {
    if (event->type() == QEvent::UngrabMouse)
        finishPaint();
    return SelectablePixmapItem::sceneEvent(event);
}

void TilesetEditorMetatileSelector::drawMetatiles(const QSet<uint16_t> &metatileIds) {
    QPainter painter(&this->baseImage);
    for (uint16_t metatileId : metatileIds) {
        bool ok;
        const QPoint pos = metatileIdToPos(metatileId, &ok);
        if (!ok)
            continue;
        const QImage image = getBlockImage(this->kind, metatileId, this->primaryTileset, this->secondaryTileset, visibleLayerOrder(), this->layout->metatileLayerOpacity(), true)
                                 .scaled(this->cellWidth, this->cellHeight);
        painter.drawImage(QPoint(pos.x() * this->cellWidth, pos.y() * this->cellHeight), image);
    }
    painter.end();
    this->basePixmap = QPixmap::fromImage(this->baseImage);
    draw();
}

void TilesetEditorMetatileSelector::legacyPress(QGraphicsSceneMouseEvent *event) {
    QPoint cellPos = getCellPos(event->pos());

    bool ok;
    uint16_t metatileId = posToMetatileId(cellPos, &ok);
    if (!ok) return;

    SelectablePixmapItem::mousePressEvent(event);
    this->selectedMetatileId = this->lastHoveredMetatileId = metatileId;
    emit selectedMetatileChanged(this->selectedMetatileId);
    this->prevCellPos = cellPos;
}

void TilesetEditorMetatileSelector::legacyMove(QGraphicsSceneMouseEvent *event) {
    QPoint cellPos = getCellPos(event->pos());
    if (cellPos == this->prevCellPos) return;

    bool ok;
    uint16_t metatileId = posToMetatileId(cellPos, &ok);
    if (!ok) return;

    SelectablePixmapItem::mouseMoveEvent(event);
    this->selectedMetatileId = this->lastHoveredMetatileId = metatileId;
    emit selectedMetatileChanged(this->selectedMetatileId);
    emit hoveredMetatileChanged(this->selectedMetatileId);
    this->prevCellPos = cellPos;
}

void TilesetEditorMetatileSelector::legacyRelease(QGraphicsSceneMouseEvent *event) {
    QPoint cellPos = getCellPos(event->pos());

    bool ok;
    uint16_t metatileId = posToMetatileId(cellPos, &ok);
    if (!ok) return;

    if (this->inSwapMode) {
        if (this->swapMetatileIds.contains(metatileId)) {
            this->removeFromSwapSelection(metatileId);
        } else {
            this->addToSwapSelection(metatileId);
        }
    }

    SelectablePixmapItem::mouseReleaseEvent(event);
    this->selectedMetatileId = this->lastHoveredMetatileId = metatileId;
    emit selectedMetatileChanged(this->selectedMetatileId);
    this->prevCellPos = cellPos;
}

void TilesetEditorMetatileSelector::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
    const QPoint tile = unitAt(event->pos());
    refreshModifiers(event->modifiers());
    if ((paintingActive() || this->sheetMode == SheetMode::Behavior) && tile != this->hoverTile) {
        this->hoverTile = tile;
        updateOverlays();
        uint16_t id; int sub;
        if (this->sheetMode == SheetMode::Paint && tileSlot(tile, &id, &sub))
            emit hoveredSlotChanged(id, sub);
    }
    QPoint cellPos = getCellPos(event->pos());
    if (cellPos == this->prevCellPos) return;

    bool ok;
    uint16_t metatileId = posToMetatileId(cellPos, &ok);
    if (ok) {
        this->lastHoveredMetatileId = metatileId;
        if (!paintingActive())   // (while painting the status bar names the tile under the mouse, see hoveredSlotChanged)
            emit this->hoveredMetatileChanged(metatileId);
        if (this->inSwapMode) draw();
    } else {
        emit this->hoveredMetatileCleared();
    }
    this->prevCellPos = cellPos;
}

void TilesetEditorMetatileSelector::hoverLeaveEvent(QGraphicsSceneHoverEvent*) {
    this->hoverTile = QPoint(-1, -1);
    updateOverlays();
    emit this->hoveredMetatileCleared();
    this->prevCellPos = QPoint(-1,-1);
}

uint16_t TilesetEditorMetatileSelector::posToMetatileId(const QPoint &pos, bool *ok) const {
    return posToMetatileId(pos.x(), pos.y(), ok);
}

uint16_t TilesetEditorMetatileSelector::posToMetatileId(int x, int y, bool *ok) const {
    if (ok) *ok = true;
    int index = y * this->numMetatilesWide + x;
    uint16_t metatileId = static_cast<uint16_t>(index);
    if (this->primaryTileset && this->primaryTileset->containsBlockId(this->kind, metatileId)) {
        return metatileId;
    }

    // There's some extra handling here because we round the tilesets to keep them on separate rows.
    // This means if the maximum number of primary metatiles is not divisible by the metatile width
    // then the metatiles we used to round the primary tileset would have the index of valid secondary metatiles.
    // These need to be ignored, or they'll appear to be duplicates of the subseqeunt secondary metatiles.
    int numPrimaryRounded = numPrimaryMetatilesRounded();
    int firstSecondaryRow = numPrimaryRounded / this->numMetatilesWide;
    metatileId = static_cast<uint16_t>(Project::getNumMetatilesPrimary() + index - numPrimaryRounded);
    if (this->secondaryTileset && this->secondaryTileset->containsBlockId(this->kind, metatileId) && y >= firstSecondaryRow) {
        return metatileId;
    }

    if (ok) *ok = false;
    return 0;
}

QPoint TilesetEditorMetatileSelector::metatileIdToPos(uint16_t metatileId, bool *ok) const {
    if (this->primaryTileset && this->primaryTileset->containsBlockId(this->kind, metatileId)) {
        if (ok) *ok = true;
        int index = metatileId;
        return QPoint(index % this->numMetatilesWide, index / this->numMetatilesWide);
    }
    if (this->secondaryTileset && this->secondaryTileset->containsBlockId(this->kind, metatileId)) {
        if (ok) *ok = true;
        int index = metatileId - Project::getNumMetatilesPrimary() + numPrimaryMetatilesRounded();
        return QPoint(index % this->numMetatilesWide, index / this->numMetatilesWide);
    }

    if (ok) *ok = false;
    return QPoint(0,0);
}

bool TilesetEditorMetatileSelector::isValidMetatileId(uint16_t metatileId) const {
    bool ok;
    metatileIdToPos(metatileId, &ok);
    return ok;
}

QPoint TilesetEditorMetatileSelector::getMetatileIdCoordsOnWidget(uint16_t metatileId) const {
    QPoint pos = metatileIdToPos(metatileId);
    pos.rx() = (pos.x() * this->cellWidth) + (this->cellWidth / 2);
    pos.ry() = (pos.y() * this->cellHeight) + (this->cellHeight / 2);
    return pos;
}

void TilesetEditorMetatileSelector::drawGrid() {
    if (!this->showGrid)
        return;

    QPixmap pixmap = this->pixmap();
    QPainter painter(&pixmap);
    const int numColumns = this->numMetatilesWide;
    const int numRows = this->numRows();
    for (int column = 1; column < numColumns; column++) {
        int x = column * this->cellWidth;
        painter.drawLine(x, 0, x, numRows * this->cellHeight);
    }
    for (int row = 1; row < numRows; row++) {
        int y = row * this->cellHeight;
        painter.drawLine(0, y, numColumns * this->cellWidth, y);
    }
    painter.end();
    this->setPixmap(pixmap);
}

// The line between the primary and the secondary tileset: thick and red, so nobody paints into the wrong half by accident. It is an item on top of the
// sheet (not part of its picture): it keeps its thickness on screen at every zoom and nothing that redraws the sheet can cover it.
void TilesetEditorMetatileSelector::drawDivider() {
    const bool both = this->primaryTileset && this->secondaryTileset && this->primaryTileset->numBlocks(this->kind) > 0 && this->secondaryTileset->numBlocks(this->kind) > 0;   // (with one side empty there is no boundary to show)
    this->dividerItem->place(this->numRows(this->numPrimaryMetatilesRounded()) * this->cellHeight, this->numMetatilesWide * this->cellWidth, this->showDivider && both);
}

void TilesetEditorMetatileSelector::drawFilters() {
    if (selectorShowUnused) {
        drawUnused();
    }
    if (selectorShowCounts) {
        drawCounts();
    }
}

// The circle with a line through it that marks a metatile as unused (and, in the view filter, a field that does not match).
QPixmap TilesetEditorMetatileSelector::unusedMarker() const {
    QPixmap redX(this->cellWidth, this->cellHeight);
    redX.fill(Qt::transparent);

    QPen whitePen(Qt::white);
    whitePen.setWidth(1);
    QPen pinkPen(Qt::magenta);
    pinkPen.setWidth(1);

    QPainter oPainter(&redX);

    oPainter.setPen(whitePen);
    oPainter.drawEllipse(QRect(1, 1, this->cellWidth - 2, this->cellHeight - 2));
    oPainter.setPen(pinkPen);
    oPainter.drawEllipse(QRect(2, 2, this->cellWidth - 4, this->cellHeight - 4));
    oPainter.drawEllipse(QRect(3, 3, this->cellWidth - 6, this->cellHeight - 6));

    oPainter.setPen(whitePen);
    oPainter.drawEllipse(QRect(4, 4, this->cellHeight - 8, this->cellHeight - 8));

    whitePen.setWidth(5);
    oPainter.setPen(whitePen);
    oPainter.drawLine(0, 0, this->cellWidth - 1, this->cellHeight - 1);

    pinkPen.setWidth(3);
    oPainter.setPen(pinkPen);
    oPainter.drawLine(2, 2, this->cellWidth - 3, this->cellHeight - 3);

    oPainter.end();
    return redX;
}

void TilesetEditorMetatileSelector::drawUnused() {
    // setup the circle with a line through it image to layer above unused metatiles
    const QPixmap redX = unusedMarker();

    // draw symbol on unused metatiles
    QPixmap metatilesPixmap = this->pixmap();

    QPainter unusedPainter(&metatilesPixmap);
    unusedPainter.setOpacity(0.5);

    for (int metatileId = 0; metatileId < this->usedMetatiles.size(); metatileId++) {
        if (this->usedMetatiles.at(metatileId) || !Tileset::blockIsValid(this->kind, metatileId, this->primaryTileset, this->secondaryTileset))
            continue;
        // Adjust position from center to top-left corner
        QPoint pos = getMetatileIdCoordsOnWidget(metatileId) - QPoint(this->cellWidth / 2, this->cellHeight / 2);
        unusedPainter.drawPixmap(pos.x(), pos.y(), redX);
    }
    unusedPainter.end();

    this->setPixmap(metatilesPixmap);
}

void TilesetEditorMetatileSelector::drawCounts() {
    QPen blackPen(Qt::black);
    blackPen.setWidth(1);
    QPen whitePen(Qt::white);
    whitePen.setWidth(1);

    QPixmap metatilesPixmap = this->pixmap();
    QPainter countPainter(&metatilesPixmap);

    for (int metatileId = 0; metatileId < this->usedMetatiles.size(); metatileId++) {
        if (!Tileset::blockIsValid(this->kind, metatileId, this->primaryTileset, this->secondaryTileset))
            continue;

        int count = this->usedMetatiles.at(metatileId);
        QString countText = (count > 1000) ? QStringLiteral(">1k") : QString::number(count);

        // Adjust position from center to bottom-left corner
        QPoint pos = getMetatileIdCoordsOnWidget(metatileId) + QPoint(-(this->cellWidth / 2), this->cellHeight / 2);

        // write in black and white for contrast
        countPainter.setPen(blackPen);
        countPainter.drawText(pos.x(), pos.y(), countText);
        countPainter.setPen(whitePen);
        countPainter.drawText(pos.x() + 1, pos.y() - 1, countText);
    }
    countPainter.end();

    this->setPixmap(metatilesPixmap);
}

// "Display Behavior": EVERY field gets a tint in its behavior colour and the two hex digits -- a behavior of 0x00 (MB_NORMAL) as well, in its own
// slate colour, so a field without a special behavior is seen and not mistaken for a missing number. How strongly the tint and the digits cover the
// picture is the Opacity slider of the Behavior page (behaviorOpacity).
void TilesetEditorMetatileSelector::drawBehaviors() {
    QPixmap metatilesPixmap = this->pixmap();
    QPainter painter(&metatilesPixmap);
    painter.setOpacity(qBound<qreal>(0.0, this->behaviorOpacity, 1.0));
    QFont font = painter.font();
    font.setPixelSize(qMax(8, this->cellHeight * 11 / 32));
    font.setBold(true);
    painter.setFont(font);

    for (int metatileId = 0; metatileId < this->usedMetatiles.size(); metatileId++) {
        const Metatile *metatile = Tileset::getBlock(this->kind, metatileId, this->primaryTileset, this->secondaryTileset);
        bool ok;
        const QPoint cell = metatileIdToPos(metatileId, &ok);
        if (!metatile || !ok)
            continue;
        const int behavior = metatile->getAttribute(Metatile::Attr::Behavior);
        const QRect rect(cell.x() * this->cellWidth, cell.y() * this->cellHeight, this->cellWidth, this->cellHeight);
        const QString digits = QString("%1").arg(behavior, 2, 16, QChar('0')).toUpper();
        const QColor tint = BehaviorColor::forId(behavior);
        painter.fillRect(rect, tint);
        painter.setPen(BehaviorColor::inkFor(tint));
        painter.drawText(rect, Qt::AlignCenter, digits);
    }
    painter.end();
    this->setPixmap(metatilesPixmap);
}

void TilesetEditorMetatileSelector::setFieldFilter(int behavior, LabelFilter labels) {
    behavior = qMax(-1, behavior);
    if (behavior == this->filterBehavior && labels == this->filterLabels)
        return;
    this->filterBehavior = behavior;
    this->filterLabels = labels;
    draw();
}

bool TilesetEditorMetatileSelector::fieldMatchesFilter(uint16_t metatileId) const {
    if (this->filterBehavior >= 0) {
        const Metatile *metatile = Tileset::getBlock(this->kind, metatileId, this->primaryTileset, this->secondaryTileset);
        if (!metatile || static_cast<int>(metatile->behavior()) != this->filterBehavior)
            return false;
    }
    if (this->filterLabels != LabelFilter::Any) {
        const bool hasLabel = !Tileset::getOwnedBlockLabel(this->kind, metatileId, this->primaryTileset, this->secondaryTileset).isEmpty();
        if (hasLabel != (this->filterLabels == LabelFilter::WithLabel))
            return false;
    }
    return true;
}

// The view filter: a field that matches is left alone; every other field carries the crossed-out pink circle that "Show Unused Metatiles" uses,
// drawn the same way (half transparent) -- and nothing else: no darkening of the field, its picture stays fully visible.
void TilesetEditorMetatileSelector::drawFieldFilter() {
    if (!fieldFilterActive())
        return;
    QPixmap metatilesPixmap = this->pixmap();
    QPainter painter(&metatilesPixmap);
    const QPixmap marker = unusedMarker();
    painter.setOpacity(0.5);
    for (int metatileId = 0; metatileId < this->usedMetatiles.size(); metatileId++) {
        bool ok;
        const QPoint cell = metatileIdToPos(metatileId, &ok);
        if (!ok || !Tileset::getBlock(this->kind, metatileId, this->primaryTileset, this->secondaryTileset) || fieldMatchesFilter(metatileId))
            continue;
        painter.drawPixmap(cell.x() * this->cellWidth, cell.y() * this->cellHeight, marker);
    }
    painter.end();
    this->setPixmap(metatilesPixmap);
}

void TilesetEditorMetatileSelector::setSwapMode(bool enabled) {
    if (enabled == this->inSwapMode)
        return;
    this->inSwapMode = enabled;
    this->swapMetatileIds.clear();
    updateCursor();
    updateOverlays();
    draw();
}
