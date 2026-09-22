#include "behavioroverlayitem.h"
#include "premappixmapitem.h"
#include "core/premap.h"
#include "projectsheets.h"
#include "tileset.h"
#include "maplayout.h"

#include <QPainter>
#include <QHash>

void BehaviorOverlayItem::setHoverField(const QPoint &field) {
    if (!this->frame) {
        this->frame = new QGraphicsRectItem(0, 0, ProjectSheets::cellSize, ProjectSheets::cellSize, this);
        this->frame->setFlag(QGraphicsItem::ItemIgnoresParentOpacity);   // (the numbers may be faded out; the frame is what shows where the pencil paints)
        this->frame->setAcceptedMouseButtons(Qt::NoButton);
        QPen pen(QColor(200, 80, 230));
        pen.setCosmetic(true);
        pen.setWidth(2);
        this->frame->setPen(pen);
        this->frame->setBrush(Qt::NoBrush);
        this->frame->hide();
    }
    const bool valid = field.x() >= 0 && field.y() >= 0;
    if (valid)
        this->frame->setPos(field.x() * ProjectSheets::cellSize, field.y() * ProjectSheets::cellSize);
    this->frame->setVisible(valid);
}

void BehaviorOverlayItem::setActive(bool active) {
    this->active = active;
    setVisible(active);
    if (active)
        draw();
}

uint32_t BehaviorOverlayItem::shownAt(int x, int y) const {
    if (this->layers)
        return this->layers->effectiveBehaviorAt(x, y);
    // (no pre-map item: the plain rule without eye flags)
    if (!this->preMap || !this->layout)
        return 0;
    if (this->preMap->hasPlacedBehavior(x, y))
        return this->preMap->behaviorAt(x, y);
    uint32_t id = 0;
    for (int layer = 0; layer < PreMap::kLayers; layer++) {
        const Metatile *porytile = Tileset::getPorytile(this->preMap->at(layer, x, y), this->layout->tileset_primary, this->layout->tileset_secondary);
        if (porytile && porytile->behavior() != 0)
            id = porytile->behavior();
    }
    return id;
}

// EVERY field is visited, like every field of the Elevation tab shows its number: a behavior of 0x00 is a behavior too, drawn with
// its own colour and "00" (the fields that have none are the ones you want to find, and a transparent cell looked like a bug).
void BehaviorOverlayItem::forEachCell(const QRect &fields, const std::function<void(const QRect &, uint32_t, bool)> &visit) const {
    if (!this->preMap || !this->layout)
        return;
    const QRect area = fields.intersected(QRect(0, 0, this->preMap->width(), this->preMap->height()));
    for (int y = area.top(); y <= area.bottom(); y++) {
        for (int x = area.left(); x <= area.right(); x++) {
            visit(QRect(x * ProjectSheets::cellSize, y * ProjectSheets::cellSize, ProjectSheets::cellSize, ProjectSheets::cellSize),
                  shownAt(x, y), this->preMap->hasPlacedBehavior(x, y));
        }
    }
}

// Draws the given fields into the image: the sheet's cell for the behavior, a frame when it was placed by hand.
void BehaviorOverlayItem::paintCells(const QRect &fields) {
    QPainter painter(&this->image);
    // Source, not SourceOver: a cell replaces what was there, so a field that lost its behavior becomes clear again.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    const QRect area = fields.intersected(QRect(0, 0, this->preMap ? this->preMap->width() : 0, this->preMap ? this->preMap->height() : 0));
    painter.fillRect(QRect(area.x() * ProjectSheets::cellSize, area.y() * ProjectSheets::cellSize, area.width() * ProjectSheets::cellSize, area.height() * ProjectSheets::cellSize), Qt::transparent);
    QHash<uint32_t, QImage> cells;   // (one cut per behavior, not one per field)
    forEachCell(fields, [&](const QRect &rect, uint32_t id, bool placed) {
        auto found = cells.constFind(id);
        if (found == cells.constEnd()) {
            QImage cut = (this->sheet && !this->sheet->isNull()) ? ProjectSheets::behaviorCell(*this->sheet, static_cast<int>(id)) : QImage();
            if (cut.isNull())   // (no sheet, or a cell the sheet does not have: the built-in one, so a field never shows nothing)
                cut = ProjectSheets::renderBehaviorCell(static_cast<int>(id));
            found = cells.insert(id, cut);
        }
        painter.drawImage(rect.topLeft(), found.value());
        if (placed) {
            painter.setPen(QPen(Qt::white, 1));
            painter.drawRect(rect.adjusted(0, 0, -1, -1));
        }
        this->lastCellCount++;
    });
}

void BehaviorOverlayItem::draw() {
    if (!this->active || !this->layout)
        return;
    const QSize wanted(this->layout->getWidth() * Metatile::pixelWidth(), this->layout->getHeight() * Metatile::pixelHeight());
    if (this->image.size() != wanted)
        this->image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
    this->image.fill(Qt::transparent);
    this->lastCellCount = 0;
    if (this->preMap)
        paintCells(QRect(0, 0, this->preMap->width(), this->preMap->height()));
    setPixmap(QPixmap::fromImage(this->image));
}

void BehaviorOverlayItem::drawRegion(const QRect &mapPixels) {
    if (!this->active || !this->layout || !this->preMap)
        return;
    const QSize wanted(this->layout->getWidth() * Metatile::pixelWidth(), this->layout->getHeight() * Metatile::pixelHeight());
    if (this->image.size() != wanted) {   // (nothing drawn yet, or the map changed size: everything)
        draw();
        return;
    }
    const int cell = ProjectSheets::cellSize;
    const QRect fields(mapPixels.left() / cell, mapPixels.top() / cell, (mapPixels.right() / cell) - (mapPixels.left() / cell) + 1, (mapPixels.bottom() / cell) - (mapPixels.top() / cell) + 1);
    paintCells(fields);
    setPixmap(QPixmap::fromImage(this->image));
}

uint32_t BehaviorOverlayItem::behaviorIdAt(const QPoint &mapPixel) const {
    if (!this->preMap || mapPixel.x() < 0 || mapPixel.y() < 0)
        return 0;
    const int x = mapPixel.x() / ProjectSheets::cellSize, y = mapPixel.y() / ProjectSheets::cellSize;
    return this->preMap->contains(x, y) ? shownAt(x, y) : 0;
}
