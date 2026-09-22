#include "tileseteditor.h"
#include "ui_tileseteditor.h"
#include "log.h"
#include "imageproviders.h"
#include "advancemapparser.h"
#include "paletteutil.h"
#include "imageexport.h"
#include "config.h"
#include "shortcut.h"
#include "filedialog.h"
#include "validator.h"
#include "eventfilters.h"
#include "utility.h"
#include "message.h"
#include <QDialogButtonBox>
#include <QToolTip>
#include <QScrollArea>
#include <QStyle>
#include <QCloseEvent>
#include <QTimer>
#include <QSplitter>
#include <QImageReader>
#include <QButtonGroup>
#include <QSlider>
#include <QScrollArea>
#include <QScrollBar>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractSpinBox>
#include <QCompleter>
#include <QCryptographicHash>

namespace {
// The one-line text fields (label, palette number, filter) take Ctrl+Z / Ctrl+Y for themselves, so the window's Undo never came: a behavior or a
// label that was just set could not be undone by keyboard. A field only keeps the keys while it has something of its own to undo or redo.
class UndoShortcutFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::ShortcutOverride)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        const bool undo = key->matches(QKeySequence::Undo), redo = key->matches(QKeySequence::Redo);
        if (!undo && !redo)
            return false;
        QLineEdit *edit = qobject_cast<QLineEdit *>(object);
        if (!edit) {
            if (auto *combo = qobject_cast<QComboBox *>(object))
                edit = combo->lineEdit();   // (an editable combo box takes the focus itself and hands the keys to its line edit)
        }
        if (edit && ((undo && edit->isUndoAvailable()) || (redo && edit->isRedoAvailable())))
            return false;
        event->ignore();
        return true;
    }
};

// The legends of the two sheets: pictograms + text for every mouse button and key that does something. The key that Qt calls Control is Cmd on a Mac
// and Ctrl elsewhere, so they name the right one. (The same controls are written down in docs/tileset_editor_bedienung.md.)
SheetControlsLegend *makePaintLegend(QWidget *parent) {
    const QString cmd = SheetBehaviorPanel::cmdKeyName();
    auto *legend = new SheetControlsLegend(1, parent);
    legend->setObjectName(QStringLiteral("legend_PaintControls"));
    legend->addEntry({ "L" }, "Left-click / drag: paint",
                     "Left-click or drag: paint the brush on the active layer (8x8 tiles). The tile under the mouse changes at once.");
    legend->addEntry({ "R" }, "Right-click: pick a tile, right-drag: an area",
                     "Right-click: the tile under the mouse (active layer) becomes the brush. Right-drag: a rectangle of tiles becomes the brush. It never erases.");
    legend->addEntry({ cmd, "+", "L", "/", "R" }, "Left / Right-click or drag: select",
                     QString("%1 + Left-click or %1 + Right-click (click or drag): select tiles, for Delete. Several separate areas are possible; %1 + click on a selected tile takes it out again.").arg(cmd));
    legend->addEntry({ cmd, "+", "Shift", "+", "L", "/", "R" }, "Left / Right-click or drag: deselect",
                     QString("%1 + Shift + Left-click or Right-click (click or drag): takes just the tiles you touch out of the selection. Esc, in contrast, deselects everything.").arg(cmd));
    legend->addEntry({ "Esc" }, "Deselect everything",
                     "Esc: removes the whole selection at once (not while you type in a text field).");
    legend->addEntry({ "Del" }, "Delete / Backspace: clear selected tiles",
                     "Delete or Backspace: the selected tiles of the active layer become tile 0, palette 0.");
    legend->addNote(QString("Every click changes the field at once. Undo: %1+Z, save: %1+S.").arg(cmd),
                    QString("Every click changes the field at once (in the editor; the files on disk change with %1+S). %1+Z undoes it. The Paint page has its own history: %1+Z here "
                            "only takes back what was done on this page (painting, Delete, Clear Field, Cut / Paste, Swap), never a behavior or a label from the Behavior page.").arg(cmd));
    return legend;
}

SheetControlsLegend *makeBehaviorLegend(QWidget *parent) {
    const QString cmd = SheetBehaviorPanel::cmdKeyName();
    auto *legend = new SheetControlsLegend(2, parent);
    legend->setObjectName(QStringLiteral("legend_BehaviorControls"));
    legend->addEntry({ "L" }, "Left-click / drag: select fields",
                     "Left-click selects that field, a left drag selects a rectangle. Both replace the selection.");
    legend->addEntry({ "R" }, "Right-click / right-drag: add fields",
                     "Right-click or right-drag adds fields to the selection. Without Shift the right button never takes one out.");
    legend->addEntry({ cmd, "+", "L", "/", "R" }, "Left / Right-click or drag: add fields",
                     QString("%1 + Left-click or %1 + Right-click (click or drag): add fields. %1 + Left-click on a selected field takes it out again (a Right-click never does).").arg(cmd));
    legend->addEntry({ cmd, "+", "Shift", "+", "L", "/", "R" }, "Left / Right-click or drag: deselect",
                     QString("%1 + Shift + Left-click or Right-click (click or drag): takes just the fields you touch out of the selection. Esc, in contrast, deselects everything.").arg(cmd));
    legend->addEntry({ "Esc" }, "Deselect everything",
                     "Esc: removes the whole selection at once (not while you type in a text field).");
    legend->addEntry({ cmd, "+", "Z" }, "Undo (+ Shift: redo), this page only",
                     QString("%1+Z undoes the last change made on THIS page (a behavior, a label, Clear Behavior), Shift+%1+Z (or %1+Y) redoes it; the same as Edit > Undo. "
                             "The Paint page has its own history, so this never touches what was painted. One step covers all fields that were changed together.").arg(cmd));
    legend->addNote(QString("Clicking a behavior changes all selected fields at once. Undo: %1+Z, save: %1+S.").arg(cmd),
                    QString("Clicking a behavior changes all selected fields at once (in the editor; the files on disk change with %1+S). %1+Z undoes it, only what was done on this page.").arg(cmd));
    return legend;
}

// Data of the label box of the view filter.
constexpr int kLabelsAny = 0, kLabelsWith = 1, kLabelsWithout = 2;

// Selects the whole text of an editable box when it gets the focus, so that typing a new search replaces the old text.
class SelectAllOnFocus : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::FocusIn) {
            QLineEdit *edit = qobject_cast<QLineEdit *>(object);
            if (!edit) {
                if (auto *combo = qobject_cast<QComboBox *>(object))
                    edit = combo->lineEdit();
            }
            if (edit)
                QTimer::singleShot(0, edit, &QLineEdit::selectAll);
        }
        return false;
    }
};
}

TilesetEditor::TilesetEditor(Project *project, Layout *layout, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::TilesetEditor),
    project(project),
    layout(layout)
{
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);
    this->baseWindowTitle = windowTitle();

    ui->spinBox_paletteSelector->setRange(0, Project::getNumPalettesTotal() - 1);

    auto validator = new IdentifierValidator(this);
    validator->setAllowEmpty(true);
    ui->lineEdit_MetatileLabel->setValidator(validator);

    ui->actionShow_Tileset_Divider->setChecked(porymapConfig.showTilesetEditorDivider);

    ActiveWindowFilter *filter = new ActiveWindowFilter(this);
    connect(filter, &ActiveWindowFilter::activated, this, &TilesetEditor::onWindowActivated);
    this->installEventFilter(filter);

    setTilesets(this->layout->tileset_primary_label, this->layout->tileset_secondary_label);

    connect(ui->checkBox_xFlip, &QCheckBox::toggled, this, &TilesetEditor::refreshTileFlips);
    connect(ui->checkBox_yFlip, &QCheckBox::toggled, this, &TilesetEditor::refreshTileFlips);

    connect(ui->actionSave_Tileset, &QAction::triggered, this, &TilesetEditor::save);

    connect(ui->actionImport_Primary_Tiles_Image,   &QAction::triggered, [this] { importTilesetTiles(this->primaryTileset); });
    connect(ui->actionImport_Secondary_Tiles_Image, &QAction::triggered, [this] { importTilesetTiles(this->secondaryTileset); });

    connect(ui->actionImport_Primary_AdvanceMap_Metatiles,   &QAction::triggered, [this] { importAdvanceMapMetatiles(this->primaryTileset); });
    connect(ui->actionImport_Secondary_AdvanceMap_Metatiles, &QAction::triggered, [this] { importAdvanceMapMetatiles(this->secondaryTileset); });

    connect(ui->actionExport_Primary_Tiles_Image,   &QAction::triggered, [this] { exportTilesImage(this->primaryTileset); });
    connect(ui->actionExport_Secondary_Tiles_Image, &QAction::triggered, [this] { exportTilesImage(this->secondaryTileset); });

    connect(ui->actionExport_Primary_Porytiles_Layer_Images,   &QAction::triggered, [this] { exportPorytilesLayerImages(this->primaryTileset); });
    connect(ui->actionExport_Secondary_Porytiles_Layer_Images, &QAction::triggered, [this] { exportPorytilesLayerImages(this->secondaryTileset); });

    connect(ui->actionExport_Metatiles_Image, &QAction::triggered, [this] { exportMetatilesImage(); });

    connect(ui->spinBox_paletteSelector, QOverload<int>::of(&QSpinBox::valueChanged), this, &TilesetEditor::refreshPaletteId);

    {
        auto *undoFilter = new UndoShortcutFilter(this);
        ui->lineEdit_MetatileLabel->installEventFilter(undoFilter);
        if (QLineEdit *paletteEdit = ui->spinBox_paletteSelector->findChild<QLineEdit *>())
            paletteEdit->installEventFilter(undoFilter);
    }
    connect(ui->lineEdit_MetatileLabel, &QLineEdit::editingFinished, this, &TilesetEditor::onLabelEditingFinished);
    connect(ui->lineEdit_MetatileLabel, &QLineEdit::textEdited, this, [this] { this->labelEditedSinceSelection = true; });

    initMetatileSelector();
    initLayerBar();
    initFieldPanel();
    initBehaviorPage();
    initTileSelector();
    initSheetPainting();
    initSelectedTileItem();
    initShortcuts();
    this->metatileSelector->select(0);
    initKindTabs();
    restoreWindowState();
}

TilesetEditor::~TilesetEditor()
{
    delete ui;
    delete paletteEditor;
    delete primaryTileset;
    delete secondaryTileset;
    delete copiedMetatile;
    delete copiedPorytile;
    delete metatileImageExportSettings;
    this->paintHistory.clear();
    this->behaviorHistory.clear();
    this->porytilePaintHistory.clear();
    this->porytileBehaviorHistory.clear();
}


BlockKind TilesetEditor::kind() const {
    return ui->tabWidget_TilesetEditor->currentWidget() == ui->tab_Porytiles ? BlockKind::Porytile : BlockKind::Metatile;
}

// The outer tabs: Porytiles first, then Generated Metatiles. The inner tab widget (Paint | Behavior) with everything in it moves into the outer
// page that is showing; the sheets switch the kind of block they show, and the layer bar (metatiles only) hides on the Porytiles page.
void TilesetEditor::initKindTabs() {
    connect(ui->tabWidget_TilesetEditor, &QTabWidget::currentChanged, this, &TilesetEditor::onKindTabChanged);
    updatePorytilesTabVisibility();
    onKindTabChanged(ui->tabWidget_TilesetEditor->currentIndex());
}

// CUSTOM ENGINE: see the declaration in the header.
void TilesetEditor::updatePorytilesTabVisibility() {
    const bool active = projectConfig.tripleLayerMetatilesEnabled;
    const int index = ui->tabWidget_TilesetEditor->indexOf(ui->tab_Porytiles);
    ui->tabWidget_TilesetEditor->setTabVisible(index, active);
    ui->tabWidget_TilesetEditor->setCurrentWidget(active ? ui->tab_Porytiles : ui->tab_GeneratedMetatiles);
}

void TilesetEditor::onKindTabChanged(int) {
    if (this->strokeOpen)
        endStroke();
    const BlockKind newKind = kind();
    const BlockKind oldKind = this->metatileSelector->blockKind();
    if (newKind != oldKind)
        this->rememberedSelection[oldKind == BlockKind::Porytile ? 1 : 0] = this->metatileSelector->getSelectedMetatileId();
    // the inner tabs go into the outer page that is showing
    QWidget *page = ui->tabWidget_TilesetEditor->currentWidget();
    if (ui->tabWidget_MetatileEditor->parentWidget() != page) {
        auto *pageLayout = static_cast<QBoxLayout *>(page->layout());
        pageLayout->addWidget(ui->tabWidget_MetatileEditor);
        ui->tabWidget_MetatileEditor->show();
    }
    const bool porytiles = newKind == BlockKind::Porytile;
    ui->actionSwap_Metatiles->setChecked(false);
    this->metatileSelector->setKind(newKind);
    if (this->behaviorSelector) {
        this->behaviorSelector->setKind(newKind);
        this->behaviorSelector->clearTileSelection();
    }
    if (QWidget *bar = findChild<QWidget *>(QStringLiteral("widget_MetatileLayerBar")))
        bar->setVisible(!porytiles);
    if (!porytiles) {   // (the porytile sheet has one layer; the metatile sheet paints on the layer the bar shows)
        for (int i = 0; i < 3; i++)
            if (this->layerSelectButtons[i] && this->layerSelectButtons[i]->isChecked())
                this->metatileSelector->setActiveLayer(i);
    }
    // metatile-only features
    if (porytiles) {
        this->metatileSelector->selectorShowUnused = false;
        this->metatileSelector->selectorShowCounts = false;
    } else {
        this->metatileSelector->selectorShowUnused = ui->actionShow_Unused->isChecked();
        this->metatileSelector->selectorShowCounts = ui->actionShow_Counts->isChecked();
        if (this->metatileSelector->selectorShowUnused || this->metatileSelector->selectorShowCounts)
            countMetatileUsage();
    }
    for (QAction *action : { ui->actionShow_Unused, ui->actionShow_Counts, ui->actionImport_Primary_AdvanceMap_Metatiles, ui->actionImport_Secondary_AdvanceMap_Metatiles,
                             ui->actionExport_Metatiles_Image, ui->actionExport_Primary_Porytiles_Layer_Images, ui->actionExport_Secondary_Porytiles_Layer_Images })
        action->setEnabled(!porytiles);
    ui->actionSwap_Metatiles->setEnabled(!porytiles && !onBehaviorPage());
    ui->actionChange_Metatiles_Count->setText(porytiles ? QStringLiteral("Change Number of Porytiles...") : QStringLiteral("Change Number of Metatiles..."));
    ui->actionPaste->setEnabled(clipboardBlock() != nullptr && !onBehaviorPage());
    // the selection of this kind
    uint16_t selected = this->rememberedSelection[porytiles ? 1 : 0];
    if (!blockIsValid(selected))
        selected = this->primaryTileset->firstMetatileId();
    this->metatileSelector->select(selected);
    if (onBehaviorPage()) {
        this->behaviorSelector->setSelectedCells({ selected });
        onBehaviorSelectionChanged();
    }
    if (this->behaviorFilterCombo)
        resetBehaviorFilter();
    refreshBehaviorFilterChoices();
    updateEditHistoryActions();
    redrawMetatileSelector();
    redrawBehaviorView();
    if (porytiles && !this->primaryTileset->porytilesLoadError.isEmpty())
        this->ui->statusbar->showMessage(this->primaryTileset->porytilesLoadError, 15000);
    else if (porytiles && !this->secondaryTileset->porytilesLoadError.isEmpty())
        this->ui->statusbar->showMessage(this->secondaryTileset->porytilesLoadError, 15000);
}

// Entry 0 -- metatile 0 and porytile 0 -- is the erase entry of the maps (the "delete id"): all of its tiles are tile 0 with palette 0, and it stays
// like that. Every way of changing a block asks here first (or refuses by itself); loading the tileset empties it should it ever not be.
bool TilesetEditor::allowEntryZero(const QSet<uint16_t> &ids) {
    if (!ids.contains(0))
        return true;
    refuseEntryZero();
    return false;
}

void TilesetEditor::refuseEntryZero() {
    this->ui->statusbar->showMessage(QStringLiteral("Entry 0 is the erase entry of the maps: it is always empty and is never edited."), 6000);
}

// The main window calls this on every map switch. A map that uses the tileset pair this editor already shows changes nothing about the tilesets:
// the editor keeps its copies, its unsaved edits AND its undo history (also right after a Save). Only a different pair goes through updateTilesets().
void TilesetEditor::update(Layout *layout, QString primaryTilesetLabel, QString secondaryTilesetLabel) {
    this->updateLayout(layout);
    if (primaryTilesetLabel == this->primaryTileset->name && secondaryTilesetLabel == this->secondaryTileset->name) {
        if (this->metatileSelector->selectorShowUnused || this->metatileSelector->selectorShowCounts) {
            countMetatileUsage();
            this->metatileSelector->draw();
        }
        return;
    }
    this->updateTilesets(primaryTilesetLabel, secondaryTilesetLabel);
}

void TilesetEditor::updateLayout(Layout *layout) {
    this->layout = layout;
    this->metatileSelector->layout = layout;
    if (this->behaviorSelector)
        this->behaviorSelector->layout = layout;
}

// Loads the given tilesets into the editor (a different pair than before, or a reload after a change from outside, e.g. a script).
void TilesetEditor::updateTilesets(QString primaryTilesetLabel, QString secondaryTilesetLabel) {
    if (tilesetDirty()) {
        auto result = SaveChangesMessage::show(QStringLiteral("Tileset"), false, this);
        if (result == QMessageBox::Yes && !this->saveTilesetData()) {
            // (the edits are not on disk: they stay, and so does the pair the editor shows)
            this->ui->statusbar->showMessage(QStringLiteral("The tileset could not be saved: the Tileset Editor keeps what it has."), 8000);
            return;
        }
    }
    this->setTilesets(primaryTilesetLabel, secondaryTilesetLabel);
    this->refresh();
}

// CUSTOM ENGINE: Write to Finalmap / Pull to Porymap changed the project's tilesets while this editor was open. It works on
// copies, so it takes fresh ones (the caller has made sure nothing here is unsaved) and its histories start over: their steps
// describe blocks that the transfer may have rewritten.
void TilesetEditor::reloadTilesetsFromProject() {
    if (!this->primaryTileset || !this->secondaryTileset)
        return;
    const QString primaryLabel = this->primaryTileset->name, secondaryLabel = this->secondaryTileset->name;
    this->setTilesets(primaryLabel, secondaryLabel);
    this->refresh();
    setTilesetDirty(false);
    this->ui->statusbar->showMessage(QStringLiteral("The tilesets were rebuilt outside this window (Write / Pull); it shows the new data."), 8000);
}

bool TilesetEditor::selectMetatile(uint16_t metatileId) {
    if (!Tileset::metatileIsValid(metatileId, this->primaryTileset, this->secondaryTileset) || this->lockSelection)
        return false;
    if (kind() != BlockKind::Metatile) {
        this->rememberedSelection[0] = metatileId;   // (shown when the Generated Metatiles tab comes up)
        return true;
    }
    this->metatileSelector->select(metatileId);
    if (onBehaviorPage())
        this->behaviorSelector->setSelectedCells({ metatileId });   // (the label field belongs to the selection of the page that is showing)
    this->redrawMetatileSelector();
    return true;
}

// The selected METATILE (what the main window and the scripts ask for), also while the Porytiles tab is showing.
uint16_t TilesetEditor::getSelectedMetatileId() {
    if (this->metatileSelector->blockKind() != BlockKind::Metatile)
        return this->rememberedSelection[0];
    return this->metatileSelector->getSelectedMetatileId();
}

// The selected block of the kind that is showing (what every editing function works on).
uint16_t TilesetEditor::selectedBlockId() const {
    return this->metatileSelector->getSelectedMetatileId();
}

void TilesetEditor::setTilesets(QString primaryTilesetLabel, QString secondaryTilesetLabel) {
    this->metatileReloadQueue.clear();
    this->metatile = nullptr;   // (it points into the tileset that is about to be replaced; selecting a metatile below fetches it again)
    Tileset *primaryTileset = project->getTileset(primaryTilesetLabel);
    Tileset *secondaryTileset = project->getTileset(secondaryTilesetLabel);
    delete this->primaryTileset;
    delete this->secondaryTileset;
    this->primaryTileset = new Tileset(*primaryTileset);
    this->secondaryTileset = new Tileset(*secondaryTileset);
    this->tilesetGeneration = this->project ? this->project->tilesetTransferGeneration : 0;
    if (this->paletteEditor) this->paletteEditor->setTilesets(this->primaryTileset, this->secondaryTileset);
    // (marked tiles and selected fields are positions on the sheet: on other tilesets they would be other metatiles)
    if (this->metatileSelector)
        this->metatileSelector->clearTileSelection();
    if (this->behaviorSelector)
        this->behaviorSelector->clearTileSelection();
    initMetatileHistory();
}

void TilesetEditor::initMetatileSelector()
{
    this->metatileSelector = new TilesetEditorMetatileSelector(projectConfig.metatileSelectorWidth, this->primaryTileset, this->secondaryTileset, this->layout);
    this->metatileSelector->behaviorOpacity = qBound(0, porymapConfig.tilesetEditorBehaviorOpacity, 100) / 100.0;
    connect(this->metatileSelector, &TilesetEditorMetatileSelector::hoveredMetatileChanged,  this, &TilesetEditor::showMetatileStatus);
    connect(this->metatileSelector, &TilesetEditorMetatileSelector::hoveredMetatileCleared,  this, &TilesetEditor::onHoveredMetatileCleared);
    connect(this->metatileSelector, &TilesetEditorMetatileSelector::selectedMetatileChanged, this, &TilesetEditor::onSelectedMetatileChanged);
    connect(this->metatileSelector, &TilesetEditorMetatileSelector::swapRequested, this, &TilesetEditor::commitMetatileSwap);
    connect(ui->actionSwap_Metatiles, &QAction::toggled, this->metatileSelector, &TilesetEditorMetatileSelector::setSwapMode);

    bool showGrid = porymapConfig.showTilesetEditorMetatileGrid;
    this->ui->actionMetatile_Grid->setChecked(showGrid);
    this->metatileSelector->showGrid = showGrid;
    this->metatileSelector->showDivider = this->ui->actionShow_Tileset_Divider->isChecked();

    auto scene = new QGraphicsScene(this);
    scene->addItem(this->metatileSelector);
    this->metatileSelector->draw();

    this->ui->graphicsView_Metatiles->setScene(scene);
    this->ui->graphicsView_Metatiles->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    this->ui->horizontalSlider_MetatilesZoom->setValue(porymapConfig.tilesetEditorMetatilesZoom);
}

// ---- the Behavior page ---------------------------------------------------------------------------------------------------------------------

bool TilesetEditor::onBehaviorPage() const {
    return this->behaviorSelector && ui->tabWidget_MetatileEditor->currentWidget() == ui->tab_MetatileBehavior;
}

// Redraws cells on both sheets (the Paint page and the Behavior page show the same metatiles).
void TilesetEditor::drawOnSheets(const QSet<uint16_t> &metatileIds) {
    this->metatileSelector->drawMetatiles(metatileIds);
    // (the Behavior sheet is rebuilt from the tilesets when its page comes up, so a stroke on the Paint page does not have to redraw it stamp by stamp)
    if (this->behaviorSelector && onBehaviorPage()) {
        this->behaviorSelector->drawMetatiles(metatileIds);
        refreshBehaviorFilterChoices();   // (the counts in the filter boxes follow the edit)
    }
}

void TilesetEditor::initBehaviorPage() {
    // the sheet: a fixed-size view in a scroll area with a zoom slider, like the Paint page (the window never grows with the zoom)
    auto *left = new QWidget(ui->tab_MetatileBehavior);
    auto *leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    // the top row: the view filter. Behavior: a box you can type into (a name or a hex ID) and scroll; Labels: any / with a label / without one; Reset.
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(new QLabel(QStringLiteral("Show behavior:"), left));
        this->behaviorFilterCombo = new QComboBox(left);
        this->behaviorFilterCombo->setObjectName(QStringLiteral("comboBox_BehaviorFilter"));
        this->behaviorFilterCombo->setEditable(true);
        this->behaviorFilterCombo->setInsertPolicy(QComboBox::NoInsert);
        this->behaviorFilterCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        this->behaviorFilterCombo->setMinimumContentsLength(18);
        this->behaviorFilterCombo->setMaxVisibleItems(16);
        this->behaviorFilterCombo->lineEdit()->setPlaceholderText(QStringLiteral("All behaviors - type a name or a hex ID"));
        // (an editable combo box is its line edit's focus proxy: it is the combo that gets the focus and the shortcut events, so the filters go on the combo)
        this->behaviorFilterCombo->installEventFilter(new SelectAllOnFocus(this->behaviorFilterCombo));
        this->behaviorFilterCombo->installEventFilter(new UndoShortcutFilter(this->behaviorFilterCombo));
        auto *completer = new QCompleter(this->behaviorFilterCombo->model(), this->behaviorFilterCombo);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        completer->setMaxVisibleItems(16);
        this->behaviorFilterCombo->setCompleter(completer);
        this->behaviorFilterCombo->setToolTip(QStringLiteral("Shows where a behavior is used: the fields with that behavior stay as they are, all other fields get the crossed-out pink circle of \"Show Unused Metatiles\".\n"
                                                             "Open the list and scroll, or type part of a name (grass) or a hex ID (1A, 0x1A) to narrow the list down; a click or Enter chooses.\n"
                                                             "The number in brackets is how many fields have it. Combines with the Labels box; Reset shows everything again."));
        row->addWidget(this->behaviorFilterCombo, 3);
        row->addSpacing(8);
        row->addWidget(new QLabel(QStringLiteral("Labels:"), left));
        this->labelFilterCombo = new QComboBox(left);
        this->labelFilterCombo->setObjectName(QStringLiteral("comboBox_LabelFilter"));
        this->labelFilterCombo->setToolTip(QStringLiteral("Show only the fields with a label, or only the fields without one (all other fields get the crossed-out pink circle). Combines with the behavior box."));
        row->addWidget(this->labelFilterCombo, 1);
        this->filterResetButton = new QToolButton(left);
        this->filterResetButton->setObjectName(QStringLiteral("toolButton_FilterReset"));
        this->filterResetButton->setText(QStringLiteral("Reset"));
        this->filterResetButton->setToolTip(QStringLiteral("Show all fields again (no behavior, any label state)."));
        row->addWidget(this->filterResetButton);
        leftColumn->addLayout(row);
    }
    // the second row: how strongly the behavior numbers (colour + digits) cover the sheet -- the same kind of Opacity slider as the Elevation tab's
    {
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        row->addWidget(new QLabel(QStringLiteral("Opacity of the numbers:"), left));
        this->behaviorOpacitySlider = new QSlider(Qt::Horizontal, left);
        this->behaviorOpacitySlider->setObjectName(QStringLiteral("horizontalSlider_BehaviorOpacity"));
        this->behaviorOpacitySlider->setRange(0, 100);
        this->behaviorOpacitySlider->setMaximumWidth(260);
        this->behaviorOpacitySlider->setValue(qBound(0, porymapConfig.tilesetEditorBehaviorOpacity, 100));
        this->behaviorOpacitySlider->setToolTip(QStringLiteral("How strongly the behavior numbers (their colour and digits) cover the sheet: low to see the picture below, high to read the numbers clearly.\n"
                                                               "0% hides them. The setting is the same for the Porytiles and the Generated Metatiles."));
        this->behaviorOpacityValue = new QLabel(QString("%1%").arg(this->behaviorOpacitySlider->value()), left);
        this->behaviorOpacityValue->setObjectName(QStringLiteral("label_BehaviorOpacityValue"));
        this->behaviorOpacityValue->setMinimumWidth(38);
        row->addWidget(this->behaviorOpacitySlider, 1);
        row->addWidget(this->behaviorOpacityValue);
        row->addStretch(1);
        leftColumn->addLayout(row);
        connect(this->behaviorOpacitySlider, &QSlider::valueChanged, this, &TilesetEditor::onBehaviorOpacityChanged);
    }
    leftColumn->addWidget(makeBehaviorLegend(left));
    this->behaviorScroll = new QScrollArea(left);
    this->behaviorScroll->setObjectName(QStringLiteral("scrollArea_BehaviorSheet"));
    this->behaviorScroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    this->behaviorScroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    this->behaviorView = new QGraphicsView(this->behaviorScroll);
    this->behaviorView->setObjectName(QStringLiteral("graphicsView_BehaviorSheet"));
    this->behaviorView->setFrameShape(QFrame::NoFrame);
    this->behaviorView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->behaviorView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->behaviorView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    this->behaviorScroll->setWidget(this->behaviorView);
    leftColumn->addWidget(this->behaviorScroll, 1);
    this->behaviorZoomSlider = new QSlider(Qt::Horizontal, left);
    this->behaviorZoomSlider->setObjectName(QStringLiteral("horizontalSlider_BehaviorZoom"));
    this->behaviorZoomSlider->setRange(ui->horizontalSlider_MetatilesZoom->minimum(), ui->horizontalSlider_MetatilesZoom->maximum());
    this->behaviorZoomSlider->setValue(porymapConfig.tilesetEditorMetatilesZoom);
    leftColumn->addWidget(this->behaviorZoomSlider);
    // (one zoom for both sheets)
    connect(this->behaviorZoomSlider, &QSlider::valueChanged, ui->horizontalSlider_MetatilesZoom, &QSlider::setValue);
    connect(ui->horizontalSlider_MetatilesZoom, &QSlider::valueChanged, this->behaviorZoomSlider, [this](int value) {
        const QSignalBlocker blocker(this->behaviorZoomSlider);
        this->behaviorZoomSlider->setValue(value);
    });

    this->behaviorSelector = new TilesetEditorMetatileSelector(projectConfig.metatileSelectorWidth, this->primaryTileset, this->secondaryTileset, this->layout);
    this->behaviorSelector->behaviorOpacity = qBound(0, porymapConfig.tilesetEditorBehaviorOpacity, 100) / 100.0;
    this->behaviorSelector->setMode(TilesetEditorMetatileSelector::SheetMode::Behavior);
    this->behaviorSelector->showBehavior = true;   // (the numbers are always there on this page: how strongly is the Opacity slider's business, 0% hides them)
    this->behaviorSelector->showGrid = porymapConfig.showTilesetEditorMetatileGrid;
    this->behaviorSelector->showDivider = ui->actionShow_Tileset_Divider->isChecked();
    auto scene = new QGraphicsScene(this);
    scene->addItem(this->behaviorSelector);
    this->behaviorSelector->draw();
    this->behaviorView->setScene(scene);
    connect(this->behaviorSelector, &TilesetEditorMetatileSelector::tileSelectionChanged, this, &TilesetEditor::onBehaviorSelectionChanged);
    connect(this->behaviorSelector, &TilesetEditorMetatileSelector::hoveredMetatileChanged, this, &TilesetEditor::showMetatileStatus);
    connect(this->behaviorSelector, &TilesetEditorMetatileSelector::hoveredMetatileCleared, this, &TilesetEditor::onHoveredMetatileCleared);

    // the right column: behavior chooser + the label
    this->behaviorPanel = new SheetBehaviorPanel(ui->tab_MetatileBehavior);
    QMap<int, QString> behaviors;
    for (auto i = project->metatileBehaviorMapInverse.constBegin(); i != project->metatileBehaviorMapInverse.constEnd(); i++)
        behaviors.insert(static_cast<int>(i.key()), i.value());
    this->behaviorPanel->setBehaviors(behaviors);
    this->behaviorPanel->setLabelWidget(ui->frame_MetatileLabel);
    connect(this->behaviorPanel, &SheetBehaviorPanel::behaviorChosen, this, &TilesetEditor::assignBehavior);
    connect(this->behaviorPanel, &SheetBehaviorPanel::clearBehaviorRequested, this, &TilesetEditor::clearBehaviors);
    {
        auto *filterFilter = new UndoShortcutFilter(this);
        this->behaviorPanel->filterEdit()->installEventFilter(filterFilter);
    }

    auto *split = new QSplitter(Qt::Horizontal, ui->tab_MetatileBehavior);
    split->setObjectName(QStringLiteral("splitter_MetatileBehavior"));
    split->setChildrenCollapsible(false);
    split->addWidget(left);
    split->addWidget(this->behaviorPanel);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    ui->horizontalLayout_MetatileBehavior->addWidget(split);

    connect(ui->tabWidget_MetatileEditor, &QTabWidget::currentChanged, this, &TilesetEditor::onMetatileEditorTabChanged);
    refreshBehaviorFilterChoices();
    connect(this->behaviorFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TilesetEditor::onBehaviorFilterChanged);
    connect(this->labelFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &TilesetEditor::onBehaviorFilterChanged);
    connect(this->behaviorFilterCombo->lineEdit(), &QLineEdit::editingFinished, this, &TilesetEditor::onBehaviorFilterEdited);
    connect(this->filterResetButton, &QToolButton::clicked, this, &TilesetEditor::resetBehaviorFilter);
    redrawBehaviorView();
    onBehaviorSelectionChanged();
}

// The Opacity slider of the Behavior page: the sheet of that page redraws with the new value (the Paint page never shows the numbers).
void TilesetEditor::onBehaviorOpacityChanged(int percent) {
    percent = qBound(0, percent, 100);
    porymapConfig.tilesetEditorBehaviorOpacity = percent;
    if (this->behaviorOpacityValue)
        this->behaviorOpacityValue->setText(QString("%1%").arg(percent));
    const qreal opacity = percent / 100.0;
    if (this->behaviorSelector) {
        this->behaviorSelector->behaviorOpacity = opacity;
        this->behaviorSelector->draw();
    }
}

// The boxes of the view filter. Behavior: "All behaviors" and one entry per behavior that some field has (name, hex ID and the number of fields); the
// choice survives an edit, and a behavior that no field has any more stays in the list (with 0) while it is chosen. Labels: any / with / without, with
// the numbers of fields.
void TilesetEditor::refreshBehaviorFilterChoices() {
    if (!this->behaviorFilterCombo || !this->labelFilterCombo)
        return;
    QMap<int, int> perBehavior;
    int labeled = 0, total = 0;
    for (int id = 0; id < Project::getNumMetatilesTotal(); id++) {
        const Metatile *metatile = block(id);
        if (!metatile)
            continue;
        total++;
        perBehavior[static_cast<int>(metatile->behavior())]++;
        if (!ownedLabel(id).isEmpty())
            labeled++;
    }
    const int chosen = this->behaviorFilterCombo->currentData().isValid() ? this->behaviorFilterCombo->currentData().toInt() : -1;
    const int chosenLabels = this->labelFilterCombo->currentData().isValid() ? this->labelFilterCombo->currentData().toInt() : kLabelsAny;
    if (chosen >= 0 && !perBehavior.contains(chosen))
        perBehavior[chosen] = 0;

    {
        const QSignalBlocker blocker(this->behaviorFilterCombo);
        this->behaviorFilterCombo->clear();
        this->behaviorFilterCombo->addItem(QString("All behaviors (%1 fields)").arg(total), -1);
        for (auto it = perBehavior.constBegin(); it != perBehavior.constEnd(); ++it) {
            const QString name = this->behaviorPanel ? this->behaviorPanel->nameOf(it.key()) : QString();
            QString text = QString("0x%1").arg(QString("%1").arg(it.key(), 2, 16, QChar('0')).toUpper());
            if (!name.isEmpty())
                text += "  " + name;
            this->behaviorFilterCombo->addItem(text + QString("  (%1)").arg(it.value()), it.key());
            this->behaviorFilterCombo->setItemData(this->behaviorFilterCombo->count() - 1, text, Qt::UserRole + 1);   // (what a typed search is matched against: no count)
        }
        const int index = this->behaviorFilterCombo->findData(chosen);
        this->behaviorFilterCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
    {
        const QSignalBlocker blocker(this->labelFilterCombo);
        this->labelFilterCombo->clear();
        this->labelFilterCombo->addItem(QStringLiteral("Any label state"), kLabelsAny);
        this->labelFilterCombo->addItem(QString("With a label (%1)").arg(labeled), kLabelsWith);
        this->labelFilterCombo->addItem(QString("Without a label (%1)").arg(total - labeled), kLabelsWithout);
        const int index = this->labelFilterCombo->findData(chosenLabels);
        this->labelFilterCombo->setCurrentIndex(index >= 0 ? index : 0);
    }
}

// One of the boxes changed: the sheet of the Behavior page marks every field that does not match both with the crossed-out circle.
void TilesetEditor::onBehaviorFilterChanged() {
    if (!this->behaviorSelector || !this->behaviorFilterCombo || !this->labelFilterCombo)
        return;
    using Labels = TilesetEditorMetatileSelector::LabelFilter;
    const int behavior = this->behaviorFilterCombo->currentData().isValid() ? this->behaviorFilterCombo->currentData().toInt() : -1;
    const int labelState = this->labelFilterCombo->currentData().toInt();
    this->behaviorSelector->setFieldFilter(behavior, labelState == kLabelsWith ? Labels::WithLabel : labelState == kLabelsWithout ? Labels::WithoutLabel : Labels::Any);
    QTimer::singleShot(0, this, [this] { refreshBehaviorFilterChoices(); });   // (an entry that no field has any more goes once it is not the chosen one; not from inside the box's own signal)
    {
        QWidget *focused = this->focusWidget();
        if (focused == this->behaviorFilterCombo || focused == this->behaviorFilterCombo->lineEdit() || focused == this->labelFilterCombo)
            this->behaviorView->setFocus();   // (so that Esc and Delete work on the sheet again right after a choice)
    }
    if (!this->behaviorSelector->fieldFilterActive()) {
        this->ui->statusbar->clearMessage();
        return;
    }
    int matching = 0;
    for (int id = 0; id < Project::getNumMetatilesTotal(); id++)
        if (block(id) && this->behaviorSelector->fieldMatchesFilter(id))
            matching++;
    this->ui->statusbar->showMessage(QString("%1 field(s) match the filter; all other fields carry the crossed-out circle. Reset shows everything again.").arg(matching), 8000);
}

// Enter (or leaving the box) after typing into the behavior box: empty = all behaviors; otherwise the first behavior whose name or hex ID contains
// what was typed is chosen (picking from the list that pops up while typing does the same by itself).
void TilesetEditor::onBehaviorFilterEdited() {
    QComboBox *combo = this->behaviorFilterCombo;
    const QString typed = combo->lineEdit()->text().trimmed();
    if (typed.isEmpty()) {
        combo->setCurrentIndex(0);
        return;
    }
    if (typed == combo->itemText(combo->currentIndex()))
        return;
    // A hex ID (0x1A, 1A, 1a) picks exactly that behavior; anything else the first behavior whose ID or name contains what was typed. The search
    // text of an entry has no count in it, and the "All behaviors" entry is not a behavior.
    QString hex = typed;
    if (hex.startsWith("0x", Qt::CaseInsensitive))
        hex = hex.mid(2);
    bool isHex = false;
    const int hexValue = hex.size() <= 2 ? hex.toInt(&isHex, 16) : 0;
    if (isHex) {
        for (int i = 1; i < combo->count(); i++) {
            if (combo->itemData(i).toInt() == hexValue) {
                combo->setCurrentIndex(i);
                return;
            }
        }
    }
    for (int i = 1; i < combo->count(); i++) {
        if (combo->itemData(i, Qt::UserRole + 1).toString().contains(typed, Qt::CaseInsensitive)) {
            combo->setCurrentIndex(i);
            return;
        }
    }
    this->ui->statusbar->showMessage(QString("No behavior matches \"%1\".").arg(typed), 5000);
    combo->setEditText(combo->itemText(combo->currentIndex()));
}

void TilesetEditor::resetBehaviorFilter() {
    {
        const QSignalBlocker blockBehavior(this->behaviorFilterCombo), blockLabels(this->labelFilterCombo);
        this->behaviorFilterCombo->setCurrentIndex(0);
        this->labelFilterCombo->setCurrentIndex(0);
    }
    onBehaviorFilterChanged();
}

void TilesetEditor::redrawBehaviorView() {
    if (!this->behaviorView)
        return;
    QSize size(this->behaviorSelector->pixmap().width(), this->behaviorSelector->pixmap().height());
    this->behaviorView->setSceneRect(0, 0, size.width(), size.height());
    const double scale = pow(3.0, static_cast<double>(porymapConfig.tilesetEditorMetatilesZoom - 30) / 30.0);
    QTransform transform;
    transform.scale(scale, scale);
    size *= scale;
    this->behaviorView->setTransform(transform);
    this->behaviorView->setFixedSize(size.width() + 2, size.height() + 2);
}

// The selection of the Behavior page changed: say what it is, highlight its behavior, and put the label of a single metatile in the label field.
void TilesetEditor::onBehaviorSelectionChanged() {
    if (!this->behaviorPanel)
        return;
    const QList<uint16_t> ids = this->behaviorSelector->selectedCells();
    QSet<int> values;
    for (uint16_t id : ids) {
        const Metatile *metatile = block(id);
        if (metatile)
            values.insert(metatile->behavior());
    }
    this->behaviorPanel->setSelection(ids.size(), values, blockName().toLower());
    this->labelEditedSinceSelection = false;
    if (ids.size() == 1) {
        this->ui->lineEdit_MetatileLabel->setEnabled(true);
        this->metatileSelector->select(ids.first());   // the current metatile: its label shows in the field (onSelectedMetatileChanged)
    } else {
        this->ui->lineEdit_MetatileLabel->setEnabled(!ids.isEmpty());
        this->ui->lineEdit_MetatileLabel->clear();
        this->ui->lineEdit_MetatileLabel->setPlaceholderText(ids.isEmpty() ? QString() : QStringLiteral("Name: numbers them Name_1, Name_2, ..."));
    }
}

// One click on a behavior: every selected metatile gets it. One undo step.
void TilesetEditor::assignBehavior(int value) {
    const QList<uint16_t> ids = this->behaviorSelector->selectedCells();
    if (ids.isEmpty()) {
        this->ui->statusbar->showMessage(QStringLiteral("Nothing is selected: select one or more fields first."), 5000);
        return;
    }
    if (this->strokeOpen)
        return;
    auto *step = new MetatileHistoryItem();
    QSet<uint16_t> changed;
    bool skippedZero = false;
    for (uint16_t id : ids) {
        if (id == 0) {   // (the erase entry has no behavior and never gets one)
            skippedZero = true;
            continue;
        }
        Metatile *metatile = block(id);
        if (!metatile || metatile->behavior() == static_cast<uint32_t>(value))
            continue;
        MetatileEdit edit;
        edit.id = id;
        edit.prev = *metatile;
        edit.prevLabel = edit.nextLabel = ownedLabel(id);
        metatile->setAttribute(Metatile::Attr::Behavior, value);
        edit.next = *metatile;
        step->edits.append(edit);
        changed.insert(id);
    }
    if (step->edits.isEmpty()) {
        delete step;
        if (skippedZero) {
            refuseEntryZero();
            return;
        }
        if (!this->lastBehaviorApplied.isValid() || this->lastBehaviorApplied.elapsed() > 1500)   // (not right after the click that did apply it: a double click would report "already")
            this->ui->statusbar->showMessage(QString("The selected metatile(s) already have behavior %1.").arg(this->behaviorPanel->nameOf(value)), 4000);
        return;
    }
    step->metatileId = step->edits.first().id;
    step->stack = MetatileHistoryItem::Stack::Behavior;
    commit(step);
    this->lastBehaviorApplied.restart();
    drawOnSheets(changed);
    onBehaviorSelectionChanged();
    this->ui->statusbar->showMessage(value == 0 ? QString("Removed the behavior of %1 metatile(s) (MB_NORMAL). Undo brings it back.").arg(changed.size())
                                                : QString("Behavior %1 set on %2 metatile(s).").arg(this->behaviorPanel->nameOf(value)).arg(changed.size()), 5000);
}

// The label field with several metatiles selected: Name -> Name_1, Name_2, ... in reading order, as one undo step. A name that another metatile
// already has is refused (the labels of a tileset must be unique), and then nothing at all changes.
void TilesetEditor::assignNumberedLabels(const QString &baseName) {
    if (this->strokeOpen)
        return;
    const QString base = baseName.trimmed();
    const QList<uint16_t> ids = this->behaviorSelector->selectedCells();
    if (base.isEmpty() || ids.size() < 2)
        return;   // (an empty field with several metatiles selected changes nothing)
    if (ids.contains(0)) {   // (the erase entry is never named: nothing at all changes)
        refuseEntryZero();
        return;
    }
    IdentifierValidator validator;
    QStringList names;
    for (int i = 0; i < ids.size(); i++)
        names << QString("%1_%2").arg(base).arg(i + 1);
    for (const QString &name : names) {
        if (!validator.isValid(name)) {
            this->ui->statusbar->showMessage(QString("\"%1\" is not a valid label (letters, digits and underscores).").arg(name), 6000);
            return;
        }
    }
    const QSet<uint16_t> selected(ids.begin(), ids.end());
    for (Tileset *tileset : { this->primaryTileset, this->secondaryTileset }) {
        for (auto it = tileset->blockLabels(kind()).constBegin(); it != tileset->blockLabels(kind()).constEnd(); ++it) {
            if (!selected.contains(static_cast<uint16_t>(it.key())) && names.contains(it.value())) {
                this->ui->statusbar->showMessage(QString("Nothing changed: the label \"%1\" belongs to another metatile.").arg(it.value()), 8000);
                return;
            }
        }
    }
    auto *step = new MetatileHistoryItem();
    for (int i = 0; i < ids.size(); i++) {
        Metatile *metatile = block(ids.at(i));
        if (!metatile)
            continue;
        MetatileEdit edit;
        edit.id = ids.at(i);
        edit.prev = *metatile;
        edit.prevLabel = ownedLabel(edit.id);
        if (edit.prevLabel == names.at(i))
            continue;
        setLabel(edit.id, names.at(i));
        edit.next = *metatile;
        edit.nextLabel = names.at(i);
        step->edits.append(edit);
    }
    if (step->edits.isEmpty()) {
        delete step;
        return;
    }
    step->metatileId = step->edits.first().id;
    step->stack = MetatileHistoryItem::Stack::Behavior;
    commit(step);
    drawOnSheets(QSet<uint16_t>(ids.begin(), ids.end()));   // (the view filter and its counts may depend on the labels)
    this->labelEditedSinceSelection = false;
    this->ui->lineEdit_MetatileLabel->clear();
    this->ui->statusbar->showMessage(QString("Named %1 metatiles %2 ... %3.").arg(step->edits.size()).arg(names.first(), names.last()), 6000);
}

void TilesetEditor::onLabelEditingFinished() {
    if (onBehaviorPage() && this->behaviorSelector->selectedCells().size() > 1) {
        if (this->labelEditedSinceSelection)
            assignNumberedLabels(this->ui->lineEdit_MetatileLabel->text());
        return;
    }
    commitMetatileLabel();
}

// Switching between Paint and Behavior: the page that comes up shows the current pictures, and the actions that only make sense on the Paint page
// (they work on the one selected metatile of that sheet) are off on the Behavior page.
void TilesetEditor::onMetatileEditorTabChanged(int) {
    const bool behavior = onBehaviorPage();
    if (behavior) {
        this->behaviorSelector->setTilesets(this->primaryTileset, this->secondaryTileset);   // (edits made on the Paint page)
        refreshBehaviorFilterChoices();
        redrawBehaviorView();
        this->behaviorSelector->setSelectedCells({ selectedBlockId() });
        onBehaviorSelectionChanged();   // (also when the selection is the same cell: what the Paint page did to it shows in the panel)
        ensureMetatileVisible(selectedBlockId());
    } else {
        this->metatileSelector->drawMetatiles(QSet<uint16_t>());   // (edits made on the Behavior page)
        this->metatileSelector->select(selectedBlockId());
    }
    updateEditHistoryActions();   // (Undo / Redo belong to the page that is showing now)
    for (QAction *action : { ui->actionCut, ui->actionCopy, ui->actionPaste })
        action->setEnabled(!behavior);
    ui->actionSwap_Metatiles->setEnabled(!behavior && kind() == BlockKind::Metatile);
    if (behavior)
        ui->actionSwap_Metatiles->setChecked(false);
    else
        ui->actionPaste->setEnabled(clipboardBlock() != nullptr);
}

// CUSTOM ENGINE: the bar above the metatile sheet: Bottom | Middle | Top (exclusive: the layer that gets painted) with an eye each (show or hide that
// layer in the sheet, independent of the active one, so the result can be seen with any combination). There are no tools: the mouse paints, and
// Cmd turns it into a selection (the legend of the controls is in the Field panel, see initFieldPanel). "Display Behavior" lives only in the View menu.
void TilesetEditor::initLayerBar() {
    auto *bar = new QWidget(ui->frame_Metatiles);
    bar->setObjectName(QStringLiteral("widget_MetatileLayerBar"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);

    static const QColor layerColors[3] = { QColor(70, 130, 255), QColor(60, 190, 90), QColor(255, 150, 30) };
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    const int numLayers = qMin(3, projectConfig.getNumLayersInMetatile());
    for (int i = 0; i < numLayers; i++) {
        const QString name = Metatile::getLayerName(i);
        auto *eye = new QToolButton(bar);
        eye->setObjectName(QString("toolButton_MetatileLayerEye_%1").arg(name));
        eye->setAutoRaise(true);
        eye->setCheckable(true);
        eye->setChecked(true);
        eye->setIcon(QIcon(":/icons/folder_eye_open.ico"));
        eye->setToolTip(QString("Show or hide the %1 layer in this sheet (it does not change which layer you paint on).").arg(name));

        auto *select = new QToolButton(bar);
        select->setObjectName(QString("toolButton_MetatileLayer_%1").arg(name));
        select->setText(name);
        select->setCheckable(true);
        select->setChecked(i == (numLayers == 3 ? 1 : 0));
        select->setToolTip(QString("Paint on the %1 layer. Everything you paint, pick or stamp on the sheet goes to this one layer only.").arg(name));
        select->setStyleSheet(QString("QToolButton { padding: 2px 8px; border: 1px solid transparent; border-bottom: 3px solid %1; }"
                                      "QToolButton:checked { font-weight: bold; background: palette(midlight); border: 1px solid palette(mid); border-bottom: 3px solid %1; }")
                                      .arg(layerColors[i].name()));
        group->addButton(select, i);

        row->addWidget(eye);
        row->addWidget(select);
        row->addSpacing(6);
        this->layerSelectButtons[i] = select;
        this->layerEyeButtons[i] = eye;

        connect(eye, &QToolButton::toggled, this, [this, i, eye](bool checked) {
            eye->setIcon(QIcon(checked ? ":/icons/folder_eye_open.ico" : ":/icons/folder_eye_closed.ico"));
            this->metatileSelector->setLayerVisible(i, checked);
        });
    }
    connect(group, &QButtonGroup::idClicked, this, &TilesetEditor::setActiveLayer);
    row->addStretch();

    bar->setFixedHeight(28);
    auto *column = static_cast<QBoxLayout *>(ui->frame_Metatiles->layout());
    column->insertWidget(0, bar);
    setActiveLayer(numLayers == 3 ? 1 : 0);
}

// ---- painting on the metatile sheet ---------------------------------------------------------------------------------------------------

void TilesetEditor::initSheetPainting() {
    auto *sheet = this->metatileSelector;
    sheet->setPaintingEnabled(projectConfig.tripleLayerMetatilesEnabled);
    sheet->paintBlocker = [this]() { return paintBlockReason(); };
    connect(sheet, &TilesetEditorMetatileSelector::strokeStarted, this, &TilesetEditor::beginStroke);
    connect(sheet, &TilesetEditorMetatileSelector::stampRequested, this, &TilesetEditor::paintBrushAt);
    connect(sheet, &TilesetEditorMetatileSelector::strokeFinished, this, [this] { endStroke(); });
    connect(sheet, &TilesetEditorMetatileSelector::tilePicked, this, &TilesetEditor::pickTileFromSheet);
    connect(sheet, &TilesetEditorMetatileSelector::regionPicked, this, &TilesetEditor::pickRegionFromSheet);
    connect(sheet, &TilesetEditorMetatileSelector::hoveredSlotChanged, this, &TilesetEditor::showSlotStatus);
    connect(sheet, &TilesetEditorMetatileSelector::paintRefused, this, [this](const QString &reason) {
        this->ui->statusbar->showMessage(reason, 6000);
        QToolTip::showText(QCursor::pos(), reason, this->ui->graphicsView_Metatiles, {}, 2500);
    });
    // Selecting something on the source sheet (or picking from a sheet) is what arms the brush: the window starts with none.
    connect(this->tileSelector, &TilesetEditorTileSelector::selectedTilesChanged, this, [this] {
        this->brushArmed = true;
        updateBrushGhost();
    });
    this->brushArmed = false;
    updateBrushGhost();
}

void TilesetEditor::updateBrushGhost() {
    if (!this->metatileSelector || !this->tileSelector)
        return;
    this->metatileSelector->setBrush(this->brushArmed ? this->tileSelector->brush() : TileBrush());
}

// Why a click on the sheet paints nothing right now (an empty string: it does).
QString TilesetEditor::paintBlockReason() const {
    if (!projectConfig.tripleLayerMetatilesEnabled)
        return QStringLiteral("Painting on the sheet needs triple-layer metatiles.");
    const int layer = this->metatileSelector->activeLayer();
    if (!this->metatileSelector->isLayerVisible(layer))
        return QString("The %1 layer is hidden: show it (eye) to paint on it.").arg(Metatile::getLayerName(layer));
    if (!this->brushArmed)
        return QStringLiteral("Pick something to paint first: select tiles on the right, or right-click a tile on the sheet.");
    return QString();
}

void TilesetEditor::beginStroke() {
    if (this->strokeOpen)
        return;
    this->strokeOpen = true;
    this->strokeEdits.clear();
    updateEditHistoryActions();
}

// Call BEFORE a metatile is changed: the first time in a stroke it remembers what the metatile (and its label) was.
void TilesetEditor::touchMetatile(uint16_t metatileId) {
    if (!this->strokeOpen || this->strokeEdits.contains(metatileId))
        return;
    const Metatile *metatile = block(metatileId);
    if (!metatile)
        return;
    MetatileEdit edit;
    edit.id = metatileId;
    edit.prev = *metatile;
    edit.prevLabel = ownedLabel(metatileId);
    this->strokeEdits.insert(metatileId, edit);
}

// A tile that draws nothing: colour 0 is transparent, so a tile whose pixels are all colour 0 is as blank as the tile 0 itself.
bool TilesetEditor::tileIsBlank(const Tile &tile) const {
    if (tile.tileId == 0)
        return true;
    const QImage image = getTileImage(tile.tileId, this->primaryTileset, this->secondaryTileset);
    if (image.isNull())
        return true;
    for (int y = 0; y < image.height(); y++)
        for (int x = 0; x < image.width(); x++)
            if (image.pixelIndex(x, y) != 0)
                return false;
    return true;
}

bool TilesetEditor::topLayerIsBlank(const Metatile &metatile) const {
    const int perLayer = Metatile::tilesPerLayer();
    for (int i = 0; i < perLayer; i++)
        if (!tileIsBlank(metatile.tiles.value(2 * perLayer + i)))
            return false;
    return true;
}

// A metatile that has nothing on its top layer: its layer type is COVERED (the shop preview and the decoration code still read it).
Metatile TilesetEditor::blankMetatile() const {
    Metatile blank(Tileset::tilesPerBlock(kind()));
    if (kind() == BlockKind::Metatile && projectConfig.tripleLayerMetatilesEnabled && projectConfig.metatileLayerTypeMask)
        blank.setAttribute(Metatile::Attr::LayerType, Metatile::LayerType::Covered);
    return blank;
}

// Ends the stroke: everything it changed becomes ONE history step (none, when nothing changed in the end).
bool TilesetEditor::endStroke() {
    if (!this->strokeOpen)
        return false;
    auto *step = new MetatileHistoryItem();
    for (auto it = this->strokeEdits.begin(); it != this->strokeEdits.end(); ++it) {
        MetatileEdit edit = it.value();
        Metatile *metatile = block(edit.id);
        if (!metatile)
            continue;
        // The layer type follows the top layer: COVERED while it draws nothing, else NORMAL. Only when the top layer flips between the
        // two (a metatile whose top layer keeps its state, e.g. one of the few SPLIT ones, keeps its type).
        if (kind() == BlockKind::Metatile && projectConfig.tripleLayerMetatilesEnabled && projectConfig.metatileLayerTypeMask) {
            const bool wasBlank = topLayerIsBlank(edit.prev), isBlank = topLayerIsBlank(*metatile);
            if (wasBlank != isBlank)
                metatile->setAttribute(Metatile::Attr::LayerType, isBlank ? Metatile::LayerType::Covered : Metatile::LayerType::Normal);
        }
        edit.next = *metatile;
        edit.nextLabel = ownedLabel(edit.id);
        if (edit.next.tiles == edit.prev.tiles && edit.next.getAttributes() == edit.prev.getAttributes() && edit.nextLabel == edit.prevLabel)
            continue;   // (changed and changed back)
        step->edits.append(edit);
    }
    this->strokeOpen = false;
    this->strokeEdits.clear();
    const bool changed = !step->edits.isEmpty();
    if (changed) {
        step->metatileId = step->edits.first().id;
        commit(step);
        refreshMetatileAttributes();   // (the layer type of the selected metatile may have changed)
    } else {
        delete step;
    }
    updateEditHistoryActions();
    updateWindowTitle();
    return changed;
}

// Puts one tile into slot `tileIndex` of a metatile (inside a stroke): remembers the metatile as it was, keeps the tile usage counts and reports it
// in *changed. False when nothing had to change.
bool TilesetEditor::writeSheetTile(uint16_t metatileId, int tileIndex, const Tile &value, QSet<uint16_t> *changed) {
    if (metatileId == 0)
        return false;   // (the erase entry: see allowEntryZero)
    Metatile *metatile = block(metatileId);
    if (!metatile || tileIndex < 0 || tileIndex >= metatile->tiles.size() || metatile->tiles[tileIndex] == value)
        return false;   // (nothing to change: no history, no redraw)
    touchMetatile(metatileId);
    if (this->tileSelector->showUnused && metatile->tiles[tileIndex].tileId != value.tileId) {
        this->tileSelector->usedTiles[value.tileId] += 1;
        this->tileSelector->usedTiles[metatile->tiles[tileIndex].tileId] -= 1;
    }
    metatile->tiles[tileIndex] = value;
    changed->insert(metatileId);
    return true;
}

// After tiles were written: redraw the touched cells once and bring the panels of the selected metatile up to date.
void TilesetEditor::finishSheetEdit(const QSet<uint16_t> &changed) {
    if (changed.isEmpty())
        return;
    drawOnSheets(changed);
    if (this->tileSelector->showUnused)
        this->tileSelector->draw();
    if (changed.contains(selectedBlockId())) {
        this->metatile = block(selectedBlockId());
        refreshMetatileAttributes();
        updateMetatileStatus();
    }
}

// One stamp of the brush, its top-left tile on originTile, on the ACTIVE layer only. Every brush tile goes where it lands: the tiles may
// straddle several metatiles, and the ones that fall on the padding between the tilesets or off the sheet are simply left out.
void TilesetEditor::paintBrushAt(const QPoint &originTile) {
    if (!this->strokeOpen)
        return;
    auto *sheet = this->metatileSelector;
    const TileBrush brush = this->tileSelector->brush();
    const int layer = sheet->activeLayer();
    QSet<uint16_t> changed;
    auto lands = [sheet](const QPoint &tile) { return sheet->tileSlot(tile, nullptr, nullptr); };
    const QList<TileBrush::StampWrite> writes = brush.plan(originTile, lands);
    QSet<uint16_t> targets;
    for (const TileBrush::StampWrite &write : writes) {
        uint16_t metatileId;
        if (sheet->tileSlot(write.tile, &metatileId, nullptr))
            targets.insert(metatileId);
    }
    const bool zeroAllowed = allowEntryZero(targets);
    for (const TileBrush::StampWrite &write : writes) {
        uint16_t metatileId; int sub;
        sheet->tileSlot(write.tile, &metatileId, &sub);
        if (metatileId == 0 && !zeroAllowed)
            continue;
        writeSheetTile(metatileId, layer * Metatile::tilesPerLayer() + sub, write.value, &changed);
    }
    finishSheetEdit(changed);
}

// Delete / Backspace: the marked 8x8 tiles of the ACTIVE layer become tile 0 palette 0 (blank). One undo step.
void TilesetEditor::deleteSelectedTiles() {
    auto *sheet = this->metatileSelector;
    if (this->strokeOpen || !sheet->paintingActive() || onBehaviorPage())
        return;   // (on the Behavior page Delete belongs to nobody)
    if (sheet->tileSelection().isEmpty()) {
        this->ui->statusbar->showMessage(QString("Nothing is selected: hold %1 while you click or drag on the sheet to select tiles, then press Delete.").arg(SheetBehaviorPanel::cmdKeyName()), 6000);
        return;
    }
    const int layer = sheet->activeLayer();
    if (!sheet->isLayerVisible(layer)) {
        this->ui->statusbar->showMessage(QString("The %1 layer is hidden: show it (eye) to clear tiles on it.").arg(Metatile::getLayerName(layer)), 6000);
        return;
    }
    QSet<uint16_t> targets;
    for (const QPoint &tile : sheet->tileSelection()) {
        uint16_t metatileId;
        if (sheet->tileSlot(tile, &metatileId, nullptr))
            targets.insert(metatileId);
    }
    const bool zeroAllowed = allowEntryZero(targets);
    beginStroke();
    QSet<uint16_t> changed;
    int cleared = 0;
    for (const QPoint &tile : sheet->tileSelection()) {
        uint16_t metatileId; int sub;
        if (sheet->tileSlot(tile, &metatileId, &sub) && (metatileId != 0 || zeroAllowed) && writeSheetTile(metatileId, layer * Metatile::tilesPerLayer() + sub, Tile(), &changed))
            cleared++;
    }
    finishSheetEdit(changed);
    endStroke();
    this->ui->statusbar->showMessage(cleared ? QString("Cleared %1 tile(s) on the %2 layer.").arg(cleared).arg(Metatile::getLayerName(layer))
                                             : QString("The marked tiles are already blank on the %1 layer.").arg(Metatile::getLayerName(layer)), 5000);
}

// A text field (a spin box is one too) that has the focus keeps Esc for itself.
bool TilesetEditor::typingInTextField() const {
    QWidget *focused = this->focusWidget();
    if (auto *combo = qobject_cast<QComboBox *>(focused))
        return combo->isEditable();   // (an editable combo box takes the focus for its line edit)
    return qobject_cast<QLineEdit *>(focused) || qobject_cast<QAbstractSpinBox *>(focused);
}

// Esc: deselects what is selected on the page that is showing (marked tiles on the Paint page, selected fields on the Behavior page).
// False when there was nothing to deselect.
bool TilesetEditor::deselectAll() {
    TilesetEditorMetatileSelector *sheet = onBehaviorPage() ? this->behaviorSelector : this->metatileSelector;
    if (sheet->tileSelection().isEmpty())
        return false;
    sheet->clearTileSelection();
    return true;
}

// "Clear Field" (Paint page): the whole field becomes empty -- all three layers tile 0 palette 0, the behavior (with every other attribute) back to
// nothing, and the label gone -- for every field that holds a marked tile, or for the current field when nothing is marked. One undo step that
// brings all of it back. (The Behavior page has its own "Clear Behavior", which only takes the behavior away.)
void TilesetEditor::clearFields() {
    if (this->strokeOpen)
        return;
    QSet<uint16_t> fields;
    for (const QPoint &tile : this->metatileSelector->tileSelection()) {
        uint16_t metatileId;
        if (this->metatileSelector->tileSlot(tile, &metatileId, nullptr))
            fields.insert(metatileId);
    }
    QList<uint16_t> ids = fields.values();
    std::sort(ids.begin(), ids.end());
    if (ids.isEmpty())
        ids.append(selectedBlockId());
    if (!allowEntryZero(QSet<uint16_t>(ids.begin(), ids.end())))
        ids.removeAll(0);
    const uint32_t blankAttributes = blankMetatile().getAttributes();
    beginStroke();
    QSet<uint16_t> changed;
    for (uint16_t id : ids) {
        Metatile *metatile = block(id);
        if (!metatile)
            continue;
        for (int i = 0; i < metatile->tiles.size(); i++)
            writeSheetTile(id, i, Tile(), &changed);
        if (metatile->getAttributes() != blankAttributes) {
            touchMetatile(id);
            metatile->setAttributes(blankAttributes);
            changed.insert(id);
        }
        if (!ownedLabel(id).isEmpty()) {
            touchMetatile(id);
            setLabel(id, QString());
            if (id == selectedBlockId())
                this->ui->lineEdit_MetatileLabel->setText(QString());
            changed.insert(id);
        }
    }
    finishSheetEdit(changed);
    endStroke();
    this->ui->statusbar->showMessage(changed.isEmpty() ? QStringLiteral("The field(s) are already empty.")
                                                       : QString("Cleared %1 field(s): all layers, the behavior and the label. Undo brings them back.").arg(changed.size()), 6000);
}

// "Clear Behavior" (Behavior page): the selected fields lose their behavior (MB_NORMAL); their tiles and labels stay. One undo step.
void TilesetEditor::clearBehaviors() {
    assignBehavior(0);
}

// The Paint page's Field panel, in the free space of the right-hand column: centred between the left edge and the tile properties, and it keeps
// that when the window is resized. The Clear Field button, what it does in a sentence, and the legend of the mouse and keys under it.
void TilesetEditor::initFieldPanel() {
    auto *box = new QGroupBox(QStringLiteral("Field"), ui->frame_Properties);
    box->setObjectName(QStringLiteral("groupBox_FieldActions"));
    box->setMinimumWidth(240);
    box->setMaximumWidth(470);
    auto *column = new QVBoxLayout(box);
    this->clearFieldButton = new QPushButton(QStringLiteral("Clear Field"), box);
    this->clearFieldButton->setObjectName(QStringLiteral("pushButton_ClearField"));
    this->clearFieldButton->setToolTip(QStringLiteral("Empties the whole field: all three layers become tile 0 / palette 0, the behavior goes back to MB_NORMAL and the label is removed.\n"
                                                      "It acts on every field that holds a selected tile, or on the current field when nothing is selected.\n"
                                                      "One undo step brings everything back."));
    column->addWidget(this->clearFieldButton);
    auto *note = new QLabel(QStringLiteral("Empties fields completely: all three layers, the behavior and the label. Acts on the fields with a selected tile, or on the current field if none is selected."), box);
    note->setObjectName(QStringLiteral("label_ClearFieldNote"));
    note->setWordWrap(true);
    QFont font = note->font();
    font.setPointSizeF(qMax(8.0, font.pointSizeF() - 2.0));
    note->setFont(font);
    note->setMinimumHeight(QFontMetrics(font).lineSpacing() * 4);
    column->addWidget(note);
    auto *line = new QFrame(box);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    column->addWidget(line);
    column->addWidget(makePaintLegend(box));
    column->addStretch();
    auto *row = static_cast<QBoxLayout *>(ui->frame_Properties->layout());
    row->insertStretch(0, 1);
    row->insertWidget(1, box, 100, Qt::AlignTop);   // (it takes the free width up to its maximum; what is left over goes to both sides equally)
    row->insertStretch(2, 1);
    connect(this->clearFieldButton, &QPushButton::clicked, this, &TilesetEditor::clearFields);
}

// Right click: the tile under the mouse of the ACTIVE layer becomes the brush, exactly as it is (id, palette, flips).
void TilesetEditor::pickTileFromSheet(const QPoint &tile) {
    uint16_t metatileId; int sub;
    if (!this->metatileSelector->tileSlot(tile, &metatileId, &sub))
        return;   // (the padding between the tilesets is not a tile: it must not arm the eraser)
    const Metatile *metatile = block(metatileId);
    const int index = this->metatileSelector->activeLayer() * Metatile::tilesPerLayer() + sub;
    if (!metatile || index >= metatile->tiles.size())
        return;
    const Tile picked = metatile->tiles.at(index);
    this->tileSelector->setPicked(TileBrush::single(picked));
    if (picked.palette <= this->ui->spinBox_paletteSelector->maximum())   // (a palette the box cannot show would be clamped: the tile is picked exactly as it is)
        setPaletteId(picked.palette);
    this->tileSelector->highlight(picked.tileId);
    redrawTileSelector();
}

// Right drag: a rectangle of the active layer becomes the brush (blank where the rectangle leaves the sheet).
void TilesetEditor::pickRegionFromSheet(const QRect &tiles) {
    auto *sheet = this->metatileSelector;
    const int layer = sheet->activeLayer();
    const TileBrush picked = TileBrush::fromGrid(tiles, [&](int x, int y) {
        uint16_t metatileId; int sub;
        if (!sheet->tileSlot(QPoint(x, y), &metatileId, &sub))
            return Tile();
        const Metatile *metatile = block(metatileId);
        return metatile ? metatile->tiles.value(layer * Metatile::tilesPerLayer() + sub) : Tile();
    });
    this->tileSelector->setPicked(picked);
}

void TilesetEditor::showSlotStatus(uint16_t metatileId, int subIndex) {
    const Metatile *metatile = block(metatileId);
    const int layer = this->metatileSelector->activeLayer();
    if (!metatile)
        return;
    const Tile tile = metatile->tiles.value(layer * Metatile::tilesPerLayer() + subIndex);
    QString message = QString("%1 %2, %3 layer, tile %4: Tile %5, Palette %6%7%8")
                          .arg(blockName())
                          .arg(Metatile::getMetatileIdString(metatileId))
                          .arg(kind() == BlockKind::Porytile ? QStringLiteral("single") : Metatile::getLayerName(layer))
                          .arg(subIndex + 1)
                          .arg(Util::toHexString(tile.tileId, 3))
                          .arg(tile.palette)
                          .arg(tile.xflip ? ", X-flipped" : "")
                          .arg(tile.yflip ? ", Y-flipped" : "");
    const QString label = kind() == BlockKind::Metatile ? Tileset::getMetatileLabel(metatileId, this->primaryTileset, this->secondaryTileset) : ownedLabel(metatileId);
    if (!label.isEmpty())
        message += QString(" \"%1\"").arg(label);
    this->ui->statusbar->showMessage(message);
}

void TilesetEditor::setActiveLayer(int layer) {
    if (layer != this->metatileSelector->activeLayer())
        this->metatileSelector->clearTileSelection();   // (marked tiles belong to the layer they were marked on)
    this->metatileSelector->setActiveLayer(layer);
    if (layer >= 0 && layer < 3 && this->layerSelectButtons[layer] && !this->layerSelectButtons[layer]->isChecked())
        this->layerSelectButtons[layer]->setChecked(true);
    this->ui->statusbar->showMessage(QString("Active layer: %1").arg(Metatile::getLayerName(layer)), 3000);
}

void TilesetEditor::initTileSelector() {
    this->tileSelector = new TilesetEditorTileSelector(this->primaryTileset, this->secondaryTileset);
    connect(this->tileSelector, &TilesetEditorTileSelector::hoveredTileChanged, [this](uint16_t tileId) {
        showTileStatus(tileId);
    });
    connect(this->tileSelector, &TilesetEditorTileSelector::hoveredTileCleared, this, &TilesetEditor::onHoveredTileCleared);
    connect(this->tileSelector, &TilesetEditorTileSelector::selectedTilesChanged, this, &TilesetEditor::drawSelectedTiles);
    // A brush picked from a sheet is taken exactly as it is: the flip boxes go back to unticked (without re-flipping it).
    connect(this->tileSelector, &TilesetEditorTileSelector::flipsReset, this, [this] {
        const QSignalBlocker blockX(this->ui->checkBox_xFlip), blockY(this->ui->checkBox_yFlip);
        this->ui->checkBox_xFlip->setChecked(false);
        this->ui->checkBox_yFlip->setChecked(false);
    });
    // The source sheet takes selections up to its own size: 16 tiles wide (= 8 metatile fields), as tall as the sheet is.
    this->tileSelector->setMaxSelectionSize(TileBrush::kMaxCols, Project::getNumTilesTotal() / TileBrush::kMaxCols);
    this->metatileSelector->setMaxPickRows(Project::getNumTilesTotal() / TileBrush::kMaxCols);

    this->tileSelector->showDivider = this->ui->actionShow_Tileset_Divider->isChecked();

    auto scene = new QGraphicsScene(this);
    scene->addItem(this->tileSelector);
    this->tileSelector->select(0);
    this->tileSelector->draw();

    this->ui->graphicsView_Tiles->setScene(scene);
    this->ui->graphicsView_Tiles->setResizeAnchor(QGraphicsView::AnchorViewCenter);
    this->ui->horizontalSlider_TilesZoom->setValue(porymapConfig.tilesetEditorTilesZoom);
}

void TilesetEditor::initSelectedTileItem() {
    ui->graphicsView_selectedTile->setScene(new QGraphicsScene(this));
    this->drawSelectedTiles();
}

void TilesetEditor::initShortcuts() {
    initExtraShortcuts();

    shortcutsConfig.load();
    shortcutsConfig.setDefaultShortcuts(shortcutableObjects());
    applyUserShortcuts();
}

void TilesetEditor::initExtraShortcuts() {
    ui->actionRedo->setShortcuts({ui->actionRedo->shortcut(), QKeySequence("Ctrl+Shift+Z")});

    auto *shortcut_xFlip = new Shortcut(QKeySequence(), ui->checkBox_xFlip, SLOT(toggle()));
    shortcut_xFlip->setObjectName("shortcut_xFlip");
    shortcut_xFlip->setWhatsThis("X Flip");

    auto *shortcut_yFlip = new Shortcut(QKeySequence(), ui->checkBox_yFlip, SLOT(toggle()));
    shortcut_yFlip->setObjectName("shortcut_yFlip");
    shortcut_yFlip->setWhatsThis("Y Flip");
}

QObjectList TilesetEditor::shortcutableObjects() const {
    QObjectList shortcutable_objects;

    for (auto *action : findChildren<QAction *>())
        if (!action->objectName().isEmpty())
            shortcutable_objects.append(qobject_cast<QObject *>(action));
    for (auto *shortcut : findChildren<Shortcut *>())
        if (!shortcut->objectName().isEmpty())
            shortcutable_objects.append(qobject_cast<QObject *>(shortcut));

    return shortcutable_objects;
}

void TilesetEditor::applyUserShortcuts() {
    for (auto *action : findChildren<QAction *>())
        if (!action->objectName().isEmpty())
            action->setShortcuts(shortcutsConfig.userShortcuts(action));
    for (auto *shortcut : findChildren<Shortcut *>())
        if (!shortcut->objectName().isEmpty())
            shortcut->setKeys(shortcutsConfig.userShortcuts(shortcut));
}

void TilesetEditor::restoreWindowState() {
    logInfo("Restoring tileset editor geometry from previous session.");
    QMap<QString, QByteArray> geometry = porymapConfig.getTilesetEditorGeometry();
    this->restoreGeometry(geometry.value("tileset_editor_geometry"));
    this->restoreState(geometry.value("tileset_editor_state"));
    this->ui->splitter->restoreState(geometry.value("tileset_editor_splitter_state"));
}

void TilesetEditor::onWindowActivated() {
    // User may have made layout edits since window was last focused, so update counts
    if (this->metatileSelector) {
        if (this->metatileSelector->selectorShowUnused || this->metatileSelector->selectorShowCounts) {
            countMetatileUsage();
            this->metatileSelector->draw();
        }
    }
}

void TilesetEditor::reset() {
    this->setTilesets(this->primaryTileset->name, this->secondaryTileset->name);
    if (this->paletteEditor)
        this->paletteEditor->setTilesets(this->primaryTileset, this->secondaryTileset);
    this->refresh();
}

void TilesetEditor::refresh() {
    this->tileSelector->setTilesets(this->primaryTileset, this->secondaryTileset);
    this->metatileSelector->setTilesets(this->primaryTileset, this->secondaryTileset);
    if (this->behaviorSelector) {
        this->behaviorSelector->setTilesets(this->primaryTileset, this->secondaryTileset);
        refreshBehaviorFilterChoices();
        onBehaviorSelectionChanged();
    }
    uint16_t selectedId = selectedBlockId();
    if (!blockIsValid(selectedId))
        selectedId = this->primaryTileset->firstMetatileId();   // (the new tilesets are smaller than the old selection)
    this->metatileSelector->select(selectedId);

    if (metatileSelector) {
        if (metatileSelector->selectorShowUnused || metatileSelector->selectorShowCounts) {
            countMetatileUsage();
            this->metatileSelector->draw();
        }
    }

    if (tileSelector) {
        if (tileSelector->showUnused) {
            countTileUsage();
            this->tileSelector->draw();
        }
    }

    this->redrawTileSelector();
    this->redrawMetatileSelector();
    this->drawSelectedTiles();
}

// The "Selection" preview: the brush as it will be stamped (flips and palette applied). It always fits its fixed frame (a brush of
// 16 x 64 tiles is shrunk, never the window grown), and it says how big the brush is.
void TilesetEditor::drawSelectedTiles() {
    QGraphicsScene *scene = ui->graphicsView_selectedTile->scene();
    if (!scene) {
        return;
    }

    scene->clear();
    const TileBrush brush = this->tileSelector->brush();
    const QImage selectionImage = getBrushImage(brush, this->primaryTileset, this->secondaryTileset, 16);
    auto selectedTilePixmapItem = new QGraphicsPixmapItem(QPixmap::fromImage(selectionImage));
    scene->addItem(selectedTilePixmapItem);
    if (!selectionImage.isNull()) {   // (a blank brush would show nothing at all)
        QPen outline(QColor(140, 140, 140));
        outline.setCosmetic(true);
        scene->addRect(QRectF(0, 0, selectionImage.width(), selectionImage.height()), outline);
    }

    QSize size(selectionImage.width(), selectionImage.height());
    this->ui->graphicsView_selectedTile->setSceneRect(0, 0, size.width(), size.height());
    const QSize frame = this->ui->graphicsView_selectedTile->viewport()->size();
    const double fit = size.isEmpty() ? 1.0 : qMin(1.0, qMin(static_cast<double>(frame.width()) / size.width(), static_cast<double>(frame.height()) / size.height()));
    this->ui->graphicsView_selectedTile->setTransform(QTransform::fromScale(fit, fit));
    this->ui->label_SelectionSize->setText(brush.isNull() ? QStringLiteral("Selection") : QString("Selection %1x%2").arg(brush.cols()).arg(brush.rows()));
    updateBrushGhost();
}

void TilesetEditor::updateMetatileStatus() {
    if (this->metatileSelector->hasCursor()) {
        showMetatileStatus(this->metatileSelector->metatileIdUnderCursor());
    }
}

void TilesetEditor::showMetatileStatus(uint16_t metatileId) {
    QString label = kind() == BlockKind::Metatile ? Tileset::getMetatileLabel(metatileId, this->primaryTileset, this->secondaryTileset) : ownedLabel(metatileId);
    QString message = QString("%1: %2").arg(blockName(), Metatile::getMetatileIdString(metatileId));
    if (label.size() != 0) {
        message += QString(" \"%1\"").arg(label);
    }
    if (metatileId == 0)
        message += QStringLiteral("  (the erase entry: always empty, never edited)");
    this->ui->statusbar->showMessage(message);
}

void TilesetEditor::onHoveredMetatileCleared() {
    this->ui->statusbar->clearMessage();
}

void TilesetEditor::onSelectedMetatileChanged(uint16_t metatileId) {
    this->metatile = block(metatileId);
    if (!this->metatile) return;

    // The scripting API allows users to change metatiles in the project, and these changes are saved to disk.
    // The Tileset Editor (if open) needs to reflect these changes when the metatile is next displayed.
    if (kind() == BlockKind::Metatile && this->metatileReloadQueue.contains(metatileId)) {
        this->metatileReloadQueue.remove(metatileId);
        Metatile *updatedMetatile = Tileset::getMetatile(metatileId, this->layout->tileset_primary, this->layout->tileset_secondary);
        if (updatedMetatile) *this->metatile = *updatedMetatile;
    }

    MetatileLabelPair labels;
    if (kind() == BlockKind::Metatile)
        labels = Tileset::getMetatileLabelPair(metatileId, this->primaryTileset, this->secondaryTileset);
    else
        labels.owned = ownedLabel(metatileId);
    this->ui->lineEdit_MetatileLabel->setText(labels.owned);
    this->ui->lineEdit_MetatileLabel->setPlaceholderText(labels.shared);

    refreshMetatileAttributes();
}

void TilesetEditor::queueMetatileReload(uint16_t metatileId) {
    this->metatileReloadQueue.insert(metatileId);
}

void TilesetEditor::showTileStatus(uint16_t tileId) {
    this->ui->statusbar->showMessage(QString("Tile: %1").arg(Util::toHexString(tileId, 3)));
}

void TilesetEditor::onHoveredTileCleared() {
    this->ui->statusbar->clearMessage();
}

void TilesetEditor::setPaletteId(int paletteId) {
    ui->spinBox_paletteSelector->setValue(paletteId);
}

int TilesetEditor::paletteId() const {
    return ui->spinBox_paletteSelector->value();
}

void TilesetEditor::refreshPaletteId() {
    this->tileSelector->setPaletteId(paletteId());
    this->drawSelectedTiles();
    if (this->paletteEditor) {
        this->paletteEditor->setPaletteId(paletteId());
    }
}

void TilesetEditor::refreshTileFlips() {
    this->tileSelector->setTileFlips(ui->checkBox_xFlip->isChecked(), ui->checkBox_yFlip->isChecked());
    this->drawSelectedTiles();
}

void TilesetEditor::setMetatileLabel(QString label)
{
    this->ui->lineEdit_MetatileLabel->setText(label);
    commitMetatileLabel();
}

// Whether another metatile of these tilesets already has this label (the labels of the tilesets must be unique: they become #defines).
bool TilesetEditor::labelBelongsToAnotherMetatile(const QString &label, uint16_t ownId) const {
    if (label.isEmpty())
        return false;
    for (const Tileset *tileset : { this->primaryTileset, this->secondaryTileset })
        for (auto it = tileset->blockLabels(kind()).constBegin(); it != tileset->blockLabels(kind()).constEnd(); ++it)
            if (it.value() == label && it.key() != ownId)
                return true;
    return false;
}

void TilesetEditor::commitMetatileLabel() {
    if (!this->metatile) return;

    // Only commit if the field has changed.
    uint16_t metatileId = selectedBlockId();
    QString oldLabel = ownedLabel(metatileId);
    QString newLabel = this->ui->lineEdit_MetatileLabel->text();
    if (metatileId == 0 && oldLabel != newLabel) {   // (the erase entry is never named)
        this->ui->lineEdit_MetatileLabel->setText(oldLabel);
        refuseEntryZero();
        return;
    }
    if (oldLabel != newLabel && labelBelongsToAnotherMetatile(newLabel, metatileId)) {
        this->ui->statusbar->showMessage(QString("Nothing changed: the label \"%1\" belongs to another metatile.").arg(newLabel), 8000);
        this->ui->lineEdit_MetatileLabel->setText(oldLabel);
        return;
    }
    if (oldLabel != newLabel) {
        Metatile *prevMetatile = new Metatile(*this->metatile);
        setLabel(metatileId, newLabel);
        this->commitMetatileAndLabelChange(prevMetatile, oldLabel, MetatileHistoryItem::Stack::Behavior);   // (a label is edited on the Behavior page)
    }
}

void TilesetEditor::commitMetatileAndLabelChange(Metatile * prevMetatile, QString prevLabel, MetatileHistoryItem::Stack stack) {
    if (!this->metatile) return;

    auto *step = new MetatileHistoryItem(selectedBlockId(),
                                         prevMetatile, new Metatile(*this->metatile),
                                         prevLabel, this->ui->lineEdit_MetatileLabel->text());
    step->stack = stack;
    commit(step);
    if (this->behaviorSelector && this->behaviorSelector->fieldFilterActive())
        drawOnSheets({ selectedBlockId() });   // (the view filter of the Behavior page may depend on the label)
    else if (onBehaviorPage())
        refreshBehaviorFilterChoices();                    // (the counts in its "Show:" box do)
}

void TilesetEditor::commitMetatileChange(Metatile * prevMetatile)
{
    this->commitMetatileAndLabelChange(prevMetatile, this->ui->lineEdit_MetatileLabel->text());
}

void TilesetEditor::refreshMetatileAttributes() {
    if (!this->metatile) return;
    this->metatileSelector->drawSelectedMetatile();   // (the layer type can change how a metatile with nothing in it looks)
}

bool TilesetEditor::isDirty() const {
    return tilesetDirty();
}

// A fingerprint of what the two tilesets hold as far as the Tileset Editor edits it through its histories: every metatile's tiles and packed
// attributes, and the metatile labels (in id order).
QByteArray TilesetEditor::contentFingerprint() const {
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const Tileset *tileset : { this->primaryTileset, this->secondaryTileset }) {
        if (!tileset)
            continue;
        for (BlockKind blockKind : { BlockKind::Metatile, BlockKind::Porytile }) {
            for (const Metatile *metatile : tileset->blocks(blockKind)) {
                for (const Tile &tile : metatile->tiles) {
                    const uint16_t raw = tile.rawValue();
                    hash.addData(QByteArrayView(reinterpret_cast<const char *>(&raw), sizeof(raw)));
                }
                const uint32_t attributes = metatile->getAttributes();
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(&attributes), sizeof(attributes)));
            }
            const QHash<int, QString> &labels = tileset->blockLabels(blockKind);
            QList<int> ids = labels.keys();
            std::sort(ids.begin(), ids.end());
            for (int id : ids) {
                const QString &label = labels.value(id);
                if (label.isEmpty())
                    continue;   // (no label is the same as an empty one)
                hash.addData(QByteArrayView(reinterpret_cast<const char *>(&id), sizeof(id)));
                hash.addData(label.toUtf8());
            }
            hash.addData(QByteArrayView("|", 1));
        }
    }
    return hash.result();
}

// true: something changed that no history step covers. false: everything is saved (what the tilesets hold now is what counts as saved).
void TilesetEditor::setTilesetDirty(bool dirty) {
    this->extraDirty = dirty;
    if (!dirty) {
        this->paintHistory.save();
        this->behaviorHistory.save();
        this->porytilePaintHistory.save();
        this->porytileBehaviorHistory.save();
        this->savedFingerprint = contentFingerprint();
    }
    updateWindowTitle();
}

// "* Tileset Editor" while anything is unsaved (Ctrl+S / File > Save Tileset saves it).
void TilesetEditor::updateWindowTitle() {
    setWindowTitle((isDirty() ? QStringLiteral("* ") : QString()) + this->baseWindowTitle);
}

QString TilesetEditor::unsavedSummary() const {
    return tilesetDirty() ? QStringLiteral("Unsaved: tileset data.") : QString();
}

// Ctrl+S / the Save button.
bool TilesetEditor::save() {
    if (this->strokeOpen)
        return false;   // (a stroke is not a history step yet: saving now would mark the wrong state as saved)
    setFocus();         // a label or a number that is still being typed is committed first, like when the window is closed
    const bool success = saveTilesetData();
    updateWindowTitle();
    return success;
}

bool TilesetEditor::saveTilesetData() {
    // CUSTOM ENGINE: a Write / Pull (or an undo of one) changed the project's tilesets after these copies were taken. Writing
    // them now would put the old blocks back over it -- and a map that already refers to the new metatiles would lose them.
    if (this->project && this->tilesetGeneration != this->project->tilesetTransferGeneration) {
        WarningMessage::show(QStringLiteral("These tilesets changed outside the Tileset Editor (Write to Finalmap / Pull to Porymap)."),
                             QStringLiteral("This window still holds the older copy, so saving it would undo that change. Close this window (discarding what is unsaved here) and open it again."), this);
        this->ui->statusbar->showMessage(QStringLiteral("Not saved: these tilesets changed outside this window. Close it and open it again."), 10000);
        return false;
    }
    // Need this temporary flag to stop selection resetting after saving.
    // This is a workaround; redrawing the map's metatile selector shouldn't emit the same signal as when it's selected.
    this->lockSelection = true;

    bool success = this->project->saveTilesets(this->primaryTileset, this->secondaryTileset);
    if (success)
        applyMetatileSwapsToLayouts(); // (a failed save keeps the swaps pending: they belong to the tileset data that was not written)
    emit this->tilesetsSaved(this->primaryTileset->name, this->secondaryTileset->name);
    if (this->paletteEditor) {
        this->paletteEditor->setTilesets(this->primaryTileset, this->secondaryTileset);
    }
    this->ui->statusbar->showMessage(success ? QStringLiteral("Saved primary and secondary Tilesets!")
                                             : QStringLiteral("Failed to save tilesets! See log for details."), 5000);
    if (success) {
        setTilesetDirty(false);
    }
    this->lockSelection = false;
    return success;
}

void TilesetEditor::importTilesetTiles(Tileset *tileset) {
    bool primary = !tileset->is_secondary;
    QString descriptor = primary ? "primary" : "secondary";
    QString descriptorCaps = primary ? "Primary" : "Secondary";

    QString filepath = FileDialog::getOpenFileName(this, QString("Import %1 Tileset Tiles Image").arg(descriptorCaps), "", "Image Files (*.png *.bmp *.jpg *.dib)");
    if (filepath.isEmpty()) {
        return;
    }

    logInfo(QString("Importing %1 tileset tiles '%2'").arg(descriptor).arg(filepath));

    // Read image data from buffer so that the built-in QImage doesn't try to detect file format
    // purely from the extension name. Advance Map exports ".png" files that are actually BMP format, for example.
    QFile file(filepath);
    QImage image;
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray imageData = file.readAll();
        image = QImage::fromData(imageData);
    } else {
        logError(QString("Failed to open image file: '%1'").arg(filepath));
    }
    if (image.width() == 0 || image.height() == 0 || image.width() % Tile::pixelWidth() != 0 || image.height() % Tile::pixelHeight() != 0) {
        ErrorMessage::show(QStringLiteral("Failed to import tiles."),
                           QString("The image dimensions (%1x%2) are invalid. The dimensions must be a multiple of %3x%4 pixels.")
                                  .arg(image.width())
                                  .arg(image.height())
                                  .arg(Tile::pixelWidth())
                                  .arg(Tile::pixelHeight()),
                            this);
        return;
    }

    // Validate total number of tiles in image.
    int numTilesWide = image.width() / Tile::pixelWidth();
    int numTilesHigh = image.height() / Tile::pixelHeight();
    int totalTiles = numTilesHigh * numTilesWide;
    int maxAllowedTiles = primary ? Project::getNumTilesPrimary() : Project::getNumTilesSecondary();
    if (totalTiles > maxAllowedTiles) {
        ErrorMessage::show(QStringLiteral("Failed to import tiles."),
                           QString("The maximum number of tiles allowed in the %1 tileset is %2, but the provided image contains %3 total tiles.")
                                  .arg(descriptor)
                                  .arg(maxAllowedTiles)
                                  .arg(totalTiles),
                           this);
        return;
    }

    // Ask user to provide a palette for the un-indexed image.
    if (image.colorCount() == 0) {
        auto msgBox = new QuestionMessage(QStringLiteral("Select a palette file for this image?"), this);
        msgBox->setAttribute(Qt::WA_DeleteOnClose);
        msgBox->setInformativeText(QStringLiteral("The provided image is not indexed. "
                                                  "An indexed image will be generated using the provided image and palette."));
        if (msgBox->exec() != QMessageBox::Yes)
            return;

        QString filepath = FileDialog::getOpenFileName(this, "Select Palette for Tiles Image", "", "Palette Files (*.pal *.act *tpl *gpl)");
        if (filepath.isEmpty()) {
            return;
        }

        bool error = false;
        QList<QRgb> palette = PaletteUtil::parse(filepath, &error);
        if (error) {
            RecentErrorMessage::show(QStringLiteral("Failed to import palette."), this);
            return;
        }

        QVector<QRgb> colorTable = palette.toVector();
        image = image.convertToFormat(QImage::Format::Format_Indexed8, colorTable);
    }

    if (!tileset->loadTilesImage(&image)) {
        RecentErrorMessage::show(QStringLiteral("Failed to import tiles."), this);
        return;
    }
    this->refresh();
    setTilesetDirty(true);
}

void TilesetEditor::closeEvent(QCloseEvent *event)
{
    // If focus is still on any input widgets, a user may have made changes
    // but the widget hasn't had a chance to fire the 'editingFinished' signal.
    // Make sure they lose focus before we close so that changes aren't missed.
    setFocus();

    if (isDirty()) {
        auto result = SaveChangesMessage::show(QStringLiteral("The Tileset Editor"), true, this, unsavedSummary());
        if (result == QMessageBox::Yes) {
            if (this->save()) {
                event->accept();
            } else {
                event->ignore();
            }
        } else if (result == QMessageBox::No) {
            if (tilesetDirty())
                this->reset();
            event->accept();
        } else if (result == QMessageBox::Cancel) {
            event->ignore();
        }
    } else {
        event->accept();
    }

    if (event->isAccepted()) {
        if (this->paletteEditor) this->paletteEditor->close();
        porymapConfig.setTilesetEditorGeometry(
            this->saveGeometry(),
            this->saveState(),
            this->ui->splitter->saveState()
        );
    }
}

void TilesetEditor::on_actionChange_Metatiles_Count_triggered()
{
    QDialog dialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    dialog.setWindowTitle(QString("Change Number of %1s").arg(blockName()));
    dialog.setWindowModality(Qt::WindowModal);

    QFormLayout form(&dialog);

    QSpinBox *primarySpinBox = new QSpinBox();
    QSpinBox *secondarySpinBox = new QSpinBox();
    primarySpinBox->setMinimum(1);
    secondarySpinBox->setMinimum(1);
    primarySpinBox->setMaximum(Project::getNumMetatilesPrimary());
    secondarySpinBox->setMaximum(Project::getNumMetatilesSecondary());
    primarySpinBox->setValue(this->primaryTileset->numBlocks(kind()));
    secondarySpinBox->setValue(this->secondaryTileset->numBlocks(kind()));
    form.addRow(new QLabel("Primary Tileset"), primarySpinBox);
    form.addRow(new QLabel("Secondary Tileset"), secondarySpinBox);

    QDialogButtonBox buttonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    connect(&buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(&buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form.addRow(&buttonBox);

    if (dialog.exec() == QDialog::Accepted) {
        this->primaryTileset->resizeBlocks(kind(), primarySpinBox->value());
        this->secondaryTileset->resizeBlocks(kind(), secondarySpinBox->value());

        // Our selected block ID may have become invalid. Make sure it's in-bounds.
        uint16_t metatileId = this->metatileSelector->getSelectedMetatileId();
        Tileset *tileset = Tileset::getMetatileTileset(metatileId, this->primaryTileset, this->secondaryTileset);
        if (tileset && !tileset->containsBlockId(kind(), metatileId)) {
            this->metatileSelector->select(qBound(tileset->firstMetatileId(), metatileId, static_cast<uint16_t>(tileset->firstMetatileId() + qMax(1, tileset->numBlocks(kind())) - 1)));
        }

        refresh();
        setTilesetDirty(true);
    }
}

void TilesetEditor::on_actionChange_Palettes_triggered()
{
    if (!this->paletteEditor) {
        this->paletteEditor = new PaletteEditor(this->project, this->primaryTileset,
                                                this->secondaryTileset, this->paletteId(), this);
        connect(this->paletteEditor, &PaletteEditor::changedPaletteColor, this, &TilesetEditor::onPaletteEditorChangedPaletteColor);
        connect(this->paletteEditor, &PaletteEditor::changedPalette, this, &TilesetEditor::setPaletteId);
        connect(this->paletteEditor, &PaletteEditor::metatileSelected, this, &TilesetEditor::selectMetatile);
    }
    Util::show(this->paletteEditor);
}

void TilesetEditor::onPaletteEditorChangedPaletteColor() {
    this->refresh();
    setTilesetDirty(true);
}

// Puts `src` (and its label) into metatile `metatileId`. select: make it the selected metatile (the editing paths); the undo/redo of
// a step that changes several metatiles passes false so the selection does not jump from one to the next.
bool TilesetEditor::replaceMetatile(uint16_t metatileId, const Metatile &src, QString newLabel, bool select, bool redraw) {
    Metatile * dest = block(metatileId);
    QString oldLabel = ownedLabel(metatileId);
    if (!dest || (*dest == src && oldLabel == newLabel))
        return false;
    if (metatileId == 0) {   // (paste, cut and swap all end up here: the erase entry never takes another block)
        refuseEntryZero();
        return false;
    }

    setLabel(metatileId, newLabel);
    if (metatileId == selectedBlockId())
        this->ui->lineEdit_MetatileLabel->setText(newLabel);

    // Update tile usage if any tiles changed
    if (this->tileSelector && this->tileSelector->showUnused) {
        const int numTiles = qMin(src.tiles.size(), dest->tiles.size());
        for (int i = 0; i < numTiles; i++) {
            if (src.tiles[i].tileId != dest->tiles[i].tileId) {
                this->tileSelector->usedTiles[src.tiles[i].tileId] += 1;
                this->tileSelector->usedTiles[dest->tiles[i].tileId] -= 1;
            }
        }
        if (redraw)
            this->tileSelector->draw();
    }

    *dest = src;
    if (select) {
        this->metatile = dest;
        this->metatileSelector->select(metatileId);
    }
    if (redraw)
        drawOnSheets({ metatileId });
    if (redraw && (select || metatileId == selectedBlockId())) {
        this->metatile = block(selectedBlockId());
        refreshMetatileAttributes();
        updateMetatileStatus();
    }
    return true;
}

void TilesetEditor::initMetatileHistory() {
    this->strokeOpen = false;
    this->strokeEdits.clear();
    this->paintHistory.clear();
    this->behaviorHistory.clear();
    this->porytilePaintHistory.clear();
    this->porytileBehaviorHistory.clear();
    this->metatileIdSwaps.clear(); // pending layout swaps belong to the edits that were just dropped
    updateEditHistoryActions();
    setTilesetDirty(false);
}

void TilesetEditor::commit(MetatileHistoryItem *item) {
    item->kind = kind();   // (every step is made for the kind of block the editor shows)
    historyFor(item->kind, item->stack).push(item);
    updateEditHistoryActions();
    updateWindowTitle();
}

// Undo / Redo (Edit menu, Cmd/Ctrl+Z) work on the history of the page that is showing: on the Paint page they only take back what was painted
// there, on the Behavior page only what was done there (behaviors, labels). The menu says which one it is.
void TilesetEditor::updateEditHistoryActions() {
    const History<MetatileHistoryItem*> &history = currentHistory();   // (the kind AND the page that are showing)
    const QString page = onBehaviorPage() ? QStringLiteral("Behavior") : QStringLiteral("Paint");
    // (while the mouse is down in a stroke, Undo/Redo would fight with the step that is being recorded)
    ui->actionUndo->setEnabled(history.canUndo() && !this->strokeOpen);
    ui->actionRedo->setEnabled(history.canRedo() && !this->strokeOpen);
    ui->actionUndo->setText(QString("Undo %1").arg(page));
    ui->actionRedo->setText(QString("Redo %1").arg(page));
}

// Undoes or redoes ONE edit of a step, and puts back only the parts that the step changed (the tiles, each attribute, the label). The two histories
// own different parts of the same metatile (painting: tiles and layer type; the Behavior page: behavior and label), so an Undo on one page must not
// overwrite what was done on the other page in between.
bool TilesetEditor::applyEditDelta(const MetatileEdit &edit, bool undo) {
    Metatile *dest = block(edit.id);
    if (!dest)
        return false;
    const Metatile &from = undo ? edit.next : edit.prev;
    const Metatile &to = undo ? edit.prev : edit.next;
    Metatile merged = *dest;
    if (from.tiles != to.tiles)
        merged.tiles = to.tiles;
    for (Metatile::Attr attr : { Metatile::Attr::Behavior, Metatile::Attr::TerrainType, Metatile::Attr::EncounterType, Metatile::Attr::LayerType, Metatile::Attr::Unused })
        if (from.getAttribute(attr) != to.getAttribute(attr))
            merged.setAttribute(attr, to.getAttribute(attr));
    QString label = ownedLabel(edit.id);
    if ((undo ? edit.nextLabel : edit.prevLabel) != (undo ? edit.prevLabel : edit.nextLabel))
        label = undo ? edit.prevLabel : edit.nextLabel;
    return replaceMetatile(edit.id, merged, label, false, false);
}

// Whether a step can be undone (or redone) right now: every part of a metatile that the step changed (its tiles, each attribute, its label) must still
// hold what the step left there (for an Undo) or found there (for a Redo). Within one history that is always so; it is not when the OTHER page changed
// the same part in between (e.g. Clear Field on the Paint page, then a behavior on the Behavior page, then Undo on the Paint page). Such a step is
// refused instead of overwriting the newer change, and the histories stay consistent.
bool TilesetEditor::stepFits(const MetatileHistoryItem *item, bool undo, uint16_t *conflictingId) const {
    for (const MetatileEdit &edit : item->edits) {
        const Metatile *current = block(edit.id);
        if (!current)
            continue;
        const Metatile &expected = undo ? edit.next : edit.prev;
        const Metatile &other = undo ? edit.prev : edit.next;
        bool fits = expected.tiles == other.tiles || current->tiles == expected.tiles;
        for (Metatile::Attr attr : { Metatile::Attr::Behavior, Metatile::Attr::TerrainType, Metatile::Attr::EncounterType, Metatile::Attr::LayerType, Metatile::Attr::Unused })
            if (expected.getAttribute(attr) != other.getAttribute(attr) && current->getAttribute(attr) != expected.getAttribute(attr))
                fits = false;
        const QString expectedLabel = undo ? edit.nextLabel : edit.prevLabel, otherLabel = undo ? edit.prevLabel : edit.nextLabel;
        if (expectedLabel != otherLabel && ownedLabel(edit.id) != expectedLabel)
            fits = false;
        if (expectedLabel != otherLabel && labelBelongsToAnotherMetatile(otherLabel, edit.id))   // (the label it would write went to another block meanwhile)
            fits = false;
        if (!fits) {
            if (conflictingId)
                *conflictingId = edit.id;
            return false;
        }
    }
    return true;
}

// Runs one step backwards (undo) or forwards (redo). A step with several edits undoes them in reverse order; the selection ends on
// the first edited metatile, which is scrolled into view.
void TilesetEditor::applyHistoryItem(const MetatileHistoryItem *item, bool undo) {
    const int n = item->edits.size();
    if (item->isSwap) {   // (the layouts follow the swap, or its undo, on Save)
        this->metatileSelector->clearSwapSelection();
        recordLayoutSwap(undo ? item->swapMetatileId : item->metatileId, undo ? item->metatileId : item->swapMetatileId);
    }
    QSet<uint16_t> touched;
    for (int i = 0; i < n; i++) {
        const MetatileEdit &edit = item->edits.at(undo ? n - 1 - i : i);
        if (applyEditDelta(edit, undo))
            touched.insert(edit.id);
    }
    // (the pictures are redrawn ONCE for the whole step: a step may hold hundreds of metatiles)
    drawOnSheets(touched);
    if (this->tileSelector->showUnused)
        this->tileSelector->draw();
    if (touched.contains(selectedBlockId())) {
        this->metatile = block(selectedBlockId());
        refreshMetatileAttributes();
        updateMetatileStatus();
    }
    if (n > 0) {
        const uint16_t first = item->edits.first().id;
        this->metatileSelector->select(first);
        ensureMetatileVisible(first);
    }
    if (onBehaviorPage()) {
        // The fields the step changed become the selection, so it is plain to see what an Undo / Redo did (also for a step over many fields), and the
        // panel shows what those fields have now.
        QSet<uint16_t> ids;
        for (const MetatileEdit &edit : item->edits)
            ids.insert(edit.id);
        if (!ids.isEmpty())
            this->behaviorSelector->setSelectedCells(ids);
        onBehaviorSelectionChanged();
    }
}

void TilesetEditor::on_actionUndo_triggered() {
    History<MetatileHistoryItem*> &history = currentHistory();
    MetatileHistoryItem *commit = history.current();
    if (!commit) return;
    uint16_t conflict = 0;
    if (!stepFits(commit, true, &conflict)) {
        this->ui->statusbar->showMessage(QString("Cannot undo this step: metatile %1 was changed on the %2 page after it. Undo that change first.")
                                             .arg(Metatile::getMetatileIdString(conflict), onBehaviorPage() ? QStringLiteral("Paint") : QStringLiteral("Behavior")), 8000);
        return;
    }
    history.back();
    applyHistoryItem(commit, true);
    updateEditHistoryActions();
    updateWindowTitle();
}

void TilesetEditor::on_actionRedo_triggered() {
    History<MetatileHistoryItem*> &history = currentHistory();
    if (!history.canRedo()) return;
    uint16_t conflict = 0;
    if (!stepFits(history.peekNext(), false, &conflict)) {
        this->ui->statusbar->showMessage(QString("Cannot redo this step: metatile %1 was changed on the %2 page since. Undo that change first.")
                                             .arg(Metatile::getMetatileIdString(conflict), onBehaviorPage() ? QStringLiteral("Paint") : QStringLiteral("Behavior")), 8000);
        return;
    }
    MetatileHistoryItem *commit = history.next();
    if (!commit) return;
    applyHistoryItem(commit, false);
    updateEditHistoryActions();
    updateWindowTitle();
}

void TilesetEditor::on_actionCut_triggered()
{
    if (this->strokeOpen)
        return;
    this->copyMetatile(true);
    this->pasteMetatile(blankMetatile(), "");
}

void TilesetEditor::on_actionCopy_triggered()
{
    this->copyMetatile(false);
}

void TilesetEditor::on_actionPaste_triggered()
{
    if (clipboardBlock() && !this->strokeOpen) {
        this->pasteMetatile(*clipboardBlock(), clipboardLabel());
    }
}

void TilesetEditor::copyMetatile(bool cut) {
    uint16_t metatileId = selectedBlockId();
    Metatile * toCopy = block(metatileId);
    if (!toCopy) return;

    Metatile *&clipboard = clipboardBlock();
    if (!clipboard)
        clipboard = new Metatile(*toCopy);
    else
        *clipboard = *toCopy;

    ui->actionPaste->setEnabled(true);

    // Don't try to copy the label unless it's a cut, these should be unique to each metatile.
    clipboardLabel() = cut ? ownedLabel(metatileId) : QString();
}

void TilesetEditor::pasteMetatile(const Metatile &toPaste, QString newLabel) {
    if (!this->metatile) return;

    Metatile *prevMetatile = new Metatile(*this->metatile);
    QString prevLabel = this->ui->lineEdit_MetatileLabel->text();
    if (newLabel.isNull()) newLabel = prevLabel; // Don't change the label if one wasn't copied
    uint16_t metatileId = selectedBlockId();
    QString droppedLabel;
    if (labelBelongsToAnotherMetatile(newLabel, metatileId)) {   // (a label that was cut and is pasted a second time: only one metatile can have it)
        droppedLabel = newLabel;
        newLabel = prevLabel;
    }
    if (!this->replaceMetatile(metatileId, toPaste, newLabel)) {
        delete prevMetatile;
        return;
    }

    this->commitMetatileAndLabelChange(prevMetatile, prevLabel);
    if (!droppedLabel.isEmpty())
        this->ui->statusbar->showMessage(QString("Pasted without the label \"%1\": it belongs to another metatile.").arg(droppedLabel), 8000);
}

void TilesetEditor::exportTilesImage(Tileset *tileset) {
    bool primary = !tileset->is_secondary;
    QString defaultFilepath = QString("%1/%2_Tiles_Pal%3.png").arg(FileDialog::getDirectory()).arg(tileset->name).arg(this->paletteId());
    QString filepath = FileDialog::getSaveFileName(this, QString("Export %1 Tiles Image").arg(primary ? "Primary" : "Secondary"), defaultFilepath, "Image Files (*.png)");
    if (!filepath.isEmpty()) {
        QImage image = primary ? this->tileSelector->buildPrimaryTilesIndexedImage() : this->tileSelector->buildSecondaryTilesIndexedImage();
        exportIndexed4BPPPng(image, filepath);
    }
}

// There are many more options for exporting metatile images than tile images, so we open a separate dialog to ask the user for settings.
void TilesetEditor::exportMetatilesImage() {
    if (!this->metatileImageExportSettings) {
        this->metatileImageExportSettings = new MetatileImageExporter::Settings;
    }
    auto dialog = new MetatileImageExporter(this, this->primaryTileset, this->secondaryTileset, this->metatileImageExportSettings);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void TilesetEditor::exportPorytilesLayerImages(Tileset *tileset) {
    QString dir = FileDialog::getExistingDirectory(this, QStringLiteral("Choose Folder to Export Images"));
    if (dir.isEmpty()) {
        return;
    }

    MetatileImageExporter layerExporter(this, this->primaryTileset, this->secondaryTileset);
    MetatileImageExporter::Settings settings = {};
    settings.usePrimaryTileset = !tileset->is_secondary;
    settings.useSecondaryTileset = tileset->is_secondary;

    QMap<QString,QImage> images;
    QStringList pathCollisions;
    for (int i = 0; i < 3; i++) {
        settings.layerOrder.clear();
        settings.layerOrder[i] = true;
        layerExporter.applySettings(settings);

        QString filename = layerExporter.getDefaultFileName();
        QString path = QString("%1/%2").arg(dir).arg(filename);
        if (QFileInfo::exists(path)) {
            pathCollisions.append(filename);
        }
        images[path] = layerExporter.getImage();
    }

    if (!pathCollisions.isEmpty()) {
        QString message = QString("The following files will be overwritten, are you sure you want to export?\n\n%1").arg(pathCollisions.join("\n"));
        auto reply = QuestionMessage::show(message, this);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }

    for (auto it = images.constBegin(); it != images.constEnd(); it++) {
        QString path = it.key();
        if (!it.value().save(path)) {
            logError(QString("Failed to save Porytiles layer image '%1'.").arg(path));
        }
    }
}

void TilesetEditor::importAdvanceMapMetatiles(Tileset *tileset) {
    ui->tabWidget_TilesetEditor->setCurrentWidget(ui->tab_GeneratedMetatiles);   // (an import of metatiles is a step of the metatile history)
    bool primary = !tileset->is_secondary;
    QString descriptorCaps = primary ? "Primary" : "Secondary";

    QString filepath = FileDialog::getOpenFileName(this, QString("Import %1 Tileset Metatiles from Advance Map 1.92").arg(descriptorCaps), "", "Advance Map 1.92 Metatile Files (*.bvd)");
    if (filepath.isEmpty()) {
        return;
    }

    bool error = false;
    QList<Metatile*> metatiles = AdvanceMapParser::parseMetatiles(filepath, &error, primary);
    if (error) {
        RecentErrorMessage::show(QStringLiteral("Failed to import metatiles from Advance Map 1.92 .bvd file."), this);
        qDeleteAll(metatiles);
        return;
    }

    // One history step for the whole import. The metatiles are overwritten IN PLACE (the tileset keeps its size: undo can restore every one of
    // them, and nothing that points into the tileset dangles); a file with more metatiles than the tileset has is cut off, said so below.
    const int available = tileset->numMetatiles();
    const int fileCount = metatiles.length();
    auto *step = new MetatileHistoryItem();
    uint16_t metatileIdBase = tileset->firstMetatileId();
    for (int i = 0; i < qMin(available, fileCount); i++) {
        MetatileEdit edit;
        edit.id = static_cast<uint16_t>(metatileIdBase + i);
        edit.prevLabel = edit.nextLabel = ownedLabel(edit.id);
        edit.prev = *tileset->metatileAt(i);
        edit.next = *metatiles.at(i);
        step->edits.append(edit);
        if (Metatile *target = block(edit.id))
            *target = edit.next;
    }
    qDeleteAll(metatiles);
    if (step->edits.isEmpty())
        delete step;
    else
        commit(step);
    this->ui->statusbar->showMessage(fileCount > available ? QString("Imported %1 metatiles; the file has %2 but this tileset has room for %1 (Edit > Change Number of Metatiles first to import them all).").arg(available).arg(fileCount)
                                                           : QString("Imported %1 metatiles.").arg(fileCount), 8000);
    this->refresh();
    return;
}

void TilesetEditor::on_actionShow_Unused_toggled(bool checked) {
    if (kind() != BlockKind::Metatile)
        return;   // (the usage counts are those of the metatiles in the maps)
    this->metatileSelector->selectorShowUnused = checked;

    if (checked) countMetatileUsage();

    this->metatileSelector->draw();
}

void TilesetEditor::on_actionShow_Counts_toggled(bool checked) {
    if (kind() != BlockKind::Metatile)
        return;
    this->metatileSelector->selectorShowCounts = checked;

    if (checked) countMetatileUsage();

    this->metatileSelector->draw();
}

void TilesetEditor::on_actionShow_UnusedTiles_toggled(bool checked) {
    this->tileSelector->showUnused = checked;

    if (checked) countTileUsage();

    this->tileSelector->draw();
}

void TilesetEditor::on_actionMetatile_Grid_triggered(bool checked) {
    if (this->behaviorSelector) {
        this->behaviorSelector->showGrid = checked;
        this->behaviorSelector->draw();
    }
    this->metatileSelector->showGrid = checked;
    this->metatileSelector->draw();
    porymapConfig.showTilesetEditorMetatileGrid = checked;
}

void TilesetEditor::on_actionShow_Tileset_Divider_triggered(bool checked) {
    if (this->behaviorSelector) {
        this->behaviorSelector->showDivider = checked;
        this->behaviorSelector->draw();
    }
    this->metatileSelector->showDivider = checked;
    this->metatileSelector->draw();

    this->tileSelector->showDivider = checked;
    this->tileSelector->draw();

    porymapConfig.showTilesetEditorDivider = checked;
    emit this->dividerShownChanged(checked);
}

void TilesetEditor::countMetatileUsage() {
    // do not double count
    this->metatileSelector->usedMetatiles.fill(0);

    for (const auto &layoutId : this->project->layoutIds()) {
        Layout *layout = this->project->getLayout(layoutId);
        bool usesPrimary = (layout->tileset_primary_label == this->primaryTileset->name);
        bool usesSecondary = (layout->tileset_secondary_label == this->secondaryTileset->name);

        if (usesPrimary || usesSecondary) {
            if (!this->project->loadLayout(layoutId))
                continue;

            // for each block in the layout, mark in the vector that it is used
            for (int i = 0; i < layout->blockdata.length(); i++) {
                uint16_t metatileId = layout->blockdata.at(i).metatileId();
                if (metatileId < this->project->getNumMetatilesPrimary()) {
                    if (usesPrimary) metatileSelector->usedMetatiles[metatileId]++;
                } else {
                    if (usesSecondary) metatileSelector->usedMetatiles[metatileId]++;
                }
            }

            for (int i = 0; i < layout->border.length(); i++) {
                uint16_t metatileId = layout->border.at(i).metatileId();
                if (metatileId < this->project->getNumMetatilesPrimary()) {
                    if (usesPrimary) metatileSelector->usedMetatiles[metatileId]++;
                } else {
                    if (usesSecondary) metatileSelector->usedMetatiles[metatileId]++;
                }
            }
        }
    }
}

void TilesetEditor::countTileUsage() {
    this->tileSelector->usedTiles.resize(Project::getNumTilesTotal());
    this->tileSelector->usedTiles.fill(0);

    auto countTilesetTileUsage = [this](Tileset *searchTileset) {
        // Count usage of our search tileset's tiles (in itself, and in any tilesets it gets paired with).
        QSet<QString> tilesetNames = this->project->getPairedTilesetLabels(searchTileset);
        QSet<Tileset*> tilesets;

        // For the currently-loaded tilesets, make sure we use the Tileset Editor's versions
        // (which may contain unsaved changes) and not the versions from the project.
        tilesetNames.remove(this->primaryTileset->name);
        tilesetNames.remove(this->secondaryTileset->name);
        tilesets.insert(this->primaryTileset);
        tilesets.insert(this->secondaryTileset);

        for (const auto &tilesetName : tilesetNames) {
            Tileset *tileset = this->project->getTileset(tilesetName);
            if (tileset) tilesets.insert(tileset);
        }
        for (const auto &tileset : tilesets) {
            for (const auto &metatile : tileset->metatiles()) {
                for (const auto &tile : metatile->tiles) {
                    if (searchTileset->containsTileId(tile.tileId)) {
                        this->tileSelector->usedTiles[tile.tileId]++;
                    }
                }
            }
        }
    };

    countTilesetTileUsage(this->primaryTileset);
    countTilesetTileUsage(this->secondaryTileset);
}

void TilesetEditor::on_copyButton_MetatileLabel_clicked() {
    uint16_t metatileId = selectedBlockId();
    QString label = Tileset::getMetatileLabel(metatileId, this->primaryTileset, this->secondaryTileset);
    if (label.isEmpty()) return;
    Tileset * tileset = Tileset::getMetatileLabelTileset(metatileId, this->primaryTileset, this->secondaryTileset);
    if (tileset)
        label.prepend(tileset->getMetatileLabelPrefix());
    QGuiApplication::clipboard()->setText(label);
    QToolTip::showText(this->ui->copyButton_MetatileLabel->mapToGlobal(QPoint(0, 0)), "Copied!");
}

void TilesetEditor::on_horizontalSlider_MetatilesZoom_valueChanged(int value) {
    porymapConfig.tilesetEditorMetatilesZoom = value;
    this->redrawMetatileSelector();
}

void TilesetEditor::redrawMetatileSelector() {
    QSize size(this->metatileSelector->pixmap().width(), this->metatileSelector->pixmap().height());
    this->ui->graphicsView_Metatiles->setSceneRect(0, 0, size.width(), size.height());

    double scale = pow(3.0, static_cast<double>(porymapConfig.tilesetEditorMetatilesZoom - 30) / 30.0);
    QTransform transform;
    transform.scale(scale, scale);
    size *= scale;

    this->ui->graphicsView_Metatiles->setTransform(transform);
    this->ui->graphicsView_Metatiles->setFixedSize(size.width() + 2, size.height() + 2);

    this->ui->scrollAreaWidgetContents_Metatiles->adjustSize();
    ensureMetatileVisible(selectedBlockId());
    redrawBehaviorView();
}

void TilesetEditor::ensureMetatileVisible(uint16_t metatileId) {
    const double scale = pow(3.0, static_cast<double>(porymapConfig.tilesetEditorMetatilesZoom - 30) / 30.0);
    QPoint pos = this->metatileSelector->getMetatileIdCoordsOnWidget(metatileId);
    pos *= scale;
    auto viewport = this->ui->scrollArea_Metatiles->viewport();
    this->ui->scrollArea_Metatiles->ensureVisible(pos.x(), pos.y(), viewport->width() / 2, viewport->height() / 2);
    if (this->behaviorScroll) {
        QPoint pos2 = this->behaviorSelector->getMetatileIdCoordsOnWidget(metatileId);
        pos2 *= scale;
        this->behaviorScroll->ensureVisible(pos2.x(), pos2.y(), this->behaviorScroll->viewport()->width() / 2, this->behaviorScroll->viewport()->height() / 2);
    }
}

void TilesetEditor::on_horizontalSlider_TilesZoom_valueChanged(int value) {
    porymapConfig.tilesetEditorTilesZoom = value;
    this->redrawTileSelector();
}

void TilesetEditor::redrawTileSelector() {
    QSize size(this->tileSelector->pixmap().width(), this->tileSelector->pixmap().height());
    this->ui->graphicsView_Tiles->setSceneRect(0, 0, size.width(), size.height());

    double scale = pow(3.0, static_cast<double>(porymapConfig.tilesetEditorTilesZoom - 30) / 30.0);
    QTransform transform;
    transform.scale(scale, scale);
    size *= scale;

    this->ui->graphicsView_Tiles->setTransform(transform);
    this->ui->graphicsView_Tiles->setFixedSize(size.width() + 2, size.height() + 2);

    this->ui->scrollAreaWidgetContents_Tiles->adjustSize();

    auto tiles = this->tileSelector->getSelectedTiles();
    if (!tiles.isEmpty()) {
        QPoint pos = this->tileSelector->getTileCoordsOnWidget(tiles[0].tileId);
        pos *= scale;
        auto viewport = this->ui->scrollArea_Tiles->viewport();
        this->ui->scrollArea_Tiles->ensureVisible(pos.x(), pos.y(), viewport->width() / 2, viewport->height() / 2);
    }
}

// A swap is a step like any other (two edits: each metatile gets the other's tiles, attributes and label), so Undo / Redo apply only what changed
// and refuse when the other page changed one of the two meanwhile (stepFits). isSwap only says that the layouts follow the swap on Save.
void TilesetEditor::commitMetatileSwap(uint16_t metatileIdA, uint16_t metatileIdB) {
    Metatile *a = block(metatileIdA), *b = block(metatileIdB);
    if (!a || !b)
        return;
    auto *step = new MetatileHistoryItem(metatileIdA, metatileIdB);
    MetatileEdit editA, editB;
    editA.id = metatileIdA; editA.prev = *a; editA.next = *b; editA.prevLabel = ownedLabel(metatileIdA); editA.nextLabel = ownedLabel(metatileIdB);
    editB.id = metatileIdB; editB.prev = *b; editB.next = *a; editB.prevLabel = ownedLabel(metatileIdB); editB.nextLabel = ownedLabel(metatileIdA);
    step->edits << editA << editB;
    if (swapMetatiles(metatileIdA, metatileIdB))
        commit(step);
    else
        delete step;
}

// Remembers a swap for the layouts (applied on Save). The inverse of the most recent pending swap (e.g. from Undo) cancels it instead.
void TilesetEditor::recordLayoutSwap(uint16_t metatileIdA, uint16_t metatileIdB) {
    if (!this->metatileIdSwaps.isEmpty()) {
        auto recentSwapPair = this->metatileIdSwaps.constLast();
        if ((recentSwapPair.first == metatileIdB && recentSwapPair.second == metatileIdA) || (recentSwapPair.first == metatileIdA && recentSwapPair.second == metatileIdB)) {
            this->metatileIdSwaps.removeLast();
            return;
        }
    }
    this->metatileIdSwaps.append(QPair<uint16_t,uint16_t>(metatileIdA, metatileIdB));
}

bool TilesetEditor::swapMetatiles(uint16_t metatileIdA, uint16_t metatileIdB) {
    this->metatileSelector->clearSwapSelection();
    if (metatileIdA == 0 || metatileIdB == 0) {   // (nothing half-done: a swap involving the erase entry does not start)
        refuseEntryZero();
        return false;
    }

    QList<Metatile*> metatiles;
    for (const auto &metatileId : {metatileIdA, metatileIdB}) {
        Metatile *metatile = block(metatileId);
        if (metatile) {
            metatiles.append(metatile);
        } else {
            logError(QString("Failed to load metatile %1 for swap.").arg(Metatile::getMetatileIdString(metatileId)));
        }
    }
    if (metatiles.length() < 2)
        return false;

    // Swap the metatile data in the tileset
    Metatile tempMetatile = *metatiles.at(0);
    QString tempLabel = ownedLabel(metatileIdA);
    replaceMetatile(metatileIdA, *metatiles.at(1), ownedLabel(metatileIdB));
    replaceMetatile(metatileIdB, tempMetatile, tempLabel);

    recordLayoutSwap(metatileIdA, metatileIdB);
    return true;
}

// If any metatiles swapped positions, apply the swap to all relevant layouts.
// We only do this once changes in the Tileset Editor are saved.
void TilesetEditor::applyMetatileSwapsToLayouts() {
    if (this->metatileIdSwaps.isEmpty())
        return;

    QProgressDialog progress("", "", 0, this->metatileIdSwaps.length(), this);
    progress.setAutoClose(true);
    progress.setWindowModality(Qt::WindowModal);
    progress.setModal(true);
    progress.setMinimumDuration(1000);
    progress.setValue(progress.minimum());

    for (const auto &swapPair : this->metatileIdSwaps) {
        progress.setLabelText(QString("Swapping metatiles %1 and %2 in map layouts...")
                                        .arg(Metatile::getMetatileIdString(swapPair.first))
                                        .arg(Metatile::getMetatileIdString(swapPair.second)));
        applyMetatileSwapToLayouts(swapPair.first, swapPair.second);
        progress.setValue(progress.value() + 1);
    }
    this->metatileIdSwaps.clear();
}

void TilesetEditor::applyMetatileSwapToLayouts(uint16_t metatileIdA, uint16_t metatileIdB) {
    struct TilesetPair {
        Tileset* primary = nullptr;
        Tileset* secondary = nullptr;
    };
    TilesetPair tilesets;

    // Get which tilesets our swapped metatiles belong to.
    auto addSourceTileset = [this](uint16_t metatileId, TilesetPair *tilesets) {
        if (this->primaryTileset->containsMetatileId(metatileId)) {
            tilesets->primary = this->primaryTileset;
        } else if (this->secondaryTileset->containsMetatileId(metatileId)) {
            tilesets->secondary = this->secondaryTileset;
        } else {
            // Invalid metatile, shouldn't happen
            this->metatileSelector->removeFromSwapSelection(metatileId);
        }
    };
    addSourceTileset(metatileIdA, &tilesets);
    addSourceTileset(metatileIdB, &tilesets);
    if (!tilesets.primary && !tilesets.secondary) {
        return;
    }

    // In each layout that uses the appropriate tileset(s), swap the two metatiles.
    QSet<QString> layoutIds = this->project->getTilesetLayoutIds(tilesets.primary, tilesets.secondary);
    for (const auto &layoutId : layoutIds) {
        Layout *layout = this->project->loadLayout(layoutId);
        if (!layout) continue;
        // Perform swap(s) in layout's main data.
        for (int y = 0; y < layout->height; y++)
        for (int x = 0; x < layout->width; x++) {
            uint16_t metatileId = layout->getMetatileId(x, y);
            if (metatileId == metatileIdA) {
                layout->setMetatileId(x, y, metatileIdB);
            } else if (metatileId == metatileIdB) {
                layout->setMetatileId(x, y, metatileIdA);
            } else continue;
            layout->hasUnsavedDataChanges = true;
        }
        // Perform swap(s) in layout's border data.
        for (auto &borderBlock : layout->border) {
            if (borderBlock.metatileId() == metatileIdA) {
                borderBlock.setMetatileId(metatileIdB);
            } else if (borderBlock.metatileId() == metatileIdB) {
                borderBlock.setMetatileId(metatileIdA);
            } else continue;
            layout->hasUnsavedDataChanges = true;
        }
    }
}

// The modifiers that are held once the key of this event went down (pressed) or up. (QKeyEvent::modifiers() is the state BEFORE the event: it leaves
// out the modifier that a Control / Shift / Meta key press is about to add and includes the one a release is about to take away.)
static Qt::KeyboardModifiers modifiersAfterKey(const QKeyEvent *event, bool pressed) {
    Qt::KeyboardModifiers own;
    switch (event->key()) {
    case Qt::Key_Control: own = Qt::ControlModifier; break;
    case Qt::Key_Shift:   own = Qt::ShiftModifier; break;
    case Qt::Key_Meta:    own = Qt::MetaModifier; break;
    default: return event->modifiers();
    }
    return pressed ? (event->modifiers() | own) : (event->modifiers() & ~own);
}

void TilesetEditor::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta) {
        this->metatileSelector->refreshModifiers(modifiersAfterKey(event, false));
        if (this->behaviorSelector)
            this->behaviorSelector->refreshModifiers(modifiersAfterKey(event, false));
    }
    QMainWindow::keyReleaseEvent(event);
}

void TilesetEditor::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift || event->key() == Qt::Key_Meta) {
        this->metatileSelector->refreshModifiers(modifiersAfterKey(event, true));
        if (this->behaviorSelector)
            this->behaviorSelector->refreshModifiers(modifiersAfterKey(event, true));
    }
    const bool plainKey = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (event->key() == Qt::Key_Escape && ui->actionSwap_Metatiles->isChecked()) {
        ui->actionSwap_Metatiles->setChecked(false);
    } else if (plainKey && (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {
        deleteSelectedTiles();   // (a text field that has the focus takes these keys itself: they never get here)
        event->accept();
    } else if (event->key() == Qt::Key_Escape && !typingInTextField() && deselectAll()) {
        event->accept();
    } else {
        QMainWindow::keyPressEvent(event);
    }
}
