#ifndef PREMAPCOMMANDS_H
#define PREMAPCOMMANDS_H

// CUSTOM ENGINE: undo/redo for edits made in the Porymap view. Every command belongs to ONE layout (by id) and lives on that layout's own
// QUndoStack (Editor::preMapStackFor), which the Undo/Redo actions use while the Porymap view is showing that layout. Every do/undo/redo is
// followed by an immediate save of layers.json: the Porymap data is always "auto-saved", there is no separate Save for it.

#include "premappixmapitem.h"
#include "core/maptransfer.h"
#include "blockdata.h"
#include <QUndoCommand>

class Editor;
class Layout;

// CUSTOM ENGINE: what ONE slot of a tileset looked like before a transfer wrote it, so the transfer can be taken back slot by
// slot. A whole-tileset snapshot would be wrong here: the tilesets are shared, and a later edit (the Tileset Editor, a push of
// another map) must survive an undo of this one. A slot the transfer appended is emptied again instead of removed, because
// another map may already have taken the ids above it.
struct BlockSlotBackup {
    Tileset *tileset = nullptr;
    BlockKind kind = BlockKind::Metatile;
    int index = 0;
    uint16_t id = 0;
    bool wasAppended = false;   // the slot did not exist before this transfer
    Metatile before;            // its content before (blank when it was appended)
    QString labelBefore;        // its label before (empty = it had none)

    static QList<BlockSlotBackup> take(const QList<MapTransfer::BlockWrite> &writes, BlockKind kind);
    static void restore(const QList<BlockSlotBackup> &backups);
};

// One press..release of a tool: fields that changed. The item has ALREADY applied the stroke when the command is pushed, so the first
// redo() does nothing; later redo()/undo() re-apply / revert it.
class PreMapStrokeCommand : public QUndoCommand {
public:
    PreMapStrokeCommand(Editor *editor, const QString &layoutId, const PreMapStroke &stroke);
    void undo() override;
    void redo() override;

private:
    Editor *editor;
    QString layoutId;
    PreMapStroke stroke;
    bool firstRedo = true;
};

// The alpha checkbox of one layer.
class PreMapAlphaCommand : public QUndoCommand {
public:
    PreMapAlphaCommand(Editor *editor, const QString &layoutId, int layer, bool before, bool after);
    void undo() override { apply(this->before); }
    void redo() override { apply(this->after); }

private:
    void apply(bool value);
    Editor *editor;
    QString layoutId;
    int layer;
    bool before, after;
};

// Shift tool: all three layers move together by whole fields (nothing wraps). It does the work in redo() (the tool only asks for it) and keeps
// a snapshot of the grid from before, so undo puts back exactly what left the map.
class PreMapShiftCommand : public QUndoCommand {
public:
    PreMapShiftCommand(Editor *editor, const QString &layoutId, QPoint delta);
    void undo() override;
    void redo() override;

private:
    Editor *editor;
    QString layoutId;
    QPoint delta;
    PreMap::Snapshot before;
    bool haveBefore = false;
};

// CUSTOM ENGINE: "Clear Layers" (the eraser in the Porymap view): the chosen layers -- and, if asked, the placed behaviors -- of the WHOLE map go back to
// what an empty map holds: Porytile 0 (the erase entry) on every field, behavior Auto. A snapshot of the grid from before makes the undo exact.
// Clearing the behaviors clears that layout's behavior history (its steps describe a grid this command replaces), like Pull to Porymap does.
class PreMapClearCommand : public QUndoCommand {
public:
    enum Part { Bottom = 1, Middle = 2, Top = 4, Behaviors = 8, Everything = 15 };
    PreMapClearCommand(Editor *editor, const QString &layoutId, int parts);
    void undo() override;
    void redo() override;
    static QString describe(int parts);   // "Bottom and Top layers", "all layers and the placed behaviors", ...

private:
    void finish();
    Editor *editor;
    QString layoutId;
    int parts;
    PreMap::Snapshot before;
    bool haveBefore = false;
};

// CUSTOM ENGINE: "Clean the Map" (the eraser on the Finalmap tab): every field becomes metatile 0 (the empty one) with elevation 0 and collision 0.
// The tilesets are not touched. One step on the layout's history.
class CleanFinalmapCommand : public QUndoCommand {
public:
    CleanFinalmapCommand(Editor *editor, Layout *layout);
    void undo() override;
    void redo() override;

private:
    void apply(const Blockdata &blocks);
    Editor *editor;
    Layout *layout;
    Blockdata beforeBlocks;
};

// Change Dimensions: the grid follows the map (a left / top margin moves the content, what ends up outside is dropped, and put back when the
// resize is undone). Lives as a CHILD of the layout's ResizeLayout command on the layout's undo stack, so one Undo in the Finalmap view reverts
// the resize AND the Porytiles. It works on the current pre-map, or on the file of the layout when that is not the one on screen.
class PreMapResizeCommand : public QUndoCommand {
public:
    PreMapResizeCommand(Editor *editor, const QString &layoutId, QSize oldSize, QMargins margins, QUndoCommand *parent);
    void undo() override;
    void redo() override;

private:
    void run(bool forward);
    Editor *editor;
    QString layoutId;
    QSize oldSize;
    QMargins margins;
    PreMap::Snapshot before;
    bool haveBefore = false;
};

// CUSTOM ENGINE: "Write to Finalmap" -- the porytile grid becomes real metatiles + map.bin. One step takes back BOTH sides:
// the map's blockdata and exactly the tileset slots the generator wrote (their old content and labels). It lives on the
// LAYOUT's history, so Ctrl+Z on the Finalmap tab undoes it. Nothing is written to disk here: the changed tilesets are marked
// on the project and written by the next project save (Project::saveChangedTilesets), together with the map itself.
class PushToFinalmapCommand : public QUndoCommand {
public:
    PushToFinalmapCommand(Editor *editor, Layout *layout, const MapTransfer::PushPlan &plan);
    void undo() override;
    void redo() override;

private:
    void finish();
    Editor *editor;
    Layout *layout;
    MapTransfer::PushPlan plan;
    Blockdata beforeBlocks;
    QList<BlockSlotBackup> backups;
};

// CUSTOM ENGINE: "Pull to Porymap" -- the real map becomes porytiles again. One step takes back the pre-map (all three layers
// AND the placed behaviors) and exactly the porytile slots the pull wrote. It lives on the layout's PORYTILE history (the
// Porymap view's own), and every do/undo clears that layout's behavior history: its steps describe a behavior grid that this
// command replaces (in both directions). The pre-map is auto-saved as always; the porytiles follow the project save.
class PullToPorymapCommand : public QUndoCommand {
public:
    PullToPorymapCommand(Editor *editor, Layout *layout, const MapTransfer::PullPlan &plan);
    void undo() override;
    void redo() override;

private:
    void finish();
    Editor *editor;
    Layout *layout;
    QString layoutId;
    MapTransfer::PullPlan plan;
    PreMap::Snapshot beforeGrid;
    QList<BlockSlotBackup> backups;
};

#endif // PREMAPCOMMANDS_H
