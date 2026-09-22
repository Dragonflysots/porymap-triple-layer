#ifndef TILESETEDITOR_H
#define TILESETEDITOR_H

#include <QMainWindow>
#include <QPointer>
#include <QKeyEvent>
#include <QListWidgetItem>
#include <QToolButton>
#include <QCheckBox>
#include <QPushButton>
#include <QElapsedTimer>
#include <functional>
#include <QComboBox>
#include "project.h"
#include "history.h"
#include "paletteeditor.h"
#include "tileseteditormetatileselector.h"
#include "tileseteditortileselector.h"
#include "sheetbehaviorpanel.h"
#include "sheetcontrolslegend.h"
#include "metatileimageexporter.h"

class NoScrollComboBox;
class Layout;

namespace Ui {
class TilesetEditor;
}

// One changed metatile of a history step: what it was and what it became (label included).
struct MetatileEdit {
    uint16_t id = 0;
    Metatile prev;
    Metatile next;
    QString prevLabel;
    QString nextLabel;
};

// One undo step. It holds one or SEVERAL metatile edits (a brush stroke, a behavior assigned to many cells, an import) that are
// undone / redone together, or it is a swap of two metatiles. Every step belongs to ONE of the two histories of the Tileset Editor: the Paint page's
// (strokes, Delete, Clear Field, Cut / Paste, Swap, imports) or the Behavior page's (behaviors, labels, Clear Behavior); Undo / Redo works on the
// history of the page that is showing.
class MetatileHistoryItem {
public:
    enum class Stack { Paint, Behavior };
    Stack stack = Stack::Paint;
    BlockKind kind = BlockKind::Metatile;   // which kind of block the step changed (set by commit(): the kind the editor showed)
    MetatileHistoryItem() {};
    // A single metatile. Takes ownership of both pointers (they are copied into the edit and deleted).
    MetatileHistoryItem(uint16_t metatileId, Metatile *prevMetatile, Metatile *newMetatile, QString prevLabel, QString newLabel) {
        this->metatileId = metatileId;
        MetatileEdit edit;
        edit.id = metatileId;
        if (prevMetatile) edit.prev = *prevMetatile;
        if (newMetatile) edit.next = *newMetatile;
        edit.prevLabel = prevLabel;
        edit.nextLabel = newLabel;
        this->edits.append(edit);
        delete prevMetatile;
        delete newMetatile;
    }
    MetatileHistoryItem(uint16_t metatileIdA, uint16_t metatileIdB) {
        this->metatileId = metatileIdA;
        this->swapMetatileId = metatileIdB;
        this->isSwap = true;
    }

    QList<MetatileEdit> edits;
    uint16_t metatileId = 0;

    uint16_t swapMetatileId = 0;
    bool isSwap = false;
};

class TilesetEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit TilesetEditor(Project *project, Layout *layout, QWidget *parent = nullptr);
    ~TilesetEditor();
    void update(Layout *layout, QString primaryTilsetLabel, QString secondaryTilesetLabel);
    void updateLayout(Layout *layout);
    void updateTilesets(QString primaryTilsetLabel, QString secondaryTilesetLabel);
    bool selectMetatile(uint16_t metatileId);
    uint16_t getSelectedMetatileId();       // the selected METATILE (for the main window and scripts)
    uint16_t selectedBlockId() const;       // the selected block of the kind that is showing
    void setMetatileLabel(QString label);
    void queueMetatileReload(uint16_t metatileId);

    QObjectList shortcutableObjects() const;

    void setPaletteId(int paletteId);
    int paletteId() const;

    // CUSTOM ENGINE: the editor works on COPIES of the project's tilesets. Write to Finalmap / Pull to Porymap change the
    // project's tilesets, so they ask whether this editor holds unsaved work (they refuse then) and otherwise tell it to
    // take fresh copies, instead of letting a later Save here write the old data back over them.
    bool isDirty() const;
    void reloadTilesetsFromProject();
    int tilesetGeneration = 0;   // the project's tilesetTransferGeneration when these copies were taken

public slots:
    void applyUserShortcuts();
    void onSelectedMetatileChanged(uint16_t);

private slots:
    void onWindowActivated();
    void onHoveredMetatileCleared();
    void onHoveredTileCleared();
    void onPaletteEditorChangedPaletteColor();

    void on_actionChange_Metatiles_Count_triggered();

    void on_actionChange_Palettes_triggered();

    void on_actionShow_Unused_toggled(bool checked);
    void on_actionShow_Counts_toggled(bool checked);
    void on_actionShow_UnusedTiles_toggled(bool checked);
    void on_actionMetatile_Grid_triggered(bool checked);
    void on_actionShow_Tileset_Divider_triggered(bool checked);

    void on_actionUndo_triggered();
    void on_actionRedo_triggered();

    void on_copyButton_MetatileLabel_clicked();

    void on_actionCut_triggered();
    void on_actionCopy_triggered();
    void on_actionPaste_triggered();
    void on_horizontalSlider_MetatilesZoom_valueChanged(int value);
    void on_horizontalSlider_TilesZoom_valueChanged(int value);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent*) override;

private:
    void initMetatileSelector();
    void initLayerBar();
    void initBehaviorPage();
    void redrawBehaviorView();
    void onBehaviorSelectionChanged();
    void onMetatileEditorTabChanged(int index);
    void onLabelEditingFinished();
    void assignBehavior(int value);
    void assignNumberedLabels(const QString &baseName);
    void drawOnSheets(const QSet<uint16_t> &metatileIds);
    bool onBehaviorPage() const;
    void initSheetPainting();
    void updateBrushGhost();
    QString paintBlockReason() const;
    void beginStroke();
    void touchMetatile(uint16_t metatileId);
    bool endStroke();
    bool strokeActive() const { return this->strokeOpen; }
    void paintBrushAt(const QPoint &originTile);
    bool writeSheetTile(uint16_t metatileId, int tileIndex, const Tile &value, QSet<uint16_t> *changed);
    void finishSheetEdit(const QSet<uint16_t> &changed);
    void deleteSelectedTiles();
    void clearFields();
    bool deselectAll();
    bool typingInTextField() const;
    void initFieldPanel();
    void clearBehaviors();
    void refreshBehaviorFilterChoices();
    void onBehaviorFilterChanged();
    void onBehaviorFilterEdited();
    void resetBehaviorFilter();
    void pickTileFromSheet(const QPoint &tile);
    void pickRegionFromSheet(const QRect &tiles);
    void showSlotStatus(uint16_t metatileId, int subIndex);
    bool tileIsBlank(const Tile &tile) const;
    bool topLayerIsBlank(const Metatile &metatile) const;
    Metatile blankMetatile() const;
    void initTileSelector();
    void initSelectedTileItem();
    void initShortcuts();
    void initExtraShortcuts();
    void restoreWindowState();
    void initMetatileHistory();
    void setTilesets(QString primaryTilesetLabel, QString secondaryTilesetLabel);
    void reset();
    void drawSelectedTiles();
    void redrawTileSelector();
    void redrawMetatileSelector();
    void importTilesetTiles(Tileset*);
    void importAdvanceMapMetatiles(Tileset*);
    void exportTilesImage(Tileset*);
    void exportPorytilesLayerImages(Tileset*);
    void exportMetatilesImage();
    void refresh();
    void commitMetatileLabel();
    void countMetatileUsage();
    void countTileUsage();
    void copyMetatile(bool cut);
    void pasteMetatile(const Metatile &toPaste, QString label);
    bool replaceMetatile(uint16_t metatileId, const Metatile &src, QString label, bool select = true, bool redraw = true);
    void applyHistoryItem(const MetatileHistoryItem *item, bool undo);
    bool applyEditDelta(const MetatileEdit &edit, bool undo);
    bool stepFits(const MetatileHistoryItem *item, bool undo, uint16_t *conflictingId = nullptr) const;
    bool labelBelongsToAnotherMetatile(const QString &label, uint16_t ownId) const;
    History<MetatileHistoryItem*> &historyFor(BlockKind kind, MetatileHistoryItem::Stack stack) {
        if (kind == BlockKind::Porytile)
            return stack == MetatileHistoryItem::Stack::Behavior ? this->porytileBehaviorHistory : this->porytilePaintHistory;
        return stack == MetatileHistoryItem::Stack::Behavior ? this->behaviorHistory : this->paintHistory;
    }
    History<MetatileHistoryItem*> &currentHistory() { return historyFor(kind(), onBehaviorPage() ? MetatileHistoryItem::Stack::Behavior : MetatileHistoryItem::Stack::Paint); }
    // ---- CUSTOM ENGINE: the two kinds of blocks. The outer tabs are Porytiles | Generated Metatiles; the inner Paint | Behavior pages and all the
    // editing code work on the kind of the outer tab that is showing (kind()). Each kind has its own histories, clipboard and remembered selection.
    BlockKind kind() const;
    QString blockName() const { return kind() == BlockKind::Porytile ? QStringLiteral("Porytile") : QStringLiteral("Metatile"); }
    Metatile *block(uint16_t id) const { return Tileset::getBlock(kind(), id, this->primaryTileset, this->secondaryTileset); }
    QString ownedLabel(uint16_t id) const { return Tileset::getOwnedBlockLabel(kind(), id, this->primaryTileset, this->secondaryTileset); }
    bool setLabel(uint16_t id, const QString &label) { return Tileset::setBlockLabel(kind(), id, label, this->primaryTileset, this->secondaryTileset); }
    bool blockIsValid(uint16_t id) const { return Tileset::blockIsValid(kind(), id, this->primaryTileset, this->secondaryTileset); }
    Metatile *&clipboardBlock() { return kind() == BlockKind::Porytile ? this->copiedPorytile : this->copiedMetatile; }
    QString &clipboardLabel() { return kind() == BlockKind::Porytile ? this->copiedPorytileLabel : this->copiedMetatileLabel; }
    void initKindTabs();
    void onKindTabChanged(int index);
    // Entry 0 (metatile 0 / porytile 0) is the erase entry of the maps: always empty, never edited. False (with a message) when `ids` contains it.
    bool allowEntryZero(const QSet<uint16_t> &ids);
    void refuseEntryZero();
    void ensureMetatileVisible(uint16_t metatileId);
    void commitMetatileChange(Metatile * prevMetatile);
    void commitMetatileAndLabelChange(Metatile * prevMetatile, QString prevLabel, MetatileHistoryItem::Stack stack = MetatileHistoryItem::Stack::Paint);
    void refreshMetatileAttributes();
    void commit(MetatileHistoryItem *item);
    void updateEditHistoryActions();
    void refreshTileFlips();
    void refreshPaletteId();
    void commitMetatileSwap(uint16_t metatileIdA, uint16_t metatileIdB);
    bool swapMetatiles(uint16_t metatileIdA, uint16_t metatileIdB);
    void recordLayoutSwap(uint16_t metatileIdA, uint16_t metatileIdB);
    void applyMetatileSwapToLayouts(uint16_t metatileIdA, uint16_t metatileIdB);
    void applyMetatileSwapsToLayouts();
    void showTileStatus(uint16_t tileId);
    void updateMetatileStatus();
    void showMetatileStatus(uint16_t metatileId);

    Ui::TilesetEditor *ui;
    History<MetatileHistoryItem*> paintHistory;      // what was done on the Paint page (metatiles)
    History<MetatileHistoryItem*> behaviorHistory;   // what was done on the Behavior page (behaviors, labels of metatiles)
    History<MetatileHistoryItem*> porytilePaintHistory;      // the same two for the porytiles
    History<MetatileHistoryItem*> porytileBehaviorHistory;
    uint16_t rememberedSelection[2] = { 0, 0 };   // the selected block of the kind that is NOT showing ([0] metatiles, [1] porytiles)
    TilesetEditorMetatileSelector *metatileSelector = nullptr;
    TilesetEditorTileSelector *tileSelector = nullptr;
    // CUSTOM ENGINE: the layer bar above the metatile sheet: which layer is painted (one of them), and an eye per layer (any number of them).
    QToolButton *layerSelectButtons[3] = { nullptr, nullptr, nullptr };
    QToolButton *layerEyeButtons[3] = { nullptr, nullptr, nullptr };
    // The Behavior page: a second view of the same metatiles (fields, not tiles) and the panel with the behavior chooser and the label.
    TilesetEditorMetatileSelector *behaviorSelector = nullptr;
    QGraphicsView *behaviorView = nullptr;
    QScrollArea *behaviorScroll = nullptr;
    QSlider *behaviorZoomSlider = nullptr;
    QSlider *behaviorOpacitySlider = nullptr;   // CUSTOM ENGINE: how strongly the behavior numbers cover the sheets (Behavior page; the Paint page's Display Behavior follows it)
    QLabel *behaviorOpacityValue = nullptr;
    void onBehaviorOpacityChanged(int percent);
    SheetBehaviorPanel *behaviorPanel = nullptr;
    bool labelEditedSinceSelection = false;   // (a label field that was only focused and left must not number a whole selection)
    // A brush stroke on the sheet is ONE undo step: the metatiles it touches are remembered (as they were) until the mouse is released.
    bool strokeOpen = false;
    QMap<uint16_t, MetatileEdit> strokeEdits;
    QPushButton *clearFieldButton = nullptr;   // the Paint page's "Clear Field" (the Behavior page has its own "Clear Behavior" in the panel)
    QComboBox *behaviorFilterCombo = nullptr;    // the view filter of the Behavior page: which behavior to show (editable: type a name or a hex ID)
    QComboBox *labelFilterCombo = nullptr;       // ... and which label state (any / with a label / without a label)
    QToolButton *filterResetButton = nullptr;
    QElapsedTimer lastBehaviorApplied;   // (a double click on a behavior must not end with "already have behavior" in the status bar)
    bool brushArmed = false;   // nothing is painted until the user has picked something (a click must not erase by accident)
    void setActiveLayer(int layer);
    QPointer<PaletteEditor> paletteEditor = nullptr;
    Project *project = nullptr;
    Layout *layout = nullptr;
    Metatile *metatile = nullptr;
    Metatile *copiedMetatile = nullptr;
    QString copiedMetatileLabel;
    Metatile *copiedPorytile = nullptr;
    QString copiedPorytileLabel;
    // "Unsaved" = something without a history step changed (palette colours, tile image import, metatile count) OR the history is
    // not at the state that was saved. So Paint -> Save -> Undo is unsaved again, and Undo back to the saved state is clean.
    bool extraDirty = false;
    // The unsaved marker looks at the DATA, not at the positions in the two histories (an Undo on one page and a step on the other can leave both
    // histories at their saved marks with different data): what the metatiles and labels hold now is compared with what they held at the last Save.
    QByteArray savedFingerprint;
    QByteArray contentFingerprint() const;
    bool tilesetDirty() const { return this->extraDirty || contentFingerprint() != this->savedFingerprint; }
    // CUSTOM ENGINE: Save (Ctrl+S / the Save button) applies the tileset data; the window title and the Save button show
    // whether anything is unsaved, and closing an unsaved editor warns.
    QString baseWindowTitle;
    void setTilesetDirty(bool dirty);
    void updateWindowTitle();
    QString unsavedSummary() const;
    bool saveTilesetData();
    Tileset *primaryTileset = nullptr;
    Tileset *secondaryTileset = nullptr;
    bool lockSelection = false;
    QSet<uint16_t> metatileReloadQueue;
    MetatileImageExporter::Settings *metatileImageExportSettings = nullptr;
    QList<QPair<uint16_t,uint16_t>> metatileIdSwaps;

    bool save();

signals:
    void tilesetsSaved(QString, QString);
    void dividerShownChanged(bool shown);   // View > Show Tileset Divider was switched (the palettes of the main window follow it)
};

#endif // TILESETEDITOR_H
