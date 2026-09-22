#ifndef COLLISIONPIXMAPITEM_H
#define COLLISIONPIXMAPITEM_H

#include <QSpinBox>

#include "metatileselector.h"
#include "movementpermissionsselector.h"
#include "layoutpixmapitem.h"
#include "map.h"
#include "settings.h"

class CollisionPixmapItem : public LayoutPixmapItem {
    Q_OBJECT
public:
    // CUSTOM ENGINE: this is now the ELEVATION layer -- map.bin has no collision bits any more, so
    // painting only ever sets the block's elevation (0-7). The class keeps its old name.
    CollisionPixmapItem(Layout *layout, QSpinBox * selectedElevation, MetatileSelector *metatileSelector, Settings *settings, qreal *opacity)
        : LayoutPixmapItem(layout, metatileSelector, settings){
        this->selectedElevation = selectedElevation;
        this->opacity = opacity;
        layout->setCollisionItem(this);
    }
    QSpinBox * selectedElevation;
    qreal *opacity;
    void updateMovementPermissionSelection(QGraphicsSceneMouseEvent *event);
    virtual void paint(QGraphicsSceneMouseEvent*) override;
    virtual void floodFill(QGraphicsSceneMouseEvent*) override;
    virtual void magicFill(QGraphicsSceneMouseEvent*) override;
    virtual void pick(QGraphicsSceneMouseEvent*) override;
    void draw(bool ignoreCache = false) override;

private:
    void updateSelection(QPoint pos);
};

#endif // COLLISIONPIXMAPITEM_H
