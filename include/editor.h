#pragma once
#ifndef EDITOR_H
#define EDITOR_H

#include <QGraphicsScene>
#include <QGraphicsItemGroup>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsItemAnimation>
#include <QComboBox>
#include <QCheckBox>
#include <QCursor>
#include <QUndoGroup>
#include <QPointer>

#include "mapconnection.h"
#include "metatileselector.h"
#include "movementpermissionsselector.h"
#include "project.h"
#include "ui_mainwindow.h"
#include "bordermetatilespixmapitem.h"
#include "connectionpixmapitem.h"
#include "divingmappixmapitem.h"
#include "currentselectedmetatilespixmapitem.h"
#include "collisionpixmapitem.h"
#include "layoutpixmapitem.h"
#include "premappixmapitem.h"
#include <functional>
#include "behavioroverlayitem.h"
#include "core/premap.h"
#include "settings.h"
#include "gridsettings.h"
#include "movablerect.h"
#include "cursortilerect.h"
#include "mapruler.h"
#include "encountertablemodel.h"

class EventPixmapItem;
class MetatilesPixmapItem;

class Editor : public QObject
{
    Q_OBJECT
public:
    Editor(Ui::MainWindow* ui);
    ~Editor();

    Editor() = delete;
    Editor(const Editor &) = delete;
    Editor & operator = (const Editor &) = delete;

public:
    Ui::MainWindow* ui;

    QPointer<Project> project = nullptr;
    QPointer<Map> map = nullptr;
    QPointer<Layout> layout = nullptr;

    QUndoGroup editGroup; // Manages the undo history for each map

    Settings *settings;
    GridSettings gridSettings;

    void setProject(Project * project);
    bool saveAll();
    bool saveCurrent();
    void saveEncounterTabData();

    void closeProject();

    bool setMap(QString map_name);
    bool setLayout(QString layoutName);
    void unsetMap();

    bool displayMap();
    bool displayLayout();

    void displayMetatileSelector();
    void displayMapMetatiles();
    void displayPreMapLayers();
    void displayPorytileSelector();   // CUSTOM ENGINE: the palette of the Porymap view (the porytiles of the layout's tilesets)
    void redrawPaletteDividers();     // CUSTOM ENGINE: the Tileset Editor's View > Show Tileset Divider changed: both palettes show / hide their red line
    void clearPorytileSelector();
    PorytileSelection porytileSelection() const;   // what the pencil paints with (the palette's selection)
    void displayMapMovementPermissions();
    void displayBorderMetatiles();
    void displayCurrentMetatilesSelection();
    void redrawCurrentMetatilesSelection();
    void displayMovementPermissionSelector();
    void displayMapEvents();
    void displayMapConnections();
    void displayMapBorder();
    void displayMapGrid();
    void updateMapGrid();
    void displayWildMonTables();

    void updateMapBorder();
    void updateMapConnections();

    void setConnectionsVisibility(bool visible);
    void updateDivingMapsVisibility();
    void renderDivingConnections();
    void addNewConnection(const QString &mapName, const QString &direction);
    void replaceConnection(const QString &mapName, const QString &direction);
    void removeConnection(MapConnection* connection);
    void removeSelectedConnection();
    void addNewWildMonGroup(QWidget *window);
    void deleteWildMonGroup();
    void configureEncounterJSON(QWidget *);
    EncounterTableModel* getCurrentWildMonTable();
    bool setDivingMapName(const QString &mapName, const QString &direction);
    QString getDivingMapName(const QString &direction) const;
    void setSelectedConnection(MapConnection *connection);

    void updatePrimaryTileset(QString tilesetLabel, bool forceLoad = false);
    void updateSecondaryTileset(QString tilesetLabel, bool forceLoad = false);
    void toggleBorderVisibility(bool visible, bool enableScriptCallback = true);
    void updateCustomMapAttributes();

    EventPixmapItem *addEventPixmapItem(Event *event);
    void removeEventPixmapItem(Event *event);
    bool canAddEvents(const QList<Event*> &events);
    void selectMapEvent(Event *event, bool toggle = false);
    Event *addNewEvent(Event::Type type);
    void updateEvents();
    void duplicateSelectedEvents();
    void redrawAllEvents();
    void redrawEvents(const QList<Event*> &events);
    void redrawEventPixmapItem(EventPixmapItem *item);
    void updateEventPixmapItemZValue(EventPixmapItem *item);

    bool isMouseInMap() const;
    void setPlayerViewRect(const QRectF &rect);
    void setCursorRectPos(const QPoint &pos);
    void updateCursorRectVisibility();

    void onEventDragged(Event *event, const QPoint &oldPosition, const QPoint &newPosition);
    void onEventReleased(Event *event, const QPoint &position);
    void updateWarpEventWarning(Event *event);
    void updateWarpEventWarnings();

    QPointer<QGraphicsScene> scene = nullptr;
    QGraphicsPixmapItem *current_view = nullptr;
    QPointer<LayoutPixmapItem> map_item = nullptr;
    QPointer<PreMapPixmapItem> preMapItem = nullptr;
    QPointer<BehaviorOverlayItem> behaviorOverlayItem = nullptr; // CUSTOM ENGINE: Behaviors tab overlay, see BehaviorOverlayItem
    QImage behaviorSheet;                                        // the project's graphics/porymap/behavior_sheet.png
    PreMap preMap;
    QList<QPointer<ConnectionPixmapItem>> connection_items;
    QMap<QString, QPointer<DivingMapPixmapItem>> diving_map_items;
    QGraphicsPathItem *connection_mask = nullptr;
    QPointer<CollisionPixmapItem> collision_item = nullptr;
    QGraphicsItemGroup *events_group = nullptr;

    QList<QGraphicsPixmapItem*> borderItems;
    QGraphicsItemGroup *mapGrid = nullptr;
    QGraphicsItemGroup *porymapGrid = nullptr; // CUSTOM ENGINE: the fixed 16x16 grid of the Porymap view
    QPointer<MapRuler> map_ruler = nullptr;

    MovableRect *playerViewRect = nullptr;
    CursorTileRect *cursorMapTileRect = nullptr;

    QPointer<QGraphicsScene> scene_metatiles = nullptr;
    QPointer<QGraphicsScene> scene_current_metatile_selection = nullptr;
    QPointer<QGraphicsScene> scene_selected_border_metatiles = nullptr;
    QPointer<QGraphicsScene> scene_collision_metatiles = nullptr;
    QPointer<MetatileSelector> metatile_selector_item = nullptr;
    QPointer<MetatileSelector> porytile_selector_item = nullptr;   // CUSTOM ENGINE: the Porymap view's palette (kind Porytile)
    QGraphicsScene *scene_porytiles = nullptr;

    QPointer<BorderMetatilesPixmapItem> selected_border_metatiles_item = nullptr;
    CurrentSelectedMetatilesPixmapItem *current_metatile_selection_item = nullptr;
    QPointer<MovementPermissionsSelector> movement_permissions_selector_item = nullptr;

    QList<Event*> selectedEvents;
    QPointer<ConnectionPixmapItem> selected_connection_item = nullptr;
    QPointer<MapConnection> connection_to_select = nullptr;

    enum class EditAction { None, Paint, Select, Fill, Shift, Pick, Move };
    void setEditAction(EditAction editAction);
    EditAction getEditAction() const;
    // CUSTOM ENGINE: true when a mouse press on the Porymap view's pre-map must NOT be handled by it but fall through to
    // the graphics view: the Hand tool, the middle button, and the synthetic left press the view injects while a
    // middle-button scroll is in progress (otherwise panning with the pencil would place an object).
    bool preMapPressPassesThrough(const QGraphicsSceneMouseEvent *event) const;
    EditAction getMapEditAction() const { return this->mapEditAction; }
    EditAction getEventEditAction() const { return this->eventEditAction; }

    // CUSTOM ENGINE: Collision = the Finalmap's Elevation tab (no collision bits any more). PorymapObjects and
    // Behaviors are the two tabs of the Porymap (design) view: they show the porytile grid over the backdrop colour, the real
    // metatile map is hidden. PorymapObjects paints porytiles on the active layer, Behaviors places a behavior per field
    // (like an elevation on the finished map; Auto = the field takes the behavior of its porytiles).
    enum class EditMode { None, Disabled, Metatiles, Collision, Header, Events, Connections, Encounters, Behaviors, PorymapObjects };
    void setEditMode(EditMode editMode);
    EditMode getEditMode() const { return this->editMode; }

    // CUSTOM ENGINE: which of the 3 real map layers newly-placed prefabs go onto on the pre-map.
    // Purely an editing-time choice (see forms/mainwindow.ui widget_PreMapLayers) -- has no effect
    // on the ROM until Write to ROM runs the generate/dedup pass.
    enum class PreMapLayer { Bottom, Middle, Top };
    void setPreMapLayer(PreMapLayer layer) { if (this->preMapLayer != layer) { this->preMapLayer = layer; emit preMapLayerChanged(static_cast<int>(layer)); } }
    PreMapLayer getPreMapLayer() const { return this->preMapLayer; }
    // CUSTOM ENGINE: which metatile layers the Finalmap VIEW shows (bit n set = layer n hidden). A view setting only; nothing of the map changes.
    int finalmapHiddenLayers() const { return this->finalmapHiddenLayerMask; }
    void setFinalmapLayerVisible(int layer, bool visible);
    // CUSTOM ENGINE: whether the pre-map overlay takes mouse input (Prefabs tab) or lets clicks fall
    // through to the regular metatile/collision tools underneath (every other tab).
    // CUSTOM ENGINE: Porymap view = only the Map Objects (pre-map) on an empty background; every other tab shows
    // the real, generated map. One scene and one view serve both; switching only changes visibility.
    bool isPorymapView() const;
    void updatePreMapCursor();
    void updateViewSceneRect();
    void updateActiveUndoStack();

    // CUSTOM ENGINE: undo/redo of Map Object edits -- one QUndoStack per layout, kept for the session (the pre-map itself is
    // reloaded from disk whenever the map is shown again, and layers.json is written after EVERY change, so the two always
    // agree). There is no separate Save for the Porymap data: it is auto-saved; only Write to Finalmap makes it permanent.
    QUndoStack *preMapStackFor(const QString &layoutId);
    // The Behaviors tab has its own history per layout: Undo there takes a placement back, never a painted porytile (and the other way round).
    QUndoStack *preMapBehaviorStackFor(const QString &layoutId);
    void pushPreMapStroke(const PreMapStroke &stroke, const QString &layoutId); // (the pencil already applied it; a behavior stroke goes on the behavior stack)
    // The behavior the pencil of the Behaviors tab places (PreMap::kAutoBehavior = Auto: the placement is taken away).
    uint16_t preMapBehaviorBrush() const { return this->behaviorBrush; }
    void setPreMapBehaviorBrush(uint16_t value) { this->behaviorBrush = value; if (this->preMapItem) this->preMapItem->update(); }
    uint16_t behaviorBrush = PreMap::kAutoBehavior;
    QHash<QString, QUndoStack*> preMapBehaviorStacks;
    void pushPreMapAlpha(int layer, bool value);
    void pushPreMapShift(QPoint delta); // the Shift tool: everything moves together (one undo step)
    // Writes layers.json; on failure tells the user (once, until a save works again) and keeps the edits in memory: a redraw of
    // this map then does not reload the older file over them.
    bool savePreMap(const QString &layoutId);
    static std::function<void(const QString &path)> preMapSaveFailedHook; // tests: replaces the error dialog
    QString preMapUnsavedLayoutId;  // a save of this layout failed and its newest edits exist only in memory
    QString preMapLoadedLayoutId;   // whose data `preMap` currently holds
    void afterPreMapChanged(bool alphaChanged); // a command changed the pre-map: redraw the item, sync the controls, auto-save
    QHash<QString, QUndoStack*> preMapStacks;
    bool preMapStrokeActive = false; // Undo/Redo are off while the pencil is down
    void applyViewMode();
    void applyToolAvailability();
    // the tool the user last picked; it comes back whenever a view offers it again (a view that lacks it
    // temporarily substitutes the Pencil or the Hand)
    EditAction preferredMapAction = EditAction::Paint;
    bool applyingToolFallback = false;
    void loadBehaviorSheet();
    uint32_t behaviorIdAtMapPos(const QPoint &metatilePos) const;
    static QString getElevationName(uint16_t elevation);

    bool getEditingLayout() const;

    void setMapEditingButtonsEnabled(bool enabled);

    int scaleIndex = 2;
    qreal collisionOpacity = 0.5;
    static QList<QList<const QImage*>> collisionIcons;

    int eventShiftActionId = 0;
    int eventMoveActionId = 0;

    void deleteSelectedEvents();
    void shouldReselectEvents();
    void scaleMapView(int);
    static void openInTextEditor(const QString &path, int lineNum = 0);
    void openMapJson(const QString &mapName) const;
    void openLayoutJson(const QString &layoutId) const;
    void setCollisionGraphics();

    enum ZValue {
        MapBorder = -4,
        MapConnectionInactive = -3,
        MapConnectionActive = -2,
        MapConnectionMask = -1,

        // Event pixmaps set their z value to be their y position on the map.
        // Their y value is int16_t, so we have enough space to allocate the
        // full range + 1 for the selected event (which should always be on top).
        EventMinimum = 1,
        EventMaximum = EventMinimum + 0x10000,

        Ruler,
        ResizeLayoutPopup
    };

public slots:
    void openMapScripts() const;
    bool openScript(const QString &scriptLabel) const;
    bool openScriptInFile(const QString &scriptLabel, const QString &filepath) const;
    void openProjectInTextEditor() const;
    void maskNonVisibleConnectionTiles();
    void onBorderMetatilesChanged();
    void selectedEventIndexChanged(int index, Event::Group eventGroup);
    void toggleGrid(bool);

private:
    const QImage defaultCollisionImgSheet = QImage(":/images/collisions.png");
    const QImage collisionPlaceholder = QImage(":/images/collisions_unknown.png");
    QPixmap collisionSheetPixmap;

    EditMode editMode = EditMode::None;
    PreMapLayer preMapLayer = PreMapLayer::Middle;
    int finalmapHiddenLayerMask = 0;

    EditAction mapEditAction = EditAction::Paint;
    EditAction eventEditAction = EditAction::Select;

    bool save(bool currentOnly);
    void clearMap();
    void clearMetatileSelector();
    void clearMovementPermissionSelector();
    void clearMapMetatiles();
    void clearPreMapLayers();
    void clearMapMovementPermissions();
    void clearBorderMetatiles();
    void clearCurrentMetatilesSelection();
    void clearMapEvents();
    void clearMapConnections();
    void clearConnectionMask();
    void clearMapBorder();
    void clearMapGrid();
    void clearWildMonTables();
    int getSortedItemIndex(QComboBox *combo, QString item);
    void updateBorderVisibility();
    void removeConnectionPixmap(MapConnection *connection);
    void displayConnection(MapConnection *connection);
    void displayDivingConnection(MapConnection *connection);
    void removeDivingMapPixmap(MapConnection *connection);
    void onDivingMapEditingFinished(NoScrollComboBox* combo, const QString &direction);
    void updateDivingMapButton(QToolButton* button, const QString &mapName);
    void updateEncounterFields(EncounterFields newFields);
    QString getElevationText(uint16_t elevation);
    QString getMetatileDisplayMessage(uint16_t metatileId);
    void setElevationTabSpinBox(uint16_t elevation);
    void adjustStraightPathPos(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item, QPoint *pos) const;
    static bool startDetachedProcess(const QString &command,
                                    const QString &workingDirectory = QString(),
                                    qint64 *pid = nullptr);
    bool canPaintMetatiles() const;
    void onMapStartPaint(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item);
    void onMapEndPaint(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item);
    void setStatusFromMapPos(const QPoint &pos);
    bool isMiddleButtonScrollInProgress() const;

private slots:
    void setSmartPathCursorMode(QGraphicsSceneMouseEvent *event);
    void mouseEvent_map(QGraphicsSceneMouseEvent *event, LayoutPixmapItem *item);
    void setSelectedConnectionItem(ConnectionPixmapItem *connectionItem);
    void onHoveredMovementPermissionChanged(uint16_t, uint16_t);
    void onHoveredMovementPermissionCleared();
    void onHoveredMetatileSelectionChanged(uint16_t);
    void onHoveredMetatileSelectionCleared();
    void onMapHoverEntered(const QPoint &pos);
    void onMapHoverChanged(const QPoint &pos);
    void onMapHoverCleared();
    void onSelectedMetatilesChanged();
    void onWheelZoom(int);

signals:
    void preMapLayerChanged(int layer); // the active layer changed (also by the eyedropper): the layer bar follows
    void preMapLayersLoaded(); // the pre-map for the current layout was (re)loaded from disk
    void eventsChanged();
    void openEventMap(Event*);
    void openConnectedMap(MapConnection*);
    void wildMonTableOpened(EncounterTableModel*);
    void wildMonTableClosed();
    void wildMonTableEdited();
    void currentMetatilesSelectionChanged();
    void mapRulerStatusChanged(const QString &);
    void gridToggled(bool);
    void editActionSet(EditAction newEditAction);
    void preMapBehaviorPicked(uint16_t stored, uint32_t shown);   // CUSTOM ENGINE: the eyedropper of the Behaviors tab found a field
    void tilesetsTransferred();   // CUSTOM ENGINE: Write / Pull rewrote the tilesets (and saved them): the views must catch up
};

#endif // EDITOR_H
