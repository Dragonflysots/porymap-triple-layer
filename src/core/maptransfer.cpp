#include "core/maptransfer.h"
#include "core/premap.h"
#include "maplayout.h"
#include "project.h"
#include "config.h"
#include "log.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace {

// ---- keys ------------------------------------------------------------------------------------------------------------------------------

// What makes two metatiles the same for reuse: the 12 tiles and the behavior. NOT the layer type: it is the legacy
// nibble that only the shop preview and the decoration code still read, and duplicating metatiles over it would eat the
// id space. A REUSED metatile keeps its layer type; a metatile this generator creates gets it by the rule below.
// A tile with id 0 draws nothing whatever its palette or flip bits are (the user's definition of "empty"), so it counts as plain
// tile 0 here: a metatile whose blank layer carries a stray flip or palette is the very same metatile as one without.
QString blockKey(const Metatile &block, uint32_t behavior) {
    QString key;
    key.reserve(block.tiles.size() * 5 + 6);
    for (const Tile &tile : block.tiles)
        key += QString::number(tile.tileId == 0 ? 0 : tile.rawValue(), 16) + QLatin1Char(',');
    key += QLatin1Char('#') + QString::number(behavior, 16);
    return key;
}

// A porytile is matched by its tiles alone (its behavior is a property of the block, not of the picture), but the key must be
// built the SAME way as the allocator builds it, which is why both go through blockKey.
QString tilesKey(const QList<Tile> &tiles) {
    Metatile block;
    block.tiles = tiles;
    return blockKey(block, 0);
}

// "Empty" for a slot that may be taken: every tile is tile 0. The palette (and the flip bits) do not matter, as the
// user put it: such a metatile draws nothing at all.
bool allTilesZero(const Metatile *block) {
    if (!block)
        return false;
    for (const Tile &tile : block->tiles)
        if (tile.tileId != 0)
            return false;
    return true;
}

// The top layer of a generated metatile decides its layer type: COVERED while it draws nothing, else NORMAL (the shop
// preview reads it; see the emulator proof in HANDOVER.md).
void setGeneratedLayerType(Metatile *metatile) {
    if (!projectConfig.tripleLayerMetatilesEnabled || !projectConfig.metatileLayerTypeMask)
        return;
    const int perLayer = Metatile::tilesPerLayer();
    bool topBlank = true;
    for (int i = 0; i < perLayer; i++)
        if (metatile->tiles.value(2 * perLayer + i).tileId != 0)
            topBlank = false;
    metatile->setAttribute(Metatile::Attr::LayerType, topBlank ? Metatile::LayerType::Covered : Metatile::LayerType::Normal);
}

// ---- usage across the project ----------------------------------------------------------------------------------------------------------

// How often every metatile id is used by a map that shares one of these two tilesets (map.bin AND border). Same rule as
// the Tileset Editor's "Show Unused"; it loads the layouts it has to.
QVector<int> metatileUsage(Project *project, const Tileset *primary, const Tileset *secondary, const QString &rewrittenLayoutId,
                           bool *primaryComplete, bool *secondaryComplete) {
    QVector<int> used(Project::getNumMetatilesTotal(), 0);
    if (primaryComplete) *primaryComplete = true;
    if (secondaryComplete) *secondaryComplete = true;
    if (!project) {
        if (primaryComplete) *primaryComplete = false;
        if (secondaryComplete) *secondaryComplete = false;
        return used;
    }
    const int numPrimary = Project::getNumMetatilesPrimary();
    for (const QString &layoutId : project->layoutIds()) {
        Layout *layout = project->getLayout(layoutId);
        if (!layout)
            continue;
        const bool usesPrimary = primary && layout->tileset_primary_label == primary->name;
        const bool usesSecondary = secondary && layout->tileset_secondary_label == secondary->name;
        if (!usesPrimary && !usesSecondary)
            continue;
        if (!project->loadLayout(layoutId)) {
            // A layout that cannot be read may well use metatiles of the tileset it claims; we just cannot see which. Only
            // THAT tileset loses its empty slots -- one broken map elsewhere must not disable the rule everywhere.
            if (usesPrimary && primaryComplete) *primaryComplete = false;
            if (usesSecondary && secondaryComplete) *secondaryComplete = false;
            continue;
        }
        auto count = [&](const Blockdata &blocks) {
            for (const Block &block : blocks) {
                const uint16_t id = block.metatileId();
                if (id >= used.size())
                    continue;
                if (id < numPrimary ? usesPrimary : usesSecondary)
                    used[id]++;
            }
        };
        if (layoutId != rewrittenLayoutId)
            count(layout->blockdata);   // (the map this push rewrites frees its own ids: every field of it is assigned anew)
        count(layout->border);          // (the border is NOT rewritten by a push)
    }
    return used;
}

// Which porytile ids the OTHER layouts' layers.json files hold, so a blank slot that some other map paints with is not
// filled with art. Only files that exist are read (a layout without layers.json has no porytiles at all).
QSet<uint16_t> porytileUsage(Project *project, const QString &exceptLayoutId) {
    QSet<uint16_t> used;
    if (!project)
        return used;
    const char *names[PreMap::kLayers] = { "bottom", "middle", "top" };
    for (const QString &layoutId : project->layoutIds()) {
        if (layoutId == exceptLayoutId)
            continue;
        const QString path = PreMap::filepathFor(layoutId);
        if (!QFile::exists(path))
            continue;
        // Read the ids straight out of the file: going through PreMap::load would crop them to a layout size that is not
        // known until that layout is loaded, and a cropped read would make a porytile someone else paints with look free.
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject layers = QJsonDocument::fromJson(file.readAll()).object().value("layers").toObject();
        for (int layer = 0; layer < PreMap::kLayers; layer++)
            for (const QJsonValue &value : layers.value(names[layer]).toArray())
                used.insert(static_cast<uint16_t>(value.toInt(0)));
    }
    return used;
}

// A block can only live in the PRIMARY tileset when everything it refers to is primary: the tiles and the palettes are both
// split at a fixed index, and a primary block is seen by every secondary tileset the primary is paired with.
bool needsSecondaryTileset(const QList<Tile> &tiles) {
    for (const Tile &tile : tiles)
        if (tile.tileId >= Project::getNumTilesPrimary() || tile.palette >= Project::getNumPalettesPrimary())
            return true;
    return false;
}

// ---- the allocator ---------------------------------------------------------------------------------------------------------------------

// Hands out slots in the two tilesets for blocks of one kind: an existing identical block, else an empty slot that
// nothing uses, else a new one at the end (the tileset grows within the room its half of the id space leaves it).
// Every id, including 0, is an ordinary candidate: an empty slot can be BOTH the block a blank field matches and a
// slot to fill. Whichever happens first wins, and the other use is dropped -- a slot that was handed out as a match
// is no longer free, and a slot that gets filled loses the key it had while it was blank.
class Allocator {
public:
    Allocator(BlockKind kind, Tileset *primary, Tileset *secondary, const QVector<int> &used, const QSet<uint16_t> &usedIds,
              bool mayFillPrimary = true, bool mayFillSecondary = true)
        : kind(kind), primary(primary), secondary(secondary) {
        this->firstSecondaryId = Project::getNumMetatilesPrimary();
        this->primaryCount = primary ? primary->numBlocks(kind) : 0;
        this->secondaryCount = secondary ? secondary->numBlocks(kind) : 0;
        this->primaryCap = Project::getNumMetatilesPrimary();
        this->secondaryCap = Project::getNumMetatilesSecondary();

        auto scan = [&](Tileset *tileset, bool isSecondary) {
            if (!tileset)
                return;
            const QList<Metatile*> &blocks = tileset->blocks(kind);
            for (int i = 0; i < blocks.size(); i++) {
                const uint16_t id = static_cast<uint16_t>((isSecondary ? this->firstSecondaryId : 0) + i);
                const Metatile *block = blocks.at(i);
                if (!block)
                    continue;
                const QString key = blockKey(*block, kind == BlockKind::Metatile ? block->behavior() : 0);
                this->candidates[key].append(id);   // ascending: the lowest id is the one that gets reused
                const bool labelled = tileset->blockLabels(kind).contains(id) && !tileset->blockLabels(kind).value(id).isEmpty();
                const bool inUse = (id < used.size() && used.at(id) > 0) || usedIds.contains(id);
                if ((isSecondary ? mayFillSecondary : mayFillPrimary) && allTilesZero(block) && !labelled && !inUse && block->behavior() == 0) {
                    (isSecondary ? this->freeSecondary : this->freePrimary).append(id);
                    this->keyWhileBlank.insert(id, key);
                }
            }
        };
        scan(primary, false);
        scan(secondary, true);
    }

    // An identical block that is already there. Taking it also takes it OUT of the free slots: this map uses it now (but it
    // stays a candidate -- its content did not change).
    uint16_t lookupExisting(const QString &key, bool *found) {
        auto it = this->candidates.constFind(key);
        *found = it != this->candidates.constEnd() && !it.value().isEmpty();
        if (!*found)
            return 0;
        const uint16_t id = it.value().first();
        claim(id);
        return id;
    }

    // A block that is already there is used by this map: it is no free slot any more (its content did not change, so it stays a candidate).
    void claim(uint16_t id) {
        this->freePrimary.removeAll(id);
        this->freeSecondary.removeAll(id);
    }

    // Takes a slot for `value`. Returns false when there is no room left anywhere.
    // The order is the user's "make room by yourself" rule: a block goes into the tileset it BELONGS in -- the primary one, unless it
    // needs secondary tiles or palettes -- and there an empty slot is taken first and then the tileset is made longer (Change
    // Dimension), up to the limit its half of the id space allows. Only when the primary tileset cannot take another block does a block
    // that does not need the secondary one move on to it. (Before, a block took an empty slot of the SECONDARY tileset as soon as the
    // primary one had none, so a full primary tileset filled the secondary one with blocks that never needed it.)
    bool allocate(const Metatile &value, bool needsSecondary, MapTransfer::BlockWrite *out) {
        struct Target { Tileset *tileset; bool isSecondary; QList<uint16_t> *free; int *count; int cap; };
        QList<Target> targets;
        if (!needsSecondary && this->primary)
            targets.append(Target{ this->primary, false, &this->freePrimary, &this->primaryCount, this->primaryCap });
        if (this->secondary)
            targets.append(Target{ this->secondary, true, &this->freeSecondary, &this->secondaryCount, this->secondaryCap });

        for (const Target &target : targets) {
            if (!target.free->isEmpty()) {   // an empty slot of this tileset
                const uint16_t id = target.free->takeFirst();
                dropBlankKey(id);
                *out = makeWrite(target.tileset, target.isSecondary, id, value, false);
                return true;
            }
            if (*target.count < target.cap) {   // ... else this tileset grows by one
                const int index = (*target.count)++;
                *out = makeWrite(target.tileset, target.isSecondary, static_cast<uint16_t>((target.isSecondary ? this->firstSecondaryId : 0) + index), value, true);
                return true;
            }
        }
        return false;
    }

    void remember(const QString &key, uint16_t id) { this->candidates[key].prepend(id); }
    int counts(bool isSecondary) const { return isSecondary ? this->secondaryCount : this->primaryCount; }

private:
    // A slot that is about to be filled is no longer the block its old key described -- but every OTHER block with that key
    // still is, so only this id is dropped from the list.
    void dropBlankKey(uint16_t id) {
        const QString key = this->keyWhileBlank.take(id);
        if (key.isEmpty())
            return;
        auto it = this->candidates.find(key);
        if (it == this->candidates.end())
            return;
        it.value().removeAll(id);
        if (it.value().isEmpty())
            this->candidates.erase(it);
    }

    MapTransfer::BlockWrite makeWrite(Tileset *tileset, bool isSecondary, uint16_t id, const Metatile &value, bool appended) const {
        MapTransfer::BlockWrite write;
        write.tileset = tileset;
        write.index = id - (isSecondary ? this->firstSecondaryId : 0);
        write.id = id;
        write.value = value;
        write.appended = appended;
        return write;
    }

    BlockKind kind;
    Tileset *primary;
    Tileset *secondary;
    int firstSecondaryId;
    int primaryCount, secondaryCount, primaryCap, secondaryCap;
    QHash<QString, QList<uint16_t>> candidates;
    QHash<uint16_t, QString> keyWhileBlank;
    QList<uint16_t> freePrimary, freeSecondary;
};

// ---- the safety net: a plan must reproduce the map exactly, or it is not offered ----------------------------------------------------------

// `got` shows what `expected` asks for: the same tile ids everywhere and -- where a tile is drawn (id != 0) -- the same palette and flips.
// (A tile with id 0 draws nothing whatever its palette is: that is the user's definition of "empty".)
bool sameTiles(const QList<Tile> &expected, const QList<Tile> &got) {
    if (expected.size() != got.size())
        return false;
    for (int i = 0; i < expected.size(); i++) {
        if (expected.at(i).tileId != got.at(i).tileId)
            return false;
        if (expected.at(i).tileId != 0 && expected.at(i).rawValue() != got.at(i).rawValue())
            return false;
    }
    return true;
}

QString describeSlot(const char *what, int id) {
    return QString("%1 %2").arg(QLatin1String(what)).arg(id);
}

// The writes of a plan may only ever fill an EMPTY slot or a new one at the end -- never a block that has art (or a
// label or a behavior) in it. Entry 0 is an ordinary id here: it may be filled exactly like any other empty slot.
// Returns an error text, or nothing.
QString checkWritesOnlyTouchEmptySlots(const QList<MapTransfer::BlockWrite> &writes, BlockKind kind, const Tileset *primary, const Tileset *secondary) {
    const char *what = kind == BlockKind::Metatile ? "metatile" : "porytile";
    QSet<uint16_t> seen;
    for (const MapTransfer::BlockWrite &write : writes) {
        if (seen.contains(write.id))
            return QString("Internal check failed: the plan wanted to write %1 twice.").arg(describeSlot(what, write.id));
        seen.insert(write.id);
        if (write.appended)
            continue;
        const Metatile *existing = Tileset::getBlock(kind, write.id, primary, secondary);
        if (!existing)
            return QString("Internal check failed: the plan wanted to write %1, which does not exist.").arg(describeSlot(what, write.id));
        if (!allTilesZero(existing) || (kind == BlockKind::Porytile && existing->behavior() != 0))
            return QString("Internal check failed: the plan wanted to overwrite %1, which is not empty.").arg(describeSlot(what, write.id));
    }
    return QString();
}

// Push: every field's metatile must hold exactly the three porytiles of that field and its behavior.
QString verifyPush(const MapTransfer::PushPlan &plan, const PreMap *preMap, Layout *layout, Tileset *primary, Tileset *secondary) {
    QString problem = checkWritesOnlyTouchEmptySlots(plan.writes, BlockKind::Metatile, primary, secondary);
    if (!problem.isEmpty())
        return problem;
    QHash<uint16_t, const Metatile *> written;
    for (const MapTransfer::BlockWrite &write : plan.writes)
        written.insert(write.id, &write.value);
    const int width = layout->getWidth();
    for (int index = 0; index < plan.blockdata.size() && width > 0; index++) {
        const int x = index % width, y = index / width;
        const uint16_t id = plan.blockdata.at(index).metatileId();
        const Metatile *got = written.value(id, nullptr);
        if (!got)
            got = Tileset::getMetatile(id, primary, secondary);
        const uint32_t behavior = MapTransfer::fieldBehavior(preMap, x, y, primary, secondary);
        const Metatile expected = MapTransfer::buildMetatile(preMap, x, y, primary, secondary, behavior);
        if (!got || !sameTiles(expected.tiles, got->tiles) || static_cast<uint32_t>(got->behavior()) != behavior)
            return QString("Internal check failed: field (%1,%2) would not look like its porytiles after writing (metatile %3). Nothing was changed.").arg(x).arg(y).arg(id);
    }
    return QString();
}

// Pull: every layer of every field must show exactly the tiles of that layer of the field's metatile (whichever porytile
// that layer landed on -- a blank layer is allocated like any other content, see planPull, so this does not assume any
// particular id is blank).
QString verifyPull(const MapTransfer::PullPlan &plan, Layout *layout, Tileset *primary, Tileset *secondary) {
    QString problem = checkWritesOnlyTouchEmptySlots(plan.writes, BlockKind::Porytile, primary, secondary);
    if (!problem.isEmpty())
        return problem;
    QHash<uint16_t, const Metatile *> written;
    for (const MapTransfer::BlockWrite &write : plan.writes)
        written.insert(write.id, &write.value);
    const int perLayer = Metatile::tilesPerLayer();
    const int width = layout->getWidth();
    for (int index = 0; index < plan.fields && width > 0; index++) {
        const int x = index % width, y = index / width;
        const Metatile *metatile = Tileset::getMetatile(layout->blockdata.at(index).metatileId(), primary, secondary);
        if (!metatile)
            continue;   // (reported as a missing metatile, the field stays empty)
        for (int layer = 0; layer < PreMap::kLayers; layer++) {
            QList<Tile> expected;
            for (int i = 0; i < perLayer; i++)
                expected.append(metatile->tiles.value(layer * perLayer + i));
            const uint16_t id = plan.layers[layer].at(index);
            const Metatile *got = written.value(id, nullptr);
            if (!got)
                got = Tileset::getPorytile(id, primary, secondary);
            if (!got || !sameTiles(expected, got->tiles.mid(0, perLayer)))
                return QString("Internal check failed: the %1 layer of field (%2,%3) would not look like the Finalmap after the pull (porytile %4). Nothing was changed.")
                           .arg(Metatile::getLayerName(layer)).arg(x).arg(y).arg(id);
        }
    }
    return QString();
}

// ---- labels ----------------------------------------------------------------------------------------------------------------------------

// Gives a generated metatile the label "<base>_<n>" of its porytile: the lowest number that no label of that tileset
// uses yet. Existing labels are never touched or renumbered (they are #defines other code may refer to).
class LabelMaker {
public:
    explicit LabelMaker(Tileset *primary, Tileset *secondary) {
        for (Tileset *tileset : { primary, secondary })
            if (tileset)
                for (const QString &label : tileset->metatileLabels.values())
                    this->taken[tileset].insert(label);
    }

    QString next(Tileset *tileset, const QString &base) {
        if (!tileset || base.isEmpty())
            return QString();
        QSet<QString> &used = this->taken[tileset];
        for (int n = 1; n < 100000; n++) {
            const QString candidate = QString("%1_%2").arg(base).arg(n);
            if (!used.contains(candidate)) {
                used.insert(candidate);
                return candidate;
            }
        }
        return QString();
    }

private:
    QHash<Tileset*, QSet<QString>> taken;
};

} // namespace

namespace MapTransfer {

uint32_t fieldBehavior(const PreMap *preMap, int x, int y, const Tileset *primary, const Tileset *secondary) {
    if (!preMap)
        return 0;
    if (preMap->hasPlacedBehavior(x, y))
        return preMap->behaviorAt(x, y);
    uint32_t id = 0;
    for (int layer = 0; layer < PreMap::kLayers; layer++) {   // bottom first: a higher layer with a behavior wins
        const Metatile *porytile = Tileset::getPorytile(preMap->at(layer, x, y), primary, secondary);
        if (porytile && porytile->behavior() != 0)
            id = porytile->behavior();
    }
    return id;
}

Metatile buildMetatile(const PreMap *preMap, int x, int y, Tileset *primary, Tileset *secondary, uint32_t behavior) {
    const int perLayer = Metatile::tilesPerLayer();
    Metatile metatile(Tileset::tilesPerBlock(BlockKind::Metatile));
    for (int layer = 0; layer < PreMap::kLayers; layer++) {
        const Metatile *porytile = preMap ? Tileset::getPorytile(preMap->at(layer, x, y), primary, secondary) : nullptr;
        for (int i = 0; i < perLayer; i++)
            metatile.tiles[layer * perLayer + i] = porytile ? porytile->tiles.value(i) : Tile();
    }
    metatile.setBehavior(static_cast<int>(behavior));
    setGeneratedLayerType(&metatile);
    return metatile;
}

// ---- push ------------------------------------------------------------------------------------------------------------------------------

PushPlan planPush(Layout *layout, const PreMap *preMap, Project *project) {
    PushPlan plan;
    if (!layout || !preMap || !project) {
        plan.error = QStringLiteral("No map is open.");
        return plan;
    }
    Tileset *primary = layout->tileset_primary, *secondary = layout->tileset_secondary;
    if (!primary || !secondary) {
        plan.error = QStringLiteral("This map's tilesets are not loaded.");
        return plan;
    }
    const QSize size(layout->getWidth(), layout->getHeight());
    if (preMap->size() != size) {
        plan.error = QString("The porytile grid is %1x%2 but the map is %3x%4. Open the map again so the grid follows it.")
                         .arg(preMap->width()).arg(preMap->height()).arg(size.width()).arg(size.height());
        return plan;
    }
    if (layout->blockdata.size() != size.width() * size.height()) {
        plan.error = QStringLiteral("This map's blockdata is not loaded.");
        return plan;
    }
    if (Tileset::tilesPerBlock(BlockKind::Metatile) < PreMap::kLayers * Metatile::tilesPerLayer()) {
        plan.error = QStringLiteral("This project's metatiles do not have three layers (12 tiles). The Porymap view needs triple-layer metatiles.");
        return plan;
    }

    bool primaryComplete = true, secondaryComplete = true;
    const QVector<int> used = metatileUsage(project, primary, secondary, layout->id, &primaryComplete, &secondaryComplete);
    Allocator allocator(BlockKind::Metatile, primary, secondary, used, QSet<uint16_t>(), primaryComplete, secondaryComplete);
    if (!primaryComplete || !secondaryComplete)
        plan.notes << QString("A layout of this project could not be read, so the empty slots of the %1 tileset were left alone (new metatiles are added instead).")
                          .arg(!primaryComplete && !secondaryComplete ? "primary and secondary" : (primaryComplete ? "secondary" : "primary"));
    LabelMaker labels(primary, secondary);

    plan.blockdata = layout->blockdata;
    plan.fields = size.width() * size.height();
    QHash<QString, uint16_t> assigned;
    QSet<uint16_t> writtenIds, countedExisting;   // (ids this plan fills / grows; existing ids already counted as "reused")

    for (int y = 0; y < size.height(); y++) {
        for (int x = 0; x < size.width(); x++) {
            const int index = y * size.width() + x;
            const uint32_t behavior = fieldBehavior(preMap, x, y, primary, secondary);
            const Metatile metatile = buildMetatile(preMap, x, y, primary, secondary, behavior);
            const QString key = blockKey(metatile, behavior);

            // The field's OWN metatile stays when it already is what the porytiles give (same tiles -- tile 0 is nothing -- and behavior): writing a map
            // that was just pulled, or one that was changed in a few places only, changes nothing else, and two metatiles that look alike (duplicates that
            // differ only in what the view does not show) are not merged. (An id this plan itself fills is no longer what it was, so it cannot stay.)
            const uint16_t currentId = plan.blockdata.at(index).metatileId();
            const Metatile *current = writtenIds.contains(currentId) ? nullptr : Tileset::getMetatile(currentId, primary, secondary);
            const bool keepCurrent = current && current->tiles.size() == metatile.tiles.size() && blockKey(*current, current->behavior()) == key;

            uint16_t id = 0;
            auto seen = assigned.constFind(key);
            if (keepCurrent) {
                id = currentId;
                if (!countedExisting.contains(id)) {
                    countedExisting.insert(id);
                    allocator.claim(id);
                    plan.distinct++;
                    plan.reused++;
                }
                if (seen == assigned.constEnd())
                    assigned.insert(key, id);
            } else if (seen != assigned.constEnd()) {
                id = seen.value();
            } else {
                bool found = false;
                id = allocator.lookupExisting(key, &found);
                if (found) {
                    if (!countedExisting.contains(id)) {
                        countedExisting.insert(id);
                        plan.distinct++;
                        plan.reused++;
                    }
                } else {
                    plan.distinct++;
                    const bool needsSecondary = needsSecondaryTileset(metatile.tiles);
                    BlockWrite write;
                    if (!allocator.allocate(metatile, needsSecondary, &write)) {
                        plan.error = QString("There is no room for a new metatile: the primary tileset holds %1 and the secondary %2, "
                                             "which is all the id space allows. Free some metatiles or use fewer different porytile combinations.")
                                         .arg(primary->numMetatiles()).arg(secondary->numMetatiles());
                        plan.writes.clear();
                        return plan;
                    }
                    const QString base = Tileset::getOwnedBlockLabel(BlockKind::Porytile, preMap->at(2, x, y), primary, secondary);
                    const QString middle = Tileset::getOwnedBlockLabel(BlockKind::Porytile, preMap->at(1, x, y), primary, secondary);
                    const QString bottom = Tileset::getOwnedBlockLabel(BlockKind::Porytile, preMap->at(0, x, y), primary, secondary);
                    const QString chosen = !base.isEmpty() ? base : (!middle.isEmpty() ? middle : bottom);   // topmost porytile with a base label
                    write.label = labels.next(write.tileset, chosen);
                    if (!write.label.isEmpty())
                        plan.labels++;
                    plan.writes.append(write);
                    id = write.id;
                    writtenIds.insert(id);
                    if (write.appended) plan.appended++; else plan.filledEmpty++;
                    allocator.remember(key, id);
                }
                assigned.insert(key, id);
            }

            Block block = plan.blockdata.at(index);   // the field keeps its elevation (and collision bits, if any)
            if (block.metatileId() != id)
                plan.changedFields++;
            block.setMetatileId(id);
            plan.blockdata[index] = block;
        }
    }

    plan.primaryBefore = primary->numMetatiles();
    plan.secondaryBefore = secondary->numMetatiles();
    plan.primaryCount = qMax(primary->numMetatiles(), allocator.counts(false));
    plan.secondaryCount = qMax(secondary->numMetatiles(), allocator.counts(true));
    const QString problem = verifyPush(plan, preMap, layout, primary, secondary);
    if (!problem.isEmpty()) {
        plan.error = problem;
        plan.writes.clear();
        return plan;
    }
    if (plan.filledEmpty > 0)
        plan.notes << QString("%1 empty metatile slot(s) that nothing used were filled.").arg(plan.filledEmpty);
    if (plan.primaryCount > plan.primaryBefore)
        plan.notes << QString("The primary tileset grows by itself from %1 to %2 metatiles (limit %3).")
                          .arg(plan.primaryBefore).arg(plan.primaryCount).arg(Project::getNumMetatilesPrimary());
    if (plan.secondaryCount > plan.secondaryBefore)
        plan.notes << QString("The secondary tileset grows by itself from %1 to %2 metatiles (limit %3).")
                          .arg(plan.secondaryBefore).arg(plan.secondaryCount).arg(Project::getNumMetatilesSecondary());
    plan.ok = true;
    return plan;
}

// ---- pull ------------------------------------------------------------------------------------------------------------------------------

PullPlan planPull(Layout *layout, const PreMap *preMap, Project *project) {
    PullPlan plan;
    if (!layout || !preMap || !project) {
        plan.error = QStringLiteral("No map is open.");
        return plan;
    }
    Tileset *primary = layout->tileset_primary, *secondary = layout->tileset_secondary;
    if (!primary || !secondary) {
        plan.error = QStringLiteral("This map's tilesets are not loaded.");
        return plan;
    }
    const QSize size(layout->getWidth(), layout->getHeight());
    if (layout->blockdata.size() != size.width() * size.height()) {
        plan.error = QStringLiteral("This map's blockdata is not loaded.");
        return plan;
    }
    if (Tileset::tilesPerBlock(BlockKind::Metatile) < PreMap::kLayers * Metatile::tilesPerLayer()) {
        plan.error = QStringLiteral("This project's metatiles do not have three layers (12 tiles). The Porymap view needs triple-layer metatiles.");
        return plan;
    }

    // Porytile 0 is the default fill of every layer, but it is an ordinary porytile now: a blank layer is looked up and
    // allocated exactly like any other content below, so it lands on whichever porytile is (still) blank -- id 0 if that
    // one still is, otherwise whatever else matches or gets created.
    Allocator allocator(BlockKind::Porytile, primary, secondary, QVector<int>(), porytileUsage(project, layout->id));
    const int perLayer = Metatile::tilesPerLayer();

    plan.fields = size.width() * size.height();
    for (int layer = 0; layer < 3; layer++)
        plan.layers[layer] = QVector<uint16_t>(plan.fields, 0);
    plan.behaviors = QVector<uint16_t>(plan.fields, PreMap::kAutoBehavior);

    for (const Metatile *porytile : primary->porytiles())
        if (!allTilesZero(porytile)) plan.existingPrimary++;
    for (const Metatile *porytile : secondary->porytiles())
        if (!allTilesZero(porytile)) plan.existingSecondary++;

    QSet<uint16_t> missing, createdHere, reusedIds;
    for (int y = 0; y < size.height(); y++) {
        for (int x = 0; x < size.width(); x++) {
            const int index = y * size.width() + x;
            const uint16_t metatileId = layout->blockdata.at(index).metatileId();
            const Metatile *metatile = Tileset::getMetatile(metatileId, primary, secondary);
            if (!metatile) {
                missing.insert(metatileId);
                plan.missingMetatileFields++;
                continue;   // (stays Porytile 0 / Auto)
            }
            uint32_t derived = 0;
            for (int layer = 0; layer < 3; layer++) {
                QList<Tile> tiles;
                for (int i = 0; i < perLayer; i++)
                    tiles.append(metatile->tiles.value(layer * perLayer + i));

                // A blank layer (every tile id 0) is looked up and allocated exactly like any other content: it lands on
                // whichever porytile is already blank (the lowest id, so ordinarily 0), or a new blank one is created if
                // none is free. Entry 0 is not assumed to be blank any more -- see ARCHITECTURE.md.
                const QString key = tilesKey(tiles);
                bool found = false;
                uint16_t porytileId = allocator.lookupExisting(key, &found);
                if (found) {
                    // count porytiles, not fields -- and only those that existed before: one this plan just created is not "reused"
                    if (!createdHere.contains(porytileId) && !reusedIds.contains(porytileId)) {
                        reusedIds.insert(porytileId);
                        plan.reusedPorytiles++;
                    }
                } else {
                    Metatile porytile(Tileset::tilesPerBlock(BlockKind::Porytile));
                    for (int i = 0; i < perLayer; i++)
                        porytile.tiles[i] = tiles.at(i);
                    const bool needsSecondary = needsSecondaryTileset(tiles);
                    BlockWrite write;
                    if (!allocator.allocate(porytile, needsSecondary, &write)) {
                        plan.error = QString("There is no room for a new porytile: the primary tileset holds %1 and the secondary %2. "
                                             "Raise the porytile count in the Tileset Editor (Change Number of Porytiles) and try again.")
                                         .arg(primary->numPorytiles()).arg(secondary->numPorytiles());
                        plan.writes.clear();
                        return plan;
                    }
                    plan.writes.append(write);
                    plan.createdPorytiles++;
                    (write.tileset == secondary ? plan.createdSecondary : plan.createdPrimary)++;
                    porytileId = write.id;
                    createdHere.insert(porytileId);
                    allocator.remember(key, porytileId);
                }
                plan.layers[layer][index] = porytileId;
                // A porytile this plan creates carries no behavior yet; a reused one keeps the behavior it has.
                const Metatile *assignedPorytile = createdHere.contains(porytileId) ? nullptr : Tileset::getPorytile(porytileId, primary, secondary);
                const uint32_t porytileBehavior = assignedPorytile ? assignedPorytile->behavior() : 0;
                if (porytileBehavior != 0)
                    derived = porytileBehavior;
            }
            // The field keeps the metatile's behavior exactly: placed, unless the porytiles already give the same.
            const uint32_t behavior = metatile->behavior();
            if (behavior != derived) {
                plan.behaviors[index] = static_cast<uint16_t>(behavior);
                plan.placedBehaviors++;
            }
        }
    }

    for (int i = 0; i < plan.fields; i++) {   // fields whose porytiles or behavior would change
        const int x = i % size.width(), y = i / size.width();
        bool differs = preMap->size() != size || preMap->behaviorAt(x, y) != plan.behaviors.at(i);
        for (int layer = 0; layer < PreMap::kLayers && !differs; layer++)
            differs = preMap->at(layer, x, y) != plan.layers[layer].at(i);
        if (differs)
            plan.changedFields++;
    }
    plan.distinctPorytiles = plan.reusedPorytiles + plan.createdPorytiles;
    plan.primaryBefore = primary->numPorytiles();
    plan.secondaryBefore = secondary->numPorytiles();
    plan.primaryCount = qMax(primary->numPorytiles(), allocator.counts(false));
    plan.secondaryCount = qMax(secondary->numPorytiles(), allocator.counts(true));
    const QString problem = verifyPull(plan, layout, primary, secondary);
    if (!problem.isEmpty()) {
        plan.error = problem;
        plan.writes.clear();
        return plan;
    }
    if (plan.primaryCount > plan.primaryBefore)
        plan.notes << QString("The primary porytile set grows by itself from %1 to %2 entries (limit %3).")
                          .arg(plan.primaryBefore).arg(plan.primaryCount).arg(Project::getNumMetatilesPrimary());
    if (plan.secondaryCount > plan.secondaryBefore)
        plan.notes << QString("The secondary porytile set grows by itself from %1 to %2 entries (limit %3).")
                          .arg(plan.secondaryBefore).arg(plan.secondaryCount).arg(Project::getNumMetatilesSecondary());
    if (plan.missingMetatileFields > 0)
        plan.notes << QString("%1 field(s) use %2 metatile(s) that do not exist in these tilesets; they become Porytile 0.").arg(plan.missingMetatileFields).arg(missing.size());
    plan.ok = true;
    return plan;
}

void applyBlockWrites(const QList<BlockWrite> &writes, Tileset *primary, Tileset *secondary, BlockKind kind) {
    // grow first: a write may sit past the current end
    int wantedPrimary = primary ? primary->numBlocks(kind) : 0;
    int wantedSecondary = secondary ? secondary->numBlocks(kind) : 0;
    for (const BlockWrite &write : writes) {
        if (write.tileset == primary) wantedPrimary = qMax(wantedPrimary, write.index + 1);
        if (write.tileset == secondary) wantedSecondary = qMax(wantedSecondary, write.index + 1);
    }
    if (primary && wantedPrimary != primary->numBlocks(kind)) primary->resizeBlocks(kind, wantedPrimary);
    if (secondary && wantedSecondary != secondary->numBlocks(kind)) secondary->resizeBlocks(kind, wantedSecondary);

    for (const BlockWrite &write : writes) {
        if (!write.tileset)
            continue;
        const QList<Metatile*> &blocks = write.tileset->blocks(kind);
        if (write.index < 0 || write.index >= blocks.size())
            continue;
        Metatile *target = blocks.at(write.index);
        if (!target)
            continue;
        *target = write.value;
        // A label is only ever ADDED: an id that already carries one keeps it (labels are #defines other code may use, and a
        // stale one can name an id the tileset only grew into now).
        if (!write.label.isEmpty() && write.tileset->blockLabels(kind).value(write.id).isEmpty())
            write.tileset->blockLabels(kind).insert(write.id, write.label);
    }
}

} // namespace MapTransfer
