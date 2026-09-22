#ifndef MAPTRANSFER_H
#define MAPTRANSFER_H

// CUSTOM ENGINE: the two conversions between the Porymap (design) view and the Finalmap (the real, ROM-facing map).
//
//  * WRITE to Finalmap: every field of the layout gets ONE real metatile built from its three porytiles
//    (Bottom -> tiles 0-3, Middle -> 4-7, Top -> 8-11) and its behavior (the one placed on the Behaviors tab, else the
//    one its porytiles give). Identical fields share one metatile. A metatile that already exists with exactly those
//    12 tiles and that behavior is REUSED; otherwise an EMPTY slot is taken (all 12 tiles are tile 0 -- the palette
//    does not matter -- and nothing in the project uses it and it carries no label); otherwise the tileset is grown by
//    one. Nothing else of the map changes: every field keeps its elevation (and collision bits, if the project has any).
//
//  * PULL to Porymap: the reverse. Every field's metatile is split into its three layers; a porytile with exactly those
//    4 tiles is reused, else a blank porytile slot is filled, else the tileset is grown. The metatile's behavior is
//    placed on the field, unless the porytiles already give exactly that behavior (then the field stays on Auto).
//
// Both work on ONE layout, are planned first (plan* -> counts + error, nothing touched) and only then applied, so the
// caller can show what will happen and abort without a half-written state. Growing a tileset never changes the
// primary/secondary id split (that is a project setting): it only raises that tileset's own metatile / porytile count
// up to the room the split leaves it.

#include "blockdata.h"
#include "metatile.h"
#include "tileset.h"

#include <QList>
#include <QString>
#include <QStringList>

class Layout;
class PreMap;
class Project;

namespace MapTransfer {

// One block (metatile or porytile) that the plan writes into a tileset.
struct BlockWrite {
    Tileset *tileset = nullptr;
    int index = 0;          // index inside that tileset's list
    uint16_t id = 0;        // the id the map refers to
    Metatile value;         // tiles + attributes to write
    QString label;          // empty = do not touch the label
    bool appended = false;  // true: the tileset had to grow for it
};

struct PushPlan {
    bool ok = false;
    QString error;              // non-empty: nothing can be done, nothing was touched
    QStringList notes;          // things worth telling the user (not errors)

    Blockdata blockdata;        // the new map, ready to be applied
    QList<BlockWrite> writes;   // the metatiles to create / overwrite
    int primaryCount = 0;       // metatile counts the tilesets must have afterwards
    int secondaryCount = 0;
    int primaryBefore = 0;      // ... and what they have now (the difference is how much each tileset grows)
    int secondaryBefore = 0;

    int fields = 0;             // fields of the map
    int distinct = 0;           // distinct metatiles the map needs
    int reused = 0;             // ... that already existed
    int filledEmpty = 0;        // ... that took an empty slot
    int appended = 0;           // ... that made a tileset grow
    int changedFields = 0;      // fields whose metatile id changes
    int labels = 0;             // labels the plan writes
};

struct PullPlan {
    bool ok = false;
    QString error;
    QStringList notes;

    QList<BlockWrite> writes;   // the porytiles to create
    int primaryCount = 0;       // porytile counts the tilesets must have afterwards
    int secondaryCount = 0;
    int primaryBefore = 0;      // ... and what they have now (the difference is how much each set grows)
    int secondaryBefore = 0;

    // the grids the pre-map gets (size = layout width*height, reading order): three layers of porytile ids + behaviors
    QVector<uint16_t> layers[3];
    QVector<uint16_t> behaviors;

    int fields = 0;
    int distinctPorytiles = 0;
    int reusedPorytiles = 0;     // porytiles that ALREADY existed and are used (one that this plan creates does not count)
    int createdPorytiles = 0;
    int placedBehaviors = 0;
    int changedFields = 0;      // fields whose three porytiles or behavior change

    // What the warning before a pull tells the user: how much the porytile set covers this map. A porytile "exists" when it
    // has tiles (a blank one is an unused slot).
    int existingPrimary = 0;     // porytiles with tiles in the primary / secondary tileset BEFORE the pull
    int existingSecondary = 0;
    int createdPrimary = 0;      // porytiles this pull creates in each
    int createdSecondary = 0;
    int missingMetatileFields = 0;   // fields whose metatile does not exist in these tilesets (they become Porytile 0)
    bool porytilesEmpty() const { return this->existingPrimary + this->existingSecondary == 0; }
    // true = the porytile set does not cover the map completely: something is missing, the user must be asked first
    bool needsWarning() const { return this->createdPorytiles > 0 || this->missingMetatileFields > 0; }
};

// Plans only: nothing in the project is touched. `layout` must be loaded (blockdata) and `preMap` must hold this
// layout's grid at the layout's size.
PushPlan planPush(Layout *layout, const PreMap *preMap, Project *project);
PullPlan planPull(Layout *layout, const PreMap *preMap, Project *project);

// Writes a plan into the tilesets. The caller applies the map / pre-map side and saves (see PushToFinalmapCommand).
void applyBlockWrites(const QList<BlockWrite> &writes, Tileset *primary, Tileset *secondary, BlockKind kind);

// What a field's three porytiles and behavior turn into. Public so the tests can check one field on its own.
Metatile buildMetatile(const PreMap *preMap, int x, int y, Tileset *primary, Tileset *secondary, uint32_t behavior);
// The behavior a field has in the DATA (placed one wins, else the topmost layer whose porytile has one; layer eyes are
// a view thing and never count here).
uint32_t fieldBehavior(const PreMap *preMap, int x, int y, const Tileset *primary, const Tileset *secondary);

} // namespace MapTransfer

#endif // MAPTRANSFER_H
