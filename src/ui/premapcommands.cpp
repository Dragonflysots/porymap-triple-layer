#include "premapcommands.h"
#include "editor.h"
#include "maplayout.h"
#include "project.h"
#include "log.h"

// Applies (or reverts) the fields of a stroke to the pre-map that is showing (only that layout's item is on screen; a command of another
// layout is only reachable while that layout is showing, because the stacks are per layout).
static void applyEdits(Editor *editor, const PreMapStroke &stroke, bool undo) {
    if (undo) {
        for (int i = stroke.edits.size() - 1; i >= 0; i--) {
            const PreMapCellEdit &edit = stroke.edits.at(i);
            editor->preMap.set(edit.layer, edit.x, edit.y, edit.before);
        }
    } else {
        for (const PreMapCellEdit &edit : stroke.edits)
            editor->preMap.set(edit.layer, edit.x, edit.y, edit.after);
    }
}

PreMapStrokeCommand::PreMapStrokeCommand(Editor *editor, const QString &layoutId, const PreMapStroke &stroke)
    : QUndoCommand(stroke.text), editor(editor), layoutId(layoutId), stroke(stroke) {}

void PreMapStrokeCommand::undo() {
    applyEdits(this->editor, this->stroke, true);
    this->editor->afterPreMapChanged(false);
}

void PreMapStrokeCommand::redo() {
    if (this->firstRedo) {   // (the item already applied the stroke; the owner saves)
        this->firstRedo = false;
        return;
    }
    applyEdits(this->editor, this->stroke, false);
    this->editor->afterPreMapChanged(false);
}

PreMapAlphaCommand::PreMapAlphaCommand(Editor *editor, const QString &layoutId, int layer, bool before, bool after)
    : QUndoCommand(QStringLiteral("Alpha Channel")), editor(editor), layoutId(layoutId), layer(layer), before(before), after(after) {}

void PreMapAlphaCommand::apply(bool value) {
    this->editor->preMap.alphaFlag(this->layer) = value;
    this->editor->afterPreMapChanged(true);
}

PreMapShiftCommand::PreMapShiftCommand(Editor *editor, const QString &layoutId, QPoint delta)
    : QUndoCommand(QStringLiteral("Shift Porytiles")), editor(editor), layoutId(layoutId), delta(delta) {}

void PreMapShiftCommand::redo() {
    if (!this->haveBefore) {
        this->before = this->editor->preMap.snapshot();
        this->haveBefore = true;
    }
    this->editor->preMap.shiftAll(this->delta.x(), this->delta.y());
    this->editor->afterPreMapChanged(false);
}

void PreMapShiftCommand::undo() {
    this->editor->preMap.restore(this->before);
    this->editor->afterPreMapChanged(false);
}

PreMapClearCommand::PreMapClearCommand(Editor *editor, const QString &layoutId, int parts)
    : QUndoCommand(QString("Clear %1").arg(describe(parts))), editor(editor), layoutId(layoutId), parts(parts) {}

QString PreMapClearCommand::describe(int parts) {
    QStringList layers;
    if (parts & Bottom) layers << QStringLiteral("Bottom");
    if (parts & Middle) layers << QStringLiteral("Middle");
    if (parts & Top) layers << QStringLiteral("Top");
    QString text;
    if (layers.size() == 3)
        text = QStringLiteral("all layers");
    else if (layers.size() == 2)
        text = QString("the %1 and %2 layers").arg(layers.at(0), layers.at(1));
    else if (layers.size() == 1)
        text = QString("the %1 layer").arg(layers.at(0));
    if (parts & Behaviors)
        text += (text.isEmpty() ? QString() : QStringLiteral(" and ")) + QStringLiteral("the placed behaviors");
    return text;
}

void PreMapClearCommand::redo() {
    if (!this->haveBefore) {
        this->before = this->editor->preMap.snapshot();
        this->haveBefore = true;
    }
    PreMap &map = this->editor->preMap;
    for (int layer = 0; layer < PreMap::kLayers; layer++) {
        if (!(this->parts & (1 << layer)))
            continue;
        for (int y = 0; y < map.height(); y++)
            for (int x = 0; x < map.width(); x++)
                map.set(layer, x, y, 0);
    }
    if (this->parts & Behaviors)
        for (int y = 0; y < map.height(); y++)
            for (int x = 0; x < map.width(); x++)
                map.setBehavior(x, y, PreMap::kAutoBehavior);
    finish();
}

void PreMapClearCommand::undo() {
    this->editor->preMap.restore(this->before);
    finish();
}

void PreMapClearCommand::finish() {
    if (this->parts & Behaviors)   // (its steps describe the behavior grid this command replaces -- in both directions)
        this->editor->preMapBehaviorStackFor(this->layoutId)->clear();
    this->editor->afterPreMapChanged(false);   // (redraws and auto-saves)
}

CleanFinalmapCommand::CleanFinalmapCommand(Editor *editor, Layout *layout)
    : QUndoCommand(QStringLiteral("Clean the Map")), editor(editor), layout(layout) {
    this->beforeBlocks = layout ? layout->blockdata : Blockdata();
}

void CleanFinalmapCommand::redo() {
    QUndoCommand::redo();
    if (!this->layout)
        return;
    Blockdata cleaned = this->beforeBlocks;
    for (int i = 0; i < cleaned.size(); i++)
        cleaned[i] = Block(0, 0, 0);   // metatile 0 (the empty one), collision 0, elevation 0
    apply(cleaned);
}

void CleanFinalmapCommand::undo() {
    if (this->layout)
        apply(this->beforeBlocks);
}

void CleanFinalmapCommand::apply(const Blockdata &blocks) {
    this->layout->setBlockdata(blocks, true);
    this->layout->clearBorderCache();
    if (this->layout->layoutItem)
        this->layout->layoutItem->draw(true);
    if (this->layout->collisionItem)
        this->layout->collisionItem->draw(true);
}

// ---- the two transfers between the Porymap view and the Finalmap ------------------------------------------------------------------------

QList<BlockSlotBackup> BlockSlotBackup::take(const QList<MapTransfer::BlockWrite> &writes, BlockKind kind) {
    QList<BlockSlotBackup> backups;
    for (const MapTransfer::BlockWrite &write : writes) {
        BlockSlotBackup backup;
        backup.tileset = write.tileset;
        backup.kind = kind;
        backup.index = write.index;
        backup.id = write.id;
        backup.wasAppended = write.appended;
        if (write.tileset) {
            const Metatile *block = write.tileset->blocks(kind).value(write.index, nullptr);
            backup.before = block ? *block : Metatile(Tileset::tilesPerBlock(kind));
            backup.labelBefore = write.tileset->blockLabels(kind).value(write.id);
        }
        if (backup.wasAppended) {   // it did not exist yet: "before" is a blank block with no label
            backup.before = Metatile(Tileset::tilesPerBlock(kind));
            backup.labelBefore.clear();
        }
        backups.append(backup);
    }
    return backups;
}

void BlockSlotBackup::restore(const QList<BlockSlotBackup> &backups) {
    for (const BlockSlotBackup &backup : backups) {
        if (!backup.tileset)
            continue;
        Metatile *block = backup.tileset->blocks(backup.kind).value(backup.index, nullptr);
        if (!block)
            continue;   // (the tileset got shorter in the meantime: nothing of ours is there any more)
        *block = backup.before;
        if (backup.labelBefore.isEmpty())
            backup.tileset->blockLabels(backup.kind).remove(backup.id);
        else
            backup.tileset->blockLabels(backup.kind).insert(backup.id, backup.labelBefore);
    }
}

// A transfer (or its undo) changed tileset data: the project remembers it, so the next project save writes it, and the views
// catch up. Nothing is written here -- an undo must never leave data on disk that the map in memory does not refer to.
// The tilesets come from the slots that were actually written, not from the layout: the user may have picked another
// tileset for the map in between, and then the layout's pair is not the pair this command touched.
static void afterTilesetsChanged(Editor *editor, const QList<BlockSlotBackup> &backups) {
    if (!editor || !editor->project)
        return;
    editor->project->tilesetTransferGeneration++;
    QSet<const Tileset*> seen;
    for (const BlockSlotBackup &backup : backups) {
        if (backup.tileset && !seen.contains(backup.tileset)) {
            seen.insert(backup.tileset);
            editor->project->markTilesetChangedByTransfer(backup.tileset);
        }
    }
    emit editor->tilesetsTransferred();
}

PushToFinalmapCommand::PushToFinalmapCommand(Editor *editor, Layout *layout, const MapTransfer::PushPlan &plan)
    : QUndoCommand(QStringLiteral("Write to Finalmap")), editor(editor), layout(layout), plan(plan) {
    this->beforeBlocks = layout ? layout->blockdata : Blockdata();
    this->backups = BlockSlotBackup::take(plan.writes, BlockKind::Metatile);
}

void PushToFinalmapCommand::redo() {
    QUndoCommand::redo();
    if (!this->layout)
        return;
    MapTransfer::applyBlockWrites(this->plan.writes, this->layout->tileset_primary, this->layout->tileset_secondary, BlockKind::Metatile);
    this->layout->setBlockdata(this->plan.blockdata, true);
    finish();
}

void PushToFinalmapCommand::undo() {
    if (!this->layout)
        return;
    BlockSlotBackup::restore(this->backups);
    this->layout->setBlockdata(this->beforeBlocks, true);
    finish();
}

void PushToFinalmapCommand::finish() {
    this->layout->clearBorderCache();
    if (this->layout->layoutItem)
        this->layout->layoutItem->draw(true);
    if (this->layout->collisionItem)
        this->layout->collisionItem->draw(true);
    afterTilesetsChanged(this->editor, this->backups);
}

PullToPorymapCommand::PullToPorymapCommand(Editor *editor, Layout *layout, const MapTransfer::PullPlan &plan)
    : QUndoCommand(QStringLiteral("Pull to Porymap")), editor(editor), layout(layout), plan(plan) {
    this->layoutId = layout ? layout->id : QString();
    if (editor)
        this->beforeGrid = editor->preMap.snapshot();
    this->backups = BlockSlotBackup::take(plan.writes, BlockKind::Porytile);
}

void PullToPorymapCommand::redo() {
    QUndoCommand::redo();
    if (!this->layout || !this->editor)
        return;
    MapTransfer::applyBlockWrites(this->plan.writes, this->layout->tileset_primary, this->layout->tileset_secondary, BlockKind::Porytile);
    const QSize size(this->layout->getWidth(), this->layout->getHeight());
    if (this->editor->preMap.size() != size)
        this->editor->preMap.reset(size);
    for (int y = 0; y < size.height(); y++) {
        for (int x = 0; x < size.width(); x++) {
            const int index = y * size.width() + x;
            for (int layer = 0; layer < PreMap::kLayers; layer++)
                this->editor->preMap.set(layer, x, y, this->plan.layers[layer].value(index, 0));
            this->editor->preMap.setBehavior(x, y, this->plan.behaviors.value(index, PreMap::kAutoBehavior));
        }
    }
    finish();
}

void PullToPorymapCommand::undo() {
    if (!this->layout || !this->editor)
        return;
    BlockSlotBackup::restore(this->backups);
    this->editor->preMap.restore(this->beforeGrid);
    finish();
}

void PullToPorymapCommand::finish() {
    // Behavior steps of this layout describe the behavior grid that this command replaces -- in both directions.
    this->editor->preMapBehaviorStackFor(this->layoutId)->clear();
    this->editor->savePreMap(this->layoutId);          // the Porymap data is always auto-saved
    if (this->editor->preMapItem)
        this->editor->preMapItem->onTilesetsChanged(); // (the porytile pictures changed too)
    afterTilesetsChanged(this->editor, this->backups);
}

PreMapResizeCommand::PreMapResizeCommand(Editor *editor, const QString &layoutId, QSize oldSize, QMargins margins, QUndoCommand *parent)
    : QUndoCommand(QStringLiteral("Resize Porytiles"), parent), editor(editor), layoutId(layoutId), oldSize(oldSize), margins(margins) {}

void PreMapResizeCommand::redo() { run(true); }
void PreMapResizeCommand::undo() { run(false); }

void PreMapResizeCommand::run(bool forward) {
    const QSize newSize(this->oldSize.width() + this->margins.left() + this->margins.right(), this->oldSize.height() + this->margins.top() + this->margins.bottom());
    const bool onScreen = this->editor->preMapLoadedLayoutId == this->layoutId;
    PreMap file;
    PreMap &target = onScreen ? this->editor->preMap : file;
    if (!onScreen)
        file.load(this->layoutId, forward ? this->oldSize : newSize);
    if (forward) {
        if (!this->haveBefore) {
            this->before = target.snapshot();
            this->haveBefore = true;
        }
        target.resize(newSize, this->margins);
    } else if (this->haveBefore) {
        target.restore(this->before);
    }
    if (onScreen) {
        this->editor->afterPreMapChanged(false);   // (redraws and auto-saves)
        if (this->editor->preMapItem)
            this->editor->preMapItem->draw();
    } else {
        target.save(this->layoutId);
    }
}
