#include "premappixmapitem.h"
#include "editor.h"
#include "maplayout.h"
#include "imageproviders.h"
#include "tilebrush.h"
#include "tileset.h"
#include "metatile.h"
#include "config.h"

#include <QPainter>
#include <QToolTip>
#include <QKeyEvent>
#include <QStyleOptionGraphicsItem>
#include <QtMath>
#include <functional>

namespace {
constexpr int kField = Metatile::pixelWidth();   // 16 px
constexpr int kMinReach = 8 * kField;            // how far beyond the map the item reacts at the least (a block of up to 8 fields can reach in from outside)

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int floorMod(int a, int b) { return a - floorDiv(a, b) * b; }
qint64 packCell(int x, int y) { return (static_cast<qint64>(y) << 32) | static_cast<quint32>(x); }

// Every field a straight mouse move from a to b passed over (a itself excluded), so a fast drag leaves no gaps.
void forEachCellAfter(QPoint a, QPoint b, const std::function<void(const QPoint &)> &fn) {
    const int dx = qAbs(b.x() - a.x()), dy = qAbs(b.y() - a.y());
    const int sx = a.x() < b.x() ? 1 : -1, sy = a.y() < b.y() ? 1 : -1;
    int err = dx - dy;
    while (a != b) {
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; a.rx() += sx; }
        if (e2 < dx) { err += dx; a.ry() += sy; }
        fn(a);
    }
}
} // namespace

PreMapPixmapItem::PreMapPixmapItem(Layout *layout, PreMap *preMap, Editor *editor)
    : QGraphicsObject(), layout(layout), preMap(preMap), editor(editor) {
    setFlag(ItemUsesExtendedStyleOption, true);
    updateAcceptance();
}

QRectF PreMapPixmapItem::mapRect() const {
    const QSize cells = mapCells();
    return QRectF(0, 0, cells.width() * kField, cells.height() * kField);
}

QRectF PreMapPixmapItem::boundingRect() const {
    const QRectF map = mapRect();
    if (!this->layout)
        return map;
    const QRectF visible = this->layout->getVisibleRect();   // (the map and the player's view distance around it: what the view shows beyond the map)
    return map.adjusted(-qMax<qreal>(kMinReach, map.left() - visible.left()), -qMax<qreal>(kMinReach, map.top() - visible.top()),
                        qMax<qreal>(kMinReach, visible.right() - map.right()), qMax<qreal>(kMinReach, visible.bottom() - map.bottom()));
}

bool PreMapPixmapItem::onMap(const QPoint &cell) const {
    return this->preMap && this->preMap->contains(cell.x(), cell.y());
}

QSize PreMapPixmapItem::mapCells() const {
    return this->layout ? QSize(this->layout->getWidth(), this->layout->getHeight()) : QSize();
}

QPoint PreMapPixmapItem::cellAt(const QPointF &itemPos) {
    return QPoint(qFloor(itemPos.x() / kField), qFloor(itemPos.y() / kField));
}

QRect PreMapPixmapItem::cellPixels(int x, int y) const {
    return QRect(x * kField, y * kField, kField, kField);
}

int PreMapPixmapItem::activeLayer() const {
    return this->editor ? static_cast<int>(this->editor->getPreMapLayer()) : 1;
}

PorytileSelection PreMapPixmapItem::currentBrush() const {
    if (editingBehaviors()) {
        PorytileSelection brush;
        brush.dims = QSize(1, 1);
        brush.ids = { this->editor ? this->editor->preMapBehaviorBrush() : PreMap::kAutoBehavior };
        return brush;
    }
    return this->editor ? this->editor->porytileSelection() : PorytileSelection();
}

void PreMapPixmapItem::setMode(Mode mode) {
    if (this->editMode == mode)
        return;
    endStroke();
    clearSelection();
    this->editMode = mode;
    update();
}

bool &PreMapPixmapItem::visibilityFlag(int layerIndex) {
    switch (layerIndex) {
        case 0: return this->bottomVisible;
        case 2: return this->topVisible;
        default: return this->middleVisible;
    }
}

void PreMapPixmapItem::setLayerVisible(int layerIndex, bool visible) {
    if (visibilityFlag(layerIndex) == visible)
        return;
    visibilityFlag(layerIndex) = visible;
    update();
    emit this->layerVisibilityChanged();
}

// ---- behaviors -----------------------------------------------------------------------------------------------------------------------------------

uint32_t PreMapPixmapItem::derivedBehaviorAt(int x, int y, int *fromLayer) const {
    uint32_t id = 0;
    int decided = -1;
    if (this->preMap && this->layout) {
        for (int layer = 0; layer < PreMap::kLayers; layer++) {   // bottom first: a higher layer with a behavior replaces it
            if (!layerVisible(layer))
                continue;
            const Metatile *porytile = Tileset::getPorytile(this->preMap->at(layer, x, y), this->layout->tileset_primary, this->layout->tileset_secondary);
            if (porytile && porytile->behavior() != 0) {
                id = porytile->behavior();
                decided = layer;
            }
        }
    }
    if (fromLayer)
        *fromLayer = decided;
    return id;
}

uint32_t PreMapPixmapItem::effectiveBehaviorAt(int x, int y) const {
    if (this->preMap && this->preMap->hasPlacedBehavior(x, y))
        return this->preMap->behaviorAt(x, y);
    return derivedBehaviorAt(x, y);
}

// ---- pictures ----------------------------------------------------------------------------------------------------------------------------------

// The picture of a porytile as the map shows it: 16x16, colour 0 transparent (the layers composite over the backdrop). Porytile 0 is a
// porytile like any other: blank it shows nothing but the backdrop, given tiles it shows them.
QImage PreMapPixmapItem::porytileImage(uint16_t id) {
    auto found = this->pictures.constFind(id);
    if (found != this->pictures.constEnd())
        return found.value();
    QImage image;
    const Metatile *porytile = this->layout ? Tileset::getPorytile(id, this->layout->tileset_primary, this->layout->tileset_secondary) : nullptr;
    if (porytile) {
        const TileBrush brush = TileBrush::fromGrid(QRect(0, 0, 2, 2), [&](int x, int y) { return porytile->tiles.value(y * 2 + x); });
        image = getBrushImage(brush, this->layout->tileset_primary, this->layout->tileset_secondary, Tile::pixelWidth()).convertToFormat(QImage::Format_ARGB32_Premultiplied);
        this->stats.pictureBuilds++;
    }
    this->pictures.insert(id, image);
    return image;
}

// What the GBA shows where every layer is colour 0 -- the same colour a metatile of the Finalmap gets under its tiles.
QColor PreMapPixmapItem::backdropColor() const {
    if (projectConfig.transparencyColor.isValid())
        return projectConfig.transparencyColor;
    if (this->layout) {
        const QList<QList<QRgb>> palettes = Tileset::getBlockPalettes(this->layout->tileset_primary, this->layout->tileset_secondary);
        if (!palettes.isEmpty() && !palettes.first().isEmpty())
            return QColor(palettes.first().first());
    }
    return QColor(Qt::black);
}

void PreMapPixmapItem::onTilesetsChanged() {
    this->pictures.clear();
    draw();
}

QImage &PreMapPixmapItem::canvas(int layerIndex) {
    return this->layerCanvas[qBound(0, layerIndex, PreMap::kLayers - 1)];
}

void PreMapPixmapItem::ensureCanvas(int layerIndex) {
    const QSize cells = mapCells();
    QImage &target = canvas(layerIndex);
    const QSize wanted(cells.width() * kField, cells.height() * kField);
    if (target.size() != wanted || target.isNull()) {
        target = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::transparent);
    }
}

void PreMapPixmapItem::renderLayer(QImage &target, int layerIndex) {
    target.fill(Qt::transparent);
    if (!this->preMap)
        return;
    QPainter painter(&target);
    const QSize cells = mapCells();
    for (int y = 0; y < cells.height(); y++) {
        for (int x = 0; x < cells.width(); x++) {
            const QImage picture = porytileImage(this->preMap->at(layerIndex, x, y));
            if (!picture.isNull())
                painter.drawImage(x * kField, y * kField, picture);
        }
    }
}

void PreMapPixmapItem::renderCell(int layerIndex, int x, int y) {
    ensureCanvas(layerIndex);
    QImage &target = canvas(layerIndex);
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(cellPixels(x, y), Qt::transparent);
    const QImage picture = porytileImage(this->preMap ? this->preMap->at(layerIndex, x, y) : 0);
    if (!picture.isNull()) {
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.drawImage(x * kField, y * kField, picture);
    }
    this->stats.cellRenders++;
}

void PreMapPixmapItem::draw() {
    prepareGeometryChange();
    for (int layer = 0; layer < PreMap::kLayers; layer++) {
        ensureCanvas(layer);
        renderLayer(canvas(layer), layer);
    }
    this->dirty = false;
    this->stats.fullRenders++;
    update();
    emit this->drawn();
}

QImage PreMapPixmapItem::layerImage(int layerIndex) const {
    return this->layerCanvas[qBound(0, layerIndex, PreMap::kLayers - 1)];
}

QImage PreMapPixmapItem::renderLayerFromScratch(int layerIndex) {
    const QSize cells = mapCells();
    QImage image(cells.width() * kField, cells.height() * kField, QImage::Format_ARGB32_Premultiplied);
    renderLayer(image, layerIndex);
    return image;
}

QImage PreMapPixmapItem::toImage() const {
    const QSize cells = mapCells();
    QImage image(cells.width() * kField, cells.height() * kField, QImage::Format_ARGB32_Premultiplied);
    image.fill(backdropColor());
    QPainter painter(&image);
    for (int layer = 0; layer < PreMap::kLayers; layer++)
        if (layerVisible(layer))
            painter.drawImage(0, 0, this->layerCanvas[layer]);
    return image;
}

QVariant PreMapPixmapItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemVisibleHasChanged && value.toBool() && this->dirty)
        draw();
    return QGraphicsObject::itemChange(change, value);
}

void PreMapPixmapItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *) {
    if (this->dirty)
        draw();
    const QRectF exposed = (option->exposedRect.isNull() ? boundingRect() : option->exposedRect).intersected(mapRect());   // (the margin around the map is not drawn on)
    // the backdrop under the layers (the real map's metatiles and the border tiles below this item stay hidden)
    if (!exposed.isEmpty()) {
        painter->fillRect(exposed, backdropColor());
        for (int layer = 0; layer < PreMap::kLayers; layer++) {
            if (!layerVisible(layer) || this->layerCanvas[layer].isNull())
                continue;
            painter->drawImage(exposed, this->layerCanvas[layer], exposed);
        }
    }
    // a selection that is being moved: what it holds shows at the target, firmly on the map and faintly beyond its edge (that part is dropped)
    if (this->selection.isValid() && this->drag.active && this->drag.moving && this->selectedLayer >= 0 && this->selectedLayer < PreMap::kLayers && layerVisible(this->selectedLayer))
        paintBlock(painter, pickRect(this->selectedLayer, this->selection), this->selection.topLeft() + this->drag.delta);
    // the pointer's selection and its band / move
    if (this->selection.isValid()) {
        QRect rect(this->selection.x() * kField, this->selection.y() * kField, this->selection.width() * kField, this->selection.height() * kField);
        if (this->drag.active && this->drag.moving)
            rect.translate(this->drag.delta.x() * kField, this->drag.delta.y() * kField);
        painter->fillRect(rect, QColor(70, 130, 255, 60));
        QPen pen(QColor(40, 110, 255));
        pen.setCosmetic(true);
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(rect);
    }
    if (this->drag.active && !this->drag.moving && !this->drag.shifting && this->drag.band.isValid()) {
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRect(this->drag.band.x() * kField, this->drag.band.y() * kField, this->drag.band.width() * kField, this->drag.band.height() * kField));
    }
    if (this->stroke.active && this->stroke.button == Qt::RightButton && this->stroke.pickBand.isValid()) {
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRect(this->stroke.pickBand.x() * kField, this->stroke.pickBand.y() * kField, this->stroke.pickBand.width() * kField, this->stroke.pickBand.height() * kField));
    }
    // the hovered field: the pencil shows the footprint of the brush and a preview of it (on the Behaviors tab the overlay covers every field and would
    // hide it: there the frame belongs to the overlay item, which draws it above itself). Only the pencil reacts beyond the map; the other tools
    // show their frame on the map only.
    if (this->active && !editingBehaviors() && this->pointerValid && !this->stroke.active && !this->drag.active) {
        const bool pencil = this->editor && this->editor->getEditAction() == Editor::EditAction::Paint;
        if (pencil || onMap(this->pointerCell)) {
            QSize footprint(1, 1);
            if (pencil) {
                const PorytileSelection brush = currentBrush();
                if (brush.isValid()) {
                    footprint = brush.dims;
                    if (layerVisible(activeLayer()))   // (a hidden layer takes no paint: nothing to preview)
                        paintBlock(painter, brush, this->pointerCell);
                }
            }
            static const QColor layerColors[3] = { QColor(70, 130, 255), QColor(60, 190, 90), QColor(255, 150, 30) };
            QPen pen(editingBehaviors() ? QColor(200, 80, 230) : layerColors[qBound(0, activeLayer(), 2)]);
            pen.setCosmetic(true);
            pen.setWidth(2);
            painter->setPen(pen);
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(QRect(this->pointerCell.x() * kField, this->pointerCell.y() * kField, footprint.width() * kField, footprint.height() * kField));
        }
    }
}

// The preview of a block of porytiles with its top-left field at `origin`: a field on the map shows its porytile firmly, a field beyond the edge
// faintly -- that part will not be placed. (A blank porytile shows nothing, as on the map.)
void PreMapPixmapItem::paintBlock(QPainter *painter, const PorytileSelection &block, QPoint origin) {
    if (!block.isValid())
        return;
    const qreal saved = painter->opacity();
    for (int y = 0; y < block.dims.height(); y++) {
        for (int x = 0; x < block.dims.width(); x++) {
            const QImage picture = porytileImage(block.at(x, y));
            if (picture.isNull())
                continue;
            const QPoint cell(origin.x() + x, origin.y() + y);
            painter->setOpacity(saved * (onMap(cell) ? kPreviewOnMap : kPreviewOffMap));
            painter->drawImage(cell.x() * kField, cell.y() * kField, picture);
        }
    }
    painter->setOpacity(saved);
}

QImage PreMapPixmapItem::blockPreview(const PorytileSelection &block, QPoint origin) {
    QImage image(block.dims.width() * kField, block.dims.height() * kField, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.translate(-origin.x() * kField, -origin.y() * kField);
    paintBlock(&painter, block, origin);
    painter.end();
    return image;
}

// ---- hover -------------------------------------------------------------------------------------------------------------------------------------

void PreMapPixmapItem::setHover(const QPoint &cell) {
    if (cell == this->hover)
        return;
    this->hover = cell;
    update();
    emit this->hoverCellChanged(cell);
    if (cell.x() >= 0)
        emit this->hoveredMetatile(cell);
}

void PreMapPixmapItem::setPointer(const QPoint &cell) {
    if (this->pointerValid && cell == this->pointerCell)
        return;
    this->pointerValid = cell.x() != INT_MIN;
    this->pointerCell = cell;
    update();   // (the preview of the brush follows the mouse)
}

void PreMapPixmapItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event) {
    const QPoint cell = cellAt(event->pos());
    setPointer(cell);
    setHover(onMap(cell) ? cell : QPoint(-1, -1));   // (what listens to the hover -- status bar, overlay -- only ever hears of fields of the map)
}

void PreMapPixmapItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *) {
    setPointer(QPoint(INT_MIN, INT_MIN));
    setHover(QPoint(-1, -1));
}

// ---- mouse dispatch ------------------------------------------------------------------------------------------------------------------------------

bool PreMapPixmapItem::layerEditable(int layerIndex, const QPoint &screenPos) {
    if (layerIndex == PreMap::kBehaviorLayer)
        return true;
    const bool visible = layerVisible(layerIndex);
    if (!visible && !screenPos.isNull())
        QToolTip::showText(screenPos, "This layer is hidden (eye) -- show it to edit it", nullptr, {}, 2000);
    return visible;
}

void PreMapPixmapItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    // The Hand tool, the middle mouse button (and the synthetic press the view injects for middle-button scrolling) pan the view: the event
    // must fall through to the QGraphicsView instead of being swallowed here.
    if (!this->editor || this->editor->preMapPressPassesThrough(event)) {
        event->ignore();
        return;
    }
    if (this->stroke.active || this->drag.active) // a second button during a stroke or drag is swallowed
        return;
    const QPoint cell = cellAt(event->pos());
    const Editor::EditAction action = this->editor->getEditAction();
    // Beyond the edge of the map only the pencil's left button works (its block may reach onto the map from there); every other tool needs a field.
    if (!onMap(cell) && !(action == Editor::EditAction::Paint && event->button() == Qt::LeftButton && !editingBehaviors()))
        return;
    switch (action) {
    case Editor::EditAction::Paint:
        beginStroke(event);
        break;
    case Editor::EditAction::Pick:
        pickAt(cell);
        break;
    case Editor::EditAction::Fill:
        if (event->button() == Qt::LeftButton)
            bucketAt(cell);
        else if (event->button() == Qt::RightButton)
            pickAt(cell);
        break;
    case Editor::EditAction::Select:
        if (event->button() == Qt::LeftButton && !editingBehaviors())
            selectPress(event);
        break;
    case Editor::EditAction::Shift:
        if (event->button() == Qt::LeftButton && !editingBehaviors())
            shiftPress(event);
        break;
    default:
        break;
    }
}

void PreMapPixmapItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (this->stroke.active) {
        if (!(event->buttons() & this->stroke.button)) { endStroke(); return; }   // the release got lost
        continueStroke(event);
    } else if (this->drag.active) {
        dragContinue(event);
    }
}

void PreMapPixmapItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if (this->stroke.active && event->button() == this->stroke.button)
        endStroke();
    else if (this->drag.active && event->button() == Qt::LeftButton)
        dragEnd(event);
    update();   // (the preview of the block comes back where the mouse is: it is hidden while the button is down, and a stroke repaints only the fields it changed)
}

bool PreMapPixmapItem::sceneEvent(QEvent *event) {
    if (event->type() == QEvent::UngrabMouse) { // the grab was lost some other way (window switch, item hidden)
        endStroke();
        if (this->drag.active) { // a pointer drag that never finished: forget it, nothing was changed yet
            this->drag = Drag();
            emit this->strokeActiveChanged(false);
            update();
        }
    }
    return QGraphicsObject::sceneEvent(event);
}

// ---- strokes -------------------------------------------------------------------------------------------------------------------------------------

bool PreMapPixmapItem::writeCell(PreMapStroke &record, int layer, int x, int y, uint16_t id, QRect &dirty) {
    if (!this->preMap || !this->preMap->contains(x, y))
        return false;
    const uint16_t before = this->preMap->at(layer, x, y);
    if (before == id)
        return false;
    this->preMap->set(layer, x, y, id);
    record.edits.append(PreMapCellEdit{ layer, x, y, before, id });
    if (layer != PreMap::kBehaviorLayer)
        renderCell(layer, x, y);   // (a behavior has no picture here: the overlay redraws the changed region)
    dirty = dirty.isNull() ? cellPixels(x, y) : dirty.united(cellPixels(x, y));
    return true;
}

void PreMapPixmapItem::finishStroke(PreMapStroke &record, const QRect &dirty) {
    if (record.edits.isEmpty())
        return;
    if (!dirty.isNull()) {
        update(dirty);
        emit this->regionChanged(dirty);
    }
    emit this->strokeFinished(record);
    if (this->hover.x() >= 0)
        emit this->hoveredMetatile(this->hover);   // (the status bar describes the field under the mouse: it just changed)
}

void PreMapPixmapItem::beginStroke(QGraphicsSceneMouseEvent *event) {
    const QPoint cell = cellAt(event->pos());
    this->stroke = Stroke();
    this->stroke.layer = editingBehaviors() ? PreMap::kBehaviorLayer : activeLayer();
    if (event->button() == Qt::RightButton) {
        if (editingBehaviors()) {   // the eyedropper of the Behaviors tab: one field, no band
            pickAt(cell);
            return;
        }
        // the eyedropper: the field now, a rectangle when dragged
        this->stroke.active = true;
        this->stroke.button = Qt::RightButton;
        this->stroke.origin = this->stroke.last = cell;
        this->stroke.pickBand = QRect(cell, QSize(1, 1));
        emit this->strokeActiveChanged(true);
        emit this->porytilePicked(pickRect(this->stroke.layer, this->stroke.pickBand));
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (!layerEditable(this->stroke.layer, event->screenPos()))
        return;
    const PorytileSelection brush = currentBrush();
    if (!brush.isValid())
        return;
    this->stroke.active = true;
    this->stroke.button = Qt::LeftButton;
    this->stroke.brush = brush;
    this->stroke.origin = this->stroke.last = cell;
    this->stroke.lockedAxis = (event->modifiers() & Qt::ControlModifier) ? -1 : 0;   // (decided by the first move)
    this->stroke.record.text = editingBehaviors() ? QStringLiteral("Place Behaviors") : QStringLiteral("Paint Porytiles");
    this->stroke.record.behaviors = editingBehaviors();
    emit this->strokeActiveChanged(true);
    stampAt(cell);
    flush();
}

// The block of the brush, laid on the block grid anchored at the press: the slot that holds `cell` is stamped once per stroke.
void PreMapPixmapItem::stampAt(const QPoint &cell) {
    const QSize dims = this->stroke.brush.dims;
    const int slotX = floorDiv(cell.x() - this->stroke.origin.x(), dims.width()), slotY = floorDiv(cell.y() - this->stroke.origin.y(), dims.height());
    const qint64 slot = packCell(slotX, slotY);
    if (this->stroke.visited.contains(slot))
        return;
    this->stroke.visited.insert(slot);
    const QPoint topLeft(this->stroke.origin.x() + slotX * dims.width(), this->stroke.origin.y() + slotY * dims.height());
    for (int y = 0; y < dims.height(); y++)
        for (int x = 0; x < dims.width(); x++)
            writeCell(this->stroke.record, this->stroke.layer, topLeft.x() + x, topLeft.y() + y, this->stroke.brush.at(x, y), this->stroke.dirty);
}

void PreMapPixmapItem::continueStroke(QGraphicsSceneMouseEvent *event) {
    QPoint cell = cellAt(event->pos());
    if (this->stroke.button == Qt::RightButton) {
        const QSize cells = mapCells();
        cell.setX(qBound(0, cell.x(), cells.width() - 1));
        cell.setY(qBound(0, cell.y(), cells.height() - 1));
        const QPoint a = this->stroke.origin;
        const QRect band(QPoint(qMin(a.x(), cell.x()), qMin(a.y(), cell.y())), QPoint(qMax(a.x(), cell.x()), qMax(a.y(), cell.y())));
        if (band != this->stroke.pickBand) {
            this->stroke.pickBand = band;
            update();
        }
        return;
    }
    if (this->stroke.lockedAxis == -1 && cell != this->stroke.origin)
        this->stroke.lockedAxis = qAbs(cell.x() - this->stroke.origin.x()) >= qAbs(cell.y() - this->stroke.origin.y()) ? 1 : 2;
    if (this->stroke.lockedAxis == 1) cell.setY(this->stroke.origin.y());
    if (this->stroke.lockedAxis == 2) cell.setX(this->stroke.origin.x());
    if (cell == this->stroke.last)
        return;
    forEachCellAfter(this->stroke.last, cell, [this](const QPoint &c) { stampAt(c); });
    this->stroke.last = cell;
    flush();
}

void PreMapPixmapItem::flush() {
    if (this->stroke.dirty.isNull())
        return;
    update(this->stroke.dirty);
    emit this->regionChanged(this->stroke.dirty);
    this->stroke.dirty = QRect();
}

void PreMapPixmapItem::endStroke() {
    if (!this->stroke.active)
        return;
    const Stroke finished = this->stroke;
    this->stroke = Stroke();
    if (finished.button == Qt::RightButton) {
        if (finished.pickBand.width() > 1 || finished.pickBand.height() > 1)
            emit this->porytilePicked(pickRect(finished.layer, finished.pickBand));
        update();
    } else if (!finished.record.edits.isEmpty()) {
        emit this->strokeFinished(finished.record);
        if (this->hover.x() >= 0)
            emit this->hoveredMetatile(this->hover);   // (the status bar describes the field under the mouse: it just changed)
    }
    emit this->strokeActiveChanged(false);
}

// ---- tools -----------------------------------------------------------------------------------------------------------------------------------------

PorytileSelection PreMapPixmapItem::pickRect(int layer, const QRect &cells) const {
    PorytileSelection picked;
    picked.dims = cells.size();
    for (int y = cells.top(); y <= cells.bottom(); y++)
        for (int x = cells.left(); x <= cells.right(); x++)
            picked.ids.append(this->preMap ? this->preMap->at(layer, x, y) : 0);
    return picked;
}

void PreMapPixmapItem::pickAt(const QPoint &cell) {
    if (editingBehaviors()) {
        emit this->behaviorPicked(this->preMap ? this->preMap->behaviorAt(cell.x(), cell.y()) : PreMap::kAutoBehavior, effectiveBehaviorAt(cell.x(), cell.y()));
        return;
    }
    emit this->porytilePicked(pickRect(activeLayer(), QRect(cell, QSize(1, 1))));
}

// Fills the connected area (4-neighbours) of fields that hold the same porytile as `cell` with the brush, its pattern anchored at `cell`.
// On the Behaviors tab: the connected area of fields that SHOW the same behavior gets the chosen one.
void PreMapPixmapItem::bucketAt(const QPoint &cell) {
    const bool behaviors = editingBehaviors();
    const int layer = behaviors ? PreMap::kBehaviorLayer : activeLayer();
    if (!layerEditable(layer))
        return;
    const PorytileSelection brush = currentBrush();
    if (!brush.isValid() || !this->preMap)
        return;
    const uint32_t target = behaviors ? effectiveBehaviorAt(cell.x(), cell.y()) : this->preMap->at(layer, cell.x(), cell.y());
    auto shows = [&](const QPoint &c) { return behaviors ? effectiveBehaviorAt(c.x(), c.y()) : static_cast<uint32_t>(this->preMap->at(layer, c.x(), c.y())); };
    if (!behaviors && brush.dims == QSize(1, 1) && brush.ids.first() == target)
        return;
    const QSize cells = mapCells();
    QVector<bool> seen(cells.width() * cells.height(), false);
    QList<QPoint> todo = { cell };
    seen[cell.y() * cells.width() + cell.x()] = true;
    PreMapStroke record;
    record.text = behaviors ? QStringLiteral("Fill Behaviors") : QStringLiteral("Fill Porytiles");
    record.behaviors = behaviors;
    QRect dirty;
    while (!todo.isEmpty()) {
        const QPoint c = todo.takeLast();
        // (the neighbours are judged BEFORE this field is written: a field already written holds the brush now)
        for (const QPoint &n : { QPoint(c.x() + 1, c.y()), QPoint(c.x() - 1, c.y()), QPoint(c.x(), c.y() + 1), QPoint(c.x(), c.y() - 1) }) {
            if (!this->preMap->contains(n.x(), n.y()) || seen[n.y() * cells.width() + n.x()])
                continue;
            seen[n.y() * cells.width() + n.x()] = true;
            if (shows(n) == target)
                todo.append(n);
        }
        writeCell(record, layer, c.x(), c.y(), brush.at(floorMod(c.x() - cell.x(), brush.dims.width()), floorMod(c.y() - cell.y(), brush.dims.height())), dirty);
    }
    finishStroke(record, dirty);
}

void PreMapPixmapItem::selectPress(QGraphicsSceneMouseEvent *event) {
    const QPoint cell = cellAt(event->pos());
    const int layer = activeLayer();
    this->drag = Drag();
    this->drag.active = true;
    this->drag.originCell = cell;
    if (this->selection.isValid() && this->selectedLayer == layer && this->selection.contains(cell)) {
        this->drag.moving = true;
    } else {
        this->drag.band = QRect(cell, QSize(1, 1));
    }
    emit this->strokeActiveChanged(true);
    update();
}

void PreMapPixmapItem::shiftPress(QGraphicsSceneMouseEvent *event) {
    this->drag = Drag();
    this->drag.active = true;
    this->drag.shifting = true;
    this->drag.originCell = cellAt(event->pos());
    clearSelection();
    emit this->strokeActiveChanged(true);
    update();
}

void PreMapPixmapItem::dragContinue(QGraphicsSceneMouseEvent *event) {
    const QPoint cell = cellAt(event->pos());
    if (this->drag.shifting || this->drag.moving) {
        const QPoint delta = cell - this->drag.originCell;
        if (delta != this->drag.delta) {
            this->drag.delta = delta;
            update();
        }
    } else {
        const QSize cells = mapCells();
        const QPoint c(qBound(0, cell.x(), cells.width() - 1), qBound(0, cell.y(), cells.height() - 1));
        const QPoint a = this->drag.originCell;
        const QRect band(QPoint(qMin(a.x(), c.x()), qMin(a.y(), c.y())), QPoint(qMax(a.x(), c.x()), qMax(a.y(), c.y())));
        if (band != this->drag.band) {
            this->drag.band = band;
            update();
        }
    }
}

void PreMapPixmapItem::dragEnd(QGraphicsSceneMouseEvent *) {
    const Drag finished = this->drag;
    this->drag = Drag();
    if (finished.shifting) {
        if (finished.delta != QPoint(0, 0))
            emit this->shiftRequested(finished.delta);
    } else if (finished.moving) {
        if (finished.delta != QPoint(0, 0))
            moveSelection(finished.delta, QStringLiteral("Move Porytiles"));
    } else if (finished.band.isValid()) {
        this->selection = finished.band;
        this->selectedLayer = activeLayer();
        emit this->selectionChanged();
    }
    emit this->strokeActiveChanged(false);
    update();
}

void PreMapPixmapItem::clearSelection() {
    if (!this->selection.isValid())
        return;
    this->selection = QRect();
    this->selectedLayer = -1;
    emit this->selectionChanged();
    update();
}

// The selected fields move by `delta`: what leaves the map is gone, the fields they leave become Porytile 0. One stroke.
void PreMapPixmapItem::moveSelection(QPoint delta, const QString &text) {
    if (!this->selection.isValid() || delta == QPoint(0, 0) || !this->preMap)
        return;
    const int layer = this->selectedLayer;
    if (!layerEditable(layer))
        return;
    const PorytileSelection block = pickRect(layer, this->selection);
    PreMapStroke record;
    record.text = text;
    QRect dirty;
    for (int y = 0; y < block.dims.height(); y++)
        for (int x = 0; x < block.dims.width(); x++)
            writeCell(record, layer, this->selection.x() + x, this->selection.y() + y, 0, dirty);
    const QRect target = this->selection.translated(delta);
    for (int y = 0; y < block.dims.height(); y++)
        for (int x = 0; x < block.dims.width(); x++)
            writeCell(record, layer, target.x() + x, target.y() + y, block.at(x, y), dirty);
    this->selection = target.intersected(QRect(QPoint(0, 0), mapCells()));
    if (!this->selection.isValid())
        this->selectedLayer = -1;
    emit this->selectionChanged();
    finishStroke(record, dirty);
    update();
}

void PreMapPixmapItem::deleteSelection() {
    if (!this->selection.isValid() || !layerEditable(this->selectedLayer))
        return;
    PreMapStroke record;
    record.text = QStringLiteral("Delete Porytiles");
    QRect dirty;
    for (int y = this->selection.top(); y <= this->selection.bottom(); y++)
        for (int x = this->selection.left(); x <= this->selection.right(); x++)
            writeCell(record, this->selectedLayer, x, y, 0, dirty);
    finishStroke(record, dirty);
}

bool PreMapPixmapItem::handleKey(QKeyEvent *event) {
    if (!this->active)
        return false;
    if (event->key() == Qt::Key_Escape && this->selection.isValid()) { clearSelection(); return true; }
    if (!this->selection.isValid())
        return false;
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelection(); return true;
    case Qt::Key_Left:  moveSelection(QPoint(-1, 0), QStringLiteral("Nudge Porytiles")); return true;
    case Qt::Key_Right: moveSelection(QPoint(1, 0), QStringLiteral("Nudge Porytiles")); return true;
    case Qt::Key_Up:    moveSelection(QPoint(0, -1), QStringLiteral("Nudge Porytiles")); return true;
    case Qt::Key_Down:  moveSelection(QPoint(0, 1), QStringLiteral("Nudge Porytiles")); return true;
    default: return false;
    }
}

bool PreMapPixmapItem::copySelection() {
    if (!this->selection.isValid())
        return false;
    this->clipboard = pickRect(this->selectedLayer, this->selection);
    return true;
}

// Pastes the clipboard at the field under the mouse (or at the selection's top-left when the mouse is not over the item), on the active layer; the part
// that hangs over the edge of the map is dropped.
bool PreMapPixmapItem::pasteAtHover() {
    if (!this->clipboard.isValid() || editingBehaviors())
        return false;
    const int layer = activeLayer();
    if (!layerEditable(layer))
        return false;
    const QPoint at = this->pointerValid ? this->pointerCell : (this->selection.isValid() ? this->selection.topLeft() : QPoint(0, 0));   // (beyond the edge too: what reaches onto the map is pasted)
    if (!QRect(at, this->clipboard.dims).intersects(QRect(QPoint(0, 0), mapCells())))
        return false;   // (it misses the map altogether: nothing is pasted and the selection stays)
    PreMapStroke record;
    record.text = QStringLiteral("Paste Porytiles");
    QRect dirty;
    for (int y = 0; y < this->clipboard.dims.height(); y++)
        for (int x = 0; x < this->clipboard.dims.width(); x++)
            writeCell(record, layer, at.x() + x, at.y() + y, this->clipboard.at(x, y), dirty);
    this->selection = QRect(at, this->clipboard.dims).intersected(QRect(QPoint(0, 0), mapCells()));
    this->selectedLayer = this->selection.isValid() ? layer : -1;
    emit this->selectionChanged();
    finishStroke(record, dirty);
    update();
    return true;
}
