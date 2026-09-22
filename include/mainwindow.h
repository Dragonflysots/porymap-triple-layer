#pragma once
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QString>
#include <functional>
#include <QModelIndex>
#include <QMainWindow>
#include <QToolButton>
#include <QCheckBox>
#include <QStandardItemModel>
#include <QGraphicsPixmapItem>
#include <QGraphicsItemGroup>
#include <QGraphicsSceneMouseEvent>
#include <QCloseEvent>
#include <QAbstractItemModel>
#include "project.h"
#include "orderedjson.h"
#include "config.h"
#include "map.h"
#include "editor.h"
#include "tileseteditor.h"
#include "regionmapeditor.h"
#include "mapimageexporter.h"
#include "filterchildrenproxymodel.h"
#include "maplistmodels.h"
#include "shortcutseditor.h"
#include "preferenceeditor.h"
#include "projectsettingseditor.h"
#include "gridsettings.h"
#include "customscriptseditor.h"
#include "wildmonchart.h"
#include "wildmonsearch.h"
#include "updatepromoter.h"
#include "aboutporymap.h"
#include "mapheaderform.h"
#include "newlayoutdialog.h"
#include "message.h"
#include "resizelayoutpopup.h"
#include "unlockableicon.h"

#if __has_include(<QJSValue>)
#include <QJSValue>
#endif



namespace Ui {
class MainWindow;
class QDialog;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent);
    ~MainWindow();

    MainWindow() = delete;
    MainWindow(const MainWindow &) = delete;
    MainWindow & operator = (const MainWindow &) = delete;

    void initialize();

    // CUSTOM ENGINE: the confirmation of Write to Finalmap / Pull to Porymap, replaced by the headless tests
    // (they return QMessageBox::Yes / No instead of opening a modal dialog).
    static std::function<int()> pushToFinalmapPrompt;
    static std::function<int()> pullToPorymapPrompt;
    static QString lastPullQuestion;   // the text the last Pull asked (tests read it; the user sees it in the dialog)
    // CUSTOM ENGINE: the eraser. Porymap tab: "Clear Layers" -- a window asks WHAT to clear, then a question asks once more. Finalmap tab: "Clean the
    // Map" -- one question. The tests replace the windows: the chooser returns the parts (PreMapClearCommand::Part bits, 0 = cancelled), the prompts
    // return QMessageBox::Yes / No; `lastEraserQuestion` is the text of the last question.
    static std::function<int()> clearLayersChooser;
    static std::function<int()> clearLayersPrompt;
    static std::function<int()> cleanMapPrompt;
    static QString lastEraserQuestion;
    static std::function<int(QDialog *)> clearLayersDialogDriver;   // demo recordings / tests: drives the Clear Layers window without a modal loop (returns QDialog::Accepted / Rejected)
    static QString lastPushQuestion;                                // the text the last Write to Finalmap asked (the recordings show it)

    Q_INVOKABLE void setPrimaryTileset(const QString &tileset);
    Q_INVOKABLE void setSecondaryTileset(const QString &tileset);

    // Scripting API
#ifdef QT_QML_LIB
    Q_INVOKABLE QJSValue getBlock(int x, int y);
    void tryRedrawMapArea(bool forceRedraw);
    void redrawResizedMapArea();
    void tryCommitMapChanges(bool commitChanges);
    Q_INVOKABLE void setBlock(int x, int y, int metatileId, int collision, int elevation, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void setBlock(int x, int y, int rawValue, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void setBlocksFromSelection(int x, int y, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE int getMetatileId(int x, int y);
    Q_INVOKABLE void setMetatileId(int x, int y, int metatileId, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE int getBorderMetatileId(int x, int y);
    Q_INVOKABLE void setBorderMetatileId(int x, int y, int metatileId, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE int getCollision(int x, int y);
    Q_INVOKABLE void setCollision(int x, int y, int collision, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE int getElevation(int x, int y);
    Q_INVOKABLE void setElevation(int x, int y, int elevation, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void bucketFill(int x, int y, int metatileId, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void bucketFillFromSelection(int x, int y, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void magicFill(int x, int y, int metatileId, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void magicFillFromSelection(int x, int y, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void shift(int xDelta, int yDelta, bool forceRedraw = true, bool commitChanges = true);
    Q_INVOKABLE void redraw();
    Q_INVOKABLE void commit();
    Q_INVOKABLE QJSValue getDimensions();
    Q_INVOKABLE int getWidth();
    Q_INVOKABLE int getHeight();
    Q_INVOKABLE QJSValue getBorderDimensions();
    Q_INVOKABLE int getBorderWidth();
    Q_INVOKABLE int getBorderHeight();
    Q_INVOKABLE void setDimensions(int width, int height);
    Q_INVOKABLE void setWidth(int width);
    Q_INVOKABLE void setHeight(int height);
    Q_INVOKABLE void setBorderDimensions(int width, int height);
    Q_INVOKABLE void setBorderWidth(int width);
    Q_INVOKABLE void setBorderHeight(int height);
    void refreshAfterPaletteChange(Tileset *tileset);
    void setTilesetPalette(Tileset *tileset, int paletteIndex, QList<QList<int>> colors);
    Q_INVOKABLE void setPrimaryTilesetPalette(int paletteIndex, QList<QList<int>> colors, bool forceRedraw = true);
    Q_INVOKABLE void setPrimaryTilesetPalettes(QList<QList<QList<int>>> palettes, bool forceRedraw = true);
    Q_INVOKABLE void setSecondaryTilesetPalette(int paletteIndex, QList<QList<int>> colors, bool forceRedraw = true);
    Q_INVOKABLE void setSecondaryTilesetPalettes(QList<QList<QList<int>>> palettes, bool forceRedraw = true);
    QJSValue getTilesetPalette(const QList<QList<QRgb>> &palettes, int paletteIndex);
    QJSValue getTilesetPalettes(const QList<QList<QRgb>> &palettes);
    Q_INVOKABLE QJSValue getPrimaryTilesetPalette(int paletteIndex);
    Q_INVOKABLE QJSValue getPrimaryTilesetPalettes();
    Q_INVOKABLE QJSValue getSecondaryTilesetPalette(int paletteIndex);
    Q_INVOKABLE QJSValue getSecondaryTilesetPalettes();
    void refreshAfterPalettePreviewChange();
    void setTilesetPalettePreview(Tileset *tileset, int paletteIndex, QList<QList<int>> colors);
    Q_INVOKABLE void setPrimaryTilesetPalettePreview(int paletteIndex, QList<QList<int>> colors, bool forceRedraw = true);
    Q_INVOKABLE void setPrimaryTilesetPalettesPreview(QList<QList<QList<int>>> palettes, bool forceRedraw = true);
    Q_INVOKABLE void setSecondaryTilesetPalettePreview(int paletteIndex, QList<QList<int>> colors, bool forceRedraw = true);
    Q_INVOKABLE void setSecondaryTilesetPalettesPreview(QList<QList<QList<int>>> palettes, bool forceRedraw = true);
    Q_INVOKABLE QJSValue getPrimaryTilesetPalettePreview(int paletteIndex);
    Q_INVOKABLE QJSValue getPrimaryTilesetPalettesPreview();
    Q_INVOKABLE QJSValue getSecondaryTilesetPalettePreview(int paletteIndex);
    Q_INVOKABLE QJSValue getSecondaryTilesetPalettesPreview();
    Q_INVOKABLE int getNumPrimaryTilesetMetatiles();
    Q_INVOKABLE int getNumSecondaryTilesetMetatiles();
    Q_INVOKABLE int getNumPrimaryTilesetTiles();
    Q_INVOKABLE int getNumSecondaryTilesetTiles();
    Q_INVOKABLE QString getPrimaryTileset();
    Q_INVOKABLE QString getSecondaryTileset();
    void saveMetatilesByMetatileId(int metatileId);
    void saveMetatileAttributesByMetatileId(int metatileId);
    Metatile * getMetatile(int metatileId);
    Q_INVOKABLE QString getMetatileLabel(int metatileId);
    Q_INVOKABLE void setMetatileLabel(int metatileId, QString label);
    Q_INVOKABLE int getMetatileLayerType(int metatileId);
    Q_INVOKABLE void setMetatileLayerType(int metatileId, int layerType);
    Q_INVOKABLE int getMetatileEncounterType(int metatileId);
    Q_INVOKABLE void setMetatileEncounterType(int metatileId, int encounterType);
    Q_INVOKABLE int getMetatileTerrainType(int metatileId);
    Q_INVOKABLE void setMetatileTerrainType(int metatileId, int terrainType);
    Q_INVOKABLE int getMetatileBehavior(int metatileId);
    Q_INVOKABLE void setMetatileBehavior(int metatileId, int behavior);
    Q_INVOKABLE QString getMetatileBehaviorName(int metatileId);
    Q_INVOKABLE void setMetatileBehaviorName(int metatileId, QString behavior);
    Q_INVOKABLE int getMetatileAttributes(int metatileId);
    Q_INVOKABLE void setMetatileAttributes(int metatileId, int attributes);
    Q_INVOKABLE QJSValue getMetatileTile(int metatileId, int tileIndex);
    Q_INVOKABLE void setMetatileTile(int metatileId, int tileIndex, int tileId, bool xflip, bool yflip, int palette, bool forceRedraw = true);
    Q_INVOKABLE void setMetatileTile(int metatileId, int tileIndex, QJSValue tileObj, bool forceRedraw = true);
    int calculateTileBounds(int * tileStart, int * tileEnd);
    Q_INVOKABLE QJSValue getMetatileTiles(int metatileId, int tileStart = 0, int tileEnd = -1);
    Q_INVOKABLE void setMetatileTiles(int metatileId, QJSValue tilesObj, int tileStart = 0, int tileEnd = -1, bool forceRedraw = true);
    Q_INVOKABLE void setMetatileTiles(int metatileId, int tileId, bool xflip, bool yflip, int palette, int tileStart = 0, int tileEnd = -1, bool forceRedraw = true);
    Q_INVOKABLE QJSValue getTilePixels(int tileId);
    Q_INVOKABLE QList<int> getMetatileLayerOrder() const;
    Q_INVOKABLE void setMetatileLayerOrder(const QList<int> &order);
    Q_INVOKABLE QList<float> getMetatileLayerOpacity() const;
    Q_INVOKABLE void setMetatileLayerOpacity(const QList<float> &opacities);
    Q_INVOKABLE QString getSong();
    Q_INVOKABLE void setSong(QString song);
    Q_INVOKABLE QString getLocation();
    Q_INVOKABLE void setLocation(QString location);
    Q_INVOKABLE bool getRequiresFlash();
    Q_INVOKABLE void setRequiresFlash(bool require);
    Q_INVOKABLE QString getWeather();
    Q_INVOKABLE void setWeather(QString weather);
    Q_INVOKABLE QString getType();
    Q_INVOKABLE void setType(QString type);
    Q_INVOKABLE QString getBattleScene();
    Q_INVOKABLE void setBattleScene(QString battleScene);
    Q_INVOKABLE bool getShowLocationName();
    Q_INVOKABLE void setShowLocationName(bool show);
    Q_INVOKABLE bool getAllowRunning();
    Q_INVOKABLE void setAllowRunning(bool allow);
    Q_INVOKABLE bool getAllowBiking();
    Q_INVOKABLE void setAllowBiking(bool allow);
    Q_INVOKABLE bool getAllowEscaping();
    Q_INVOKABLE void setAllowEscaping(bool allow);
    Q_INVOKABLE int getFloorNumber();
    Q_INVOKABLE void setFloorNumber(int floorNumber);
#endif // QT_QML_LIB

public slots:
    void on_mainTabBar_tabBarClicked(int index);
    void on_mapViewTab_tabBarClicked(int index);
    void on_porymapViewTab_tabBarClicked(int index);
    // CUSTOM ENGINE: the two conversions between the Porymap view and the Finalmap (see core/maptransfer.h). Both ask first;
    // the two hooks replace that question in the headless tests (they return QMessageBox::Yes / No).
    void on_pushButton_PushToFinalmap_clicked();
    void on_pushButton_PullToPorymap_clicked();
    // CUSTOM ENGINE: the Behaviors tab of the Porymap view: the list is the brush of its pencil, the filter narrows it, the eyedropper selects in it.
    void on_listWidget_Behaviors_currentRowChanged(int row);
    void on_lineEdit_BehaviorsFilter_textChanged(const QString &text);
    void on_horizontalSlider_BehaviorsOpacity_valueChanged(int value);
    void selectBehaviorInList(uint16_t stored, uint32_t shown);
    void onWarpBehaviorWarningClicked();
    void clearOverlay();

private slots:
    void on_action_Open_Project_triggered();
    void on_action_Reload_Project_triggered();
    void on_action_Close_Project_triggered();
    void on_action_Save_Project_triggered();
    bool save(bool currentOnly = false);

    void openEventMap(Event *event);

    void duplicate();
    void setClipboardData(poryjson::Json::object);
    void setClipboardData(QImage);
    void setClipboardData(const QString &text);
    void copy();
    void paste();

    void onOpenConnectedMap(MapConnection*);
    void onTilesetsSaved(QString, QString);
    void onNewMapCreated(Map *newMap, const QString &groupName);
    void onNewMapGroupCreated(const QString &groupName);
    void onNewMapSectionCreated(const QString &idName);
    void onMapSectionDisplayNameChanged(const QString &idName, const QString &displayName);
    void onNewLayoutCreated(Layout *layout);
    void onNewTilesetCreated(Tileset *tileset);
    void onMapLoaded(Map *map);
    void onMapRulerStatusChanged(const QString &);
    void applyUserShortcuts();
    void markMapEdited(Map*);
    void markLayoutEdited();

    void on_actionNew_Tileset_triggered();
    void on_action_Save_triggered();
    void on_action_Exit_triggered();
    void onLayoutSelectorEditingFinished();
    void on_comboBox_LayoutSelector_currentTextChanged(const QString &text);
    void on_actionShortcuts_triggered();

    void on_actionZoom_In_triggered();
    void on_actionZoom_Out_triggered();
    void on_actionBetter_Cursors_triggered();
    void on_actionPlayer_View_Rectangle_triggered();
    void on_actionCursor_Tile_Outline_triggered();
    void on_actionPencil_triggered();
    void on_actionPointer_triggered();
    void on_actionFlood_Fill_triggered();
    void on_actionEyedropper_triggered();
    void on_actionMove_triggered();
    void on_actionMap_Shift_triggered();

    void tryAddEventTab(QWidget * tab);
    void displayEventTabs();
    void updateSelectedEvents();
    void updateEvents();

    void on_toolButton_Paint_clicked();
    void on_toolButton_Select_clicked();
    void on_toolButton_Fill_clicked();
    void on_toolButton_Dropper_clicked();
    void on_toolButton_Move_clicked();
    void on_toolButton_Shift_clicked();

    void onOpenMapListContextMenu(const QPoint &point);
    void currentMetatilesSelectionChanged();

    void on_action_Export_Map_Image_triggered();
    void on_actionExport_Stitched_Map_Image_triggered();
    void on_actionExport_Map_Timelapse_Image_triggered();
    void on_actionImport_Map_from_Advance_Map_1_92_triggered();

    void on_pushButton_AddConnection_clicked();
    void on_button_OpenDiveMap_clicked();
    void on_button_OpenEmergeMap_clicked();
    void on_pushButton_ChangeDimensions_clicked();
    void applyLayoutResize(const QMargins &result, const QSize &borderResult);

    void resetMapViewScale();

    void on_actionTileset_Editor_triggered();

    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *) override;

    void eventTabChanged(int index);

    void on_actionDive_Emerge_Map_triggered();
    void on_actionShow_Events_In_Map_View_triggered();
    void on_groupBox_DiveMapOpacity_toggled(bool on);
    void on_slider_DiveEmergeMapOpacity_valueChanged(int value);
    void on_slider_DiveMapOpacity_valueChanged(int value);
    void on_slider_EmergeMapOpacity_valueChanged(int value);
    void on_horizontalSlider_CollisionTransparency_valueChanged(int value);

    void mapListShortcut_ToggleEmptyFolders();
    void mapListShortcut_ExpandAll();
    void mapListShortcut_CollapseAll();

    void on_actionAbout_Porymap_triggered();
    void on_actionOpen_Log_File_triggered();
    void on_actionOpen_Config_Folder_triggered();
    void on_horizontalSlider_MetatileZoom_valueChanged(int value);
    void on_horizontalSlider_CollisionZoom_valueChanged(int value);
    void on_pushButton_NewWildMonGroup_clicked();
    void on_pushButton_DeleteWildMonGroup_clicked();
    void on_pushButton_SummaryChart_clicked();
    void on_pushButton_ConfigureEncountersJSON_clicked();
    void on_toolButton_WildMonSearch_clicked();
    void on_spinBox_SelectedElevation_valueChanged(int elevation);
    void on_actionRegion_Map_Editor_triggered();
    void on_actionPreferences_triggered();
    void on_actionOpen_Manual_triggered();
    void on_actionCheck_for_Updates_triggered();
    void togglePreferenceSpecificUi();
    void on_actionProject_Settings_triggered();
    void on_actionCustom_Scripts_triggered();
    void reloadScriptEngine();
    void on_actionShow_Grid_triggered();
    void on_actionGrid_Settings_triggered();
    void openWildMonTable(const QString &mapName, const QString &groupName, const QString &fieldName);

public:
    Ui::MainWindow *ui;
    QPointer<Editor> editor = nullptr;

signals:
    void mapOpened(Map*);
    void layoutOpened(Layout*);

private:
    QToolButton *layerSelectButtons[3] = { nullptr, nullptr, nullptr }; // CUSTOM ENGINE: the 'active layer' buttons (Bottom / Middle / Top)
    QToolButton *layerEyeButtons[3] = { nullptr, nullptr, nullptr };  // CUSTOM ENGINE: per-layer eye toggles (live inside widget_PreMapLayers)
    // CUSTOM ENGINE: the Porymap and the Finalmap are ONE top-level tab, "Maps", with the two as its sub-tabs. The logical tabs stay what they were
    // (mainTabBar keeps all six, every existing call works unchanged); what the user sees is `topTabBar` (Maps | Events | Header | Connections |
    // Wild Pokemon) and, only while Maps is showing, mainTabBar cut down to its two map tabs as the second row.
    QTabBar *topTabBar = nullptr;
    int lastMapsTab = 1;   // MainTab::Map: the sub-tab the Maps tab comes back to (the app starts on the Finalmap)
    void initTopTabBar();
    void syncTopTabBar();
    QToolButton *eraserButton = nullptr;                              // CUSTOM ENGINE: the eraser next to the layer bar (only on the Porymap and Finalmap tabs)
    QToolButton *layerViewButtons[3] = { nullptr, nullptr, nullptr };  // CUSTOM ENGINE: the Finalmap tab's version: eye and name in ONE toggle (show / hide that layer of the Finalmap view)
    QCheckBox *layerAlphaChecks[3] = { nullptr, nullptr, nullptr };   // CUSTOM ENGINE: per-layer Alpha Channel flags
    QLabel *label_MapRulerStatus = nullptr;
    QPointer<TilesetEditor> tilesetEditor = nullptr;
    QPointer<RegionMapEditor> regionMapEditor = nullptr;
    QPointer<ShortcutsEditor> shortcutsEditor = nullptr;
    QPointer<MapImageExporter> mapImageExporter = nullptr;
    QPointer<PreferenceEditor> preferenceEditor = nullptr;
    QPointer<ProjectSettingsEditor> projectSettingsEditor = nullptr;
    QPointer<GridSettingsDialog> gridSettingsDialog = nullptr;
    QPointer<CustomScriptsEditor> customScriptsEditor = nullptr;

    QPointer<FilterChildrenProxyModel> groupListProxyModel = nullptr;
    QPointer<MapGroupModel> mapGroupModel = nullptr;
    QPointer<FilterChildrenProxyModel> locationListProxyModel = nullptr;
    QPointer<MapLocationModel> mapLocationModel = nullptr;
    QPointer<FilterChildrenProxyModel> layoutListProxyModel = nullptr;
    QPointer<LayoutTreeModel> layoutTreeModel = nullptr;

#ifdef QT_NETWORK_LIB
    QPointer<UpdatePromoter> updatePromoter = nullptr;
    QPointer<NetworkAccessManager> networkAccessManager = nullptr;
#endif

    QPointer<AboutPorymap> aboutWindow = nullptr;
    QPointer<WildMonChart> wildMonChart = nullptr;
    QPointer<WildMonSearch> wildMonSearch = nullptr;
    QPointer<QuestionMessage> fileWatcherWarning = nullptr;
    QPointer<ResizeLayoutPopup> resizeLayoutPopup = nullptr;

    QAction *undoAction = nullptr;
    QAction *redoAction = nullptr;
    QPointer<QUndoView> undoView = nullptr;

    struct MapNavigation {
        QStack<QString> stack;
        QPointer<QToolButton> button;
    };
    MapNavigation backNavigation;
    MapNavigation forwardNavigation;
    bool ignoreNavigationRecords = false;

    UnlockableIcon unlockableMainTabIcon;

    QAction *copyAction = nullptr;
    QAction *pasteAction = nullptr;

    MapHeaderForm *mapHeaderForm = nullptr;

    QMap<Event::Group, Event*> lastSelectedEvent;

    bool isProgrammaticEventTabChange;

    bool tilesetNeedsRedraw = false;
    bool lockMapListAutoScroll = false;

    QSet<QObject*> objectsDisabled;

    bool setLayout(const QString &layoutId);
    bool setMap(const QString &mapName);
    void unsetMap();
    bool userSetLayout(const QString &layoutId);
    bool userSetMap(const QString &mapName);
    void redrawMapScene();
    void refreshMapScene();
    void refreshMetatileViews();
    void refreshCollisionSelector();
    void refreshBehaviorList();
    bool transferPreconditionsOk(const QString &what);   // CUSTOM ENGINE: guards of Write to Finalmap / Pull to Porymap
    void updateTransferButtons();                        // Write to Finalmap only on the Porymap tab, Pull to Porymap only on the Finalmap tab
    void onEraserClicked();                              // CUSTOM ENGINE: the eraser button: Clear Layers (Porymap tab) / Clean the Map (Finalmap tab)
    void clearPorymapLayers();
    void cleanFinalmap();
    int askLayersToClear();                              // the window that asks what to clear (0 = cancelled)
    void onTilesetsTransferred();
    void setLayoutOnlyMode(bool layoutOnly);

    bool isInvalidProject(Project *project);
    bool checkProjectSanity(Project *project);
    bool checkProjectVersion(Project *project);
    bool loadProjectData();
    bool setProjectUI();
    void clearProjectUI();

    void openEditHistory();
    void openNewMapDialog();
    void openDuplicateMapDialog(const QString &mapName);
    NewLayoutDialog* createNewLayoutDialog(const Layout *layoutToCopy = nullptr);
    void openNewLayoutDialog();
    void openDuplicateLayoutDialog(const QString &layoutId);
    void openNewMapGroupDialog();
    void openNewLocationDialog();
    void scrollMapList(MapTree *list, const QString &itemName, bool expandItem = true);
    void scrollMapListToCurrentMap(MapTree *list);
    void scrollMapListToCurrentLayout(MapTree *list);
    void scrollCurrentMapListToItem(const QString &itemName, bool expandItem = true);
    void showFileWatcherWarning();
    void syncPreMapLayerControls();
    void redrawPorytileSelector();   // CUSTOM ENGINE: the Porymap view's palette
    void on_horizontalSlider_PorytilesZoom_valueChanged(int value);
    void updateLayerBarEnabled();
    bool openProject(QString dir, bool initial = false);
    bool closeProject();
    void showRecentError(const QString &baseMessage);
    void showProjectOpenFailure();

    bool setInitialMap();
    void saveGlobalConfigs();

    void refreshRecentProjectsMenu();

    void rebuildMapList_Locations();
    void rebuildMapList_Layouts();
    void setMapListSorted(MapTree *list, bool sort);
    void updateMapList();
    void openMapListItem(const QModelIndex &index);
    void onMapListTabChanged(int index);
    QString getActiveItemName();
    void recordMapNavigation(const QString &itemName);
    void resetMapNavigation();
    void openMapFromHistory(bool previous);
    void openPreviousMap();
    void openNextMap();

    void displayMapProperties();
    void setEditActionUi(Editor::EditAction editAction);

    void updateWindowTitle();

    void initWindow();
    void initLogStatusBar();
    void initCustomUI();
    void initExtraSignals();
    void initEditor();
    void initMiscHeapObjects();
    void initMapList();
    void initShortcuts();
    void initExtraShortcuts();
    void loadUserSettings();
    void restoreWindowState();
    void setTheme(QString);
    void updateTilesetEditor();
    Event::Group getEventGroupFromTabWidget(QWidget *tab);
    bool closeSupplementaryWindows();
    void setWindowDisabled(bool);
    void resetMapCustomAttributesTable();
    void initTilesetEditor();
    bool initRegionMapEditor(bool silent = false);
    bool askToFixRegionMapEditor();
    void initShortcutsEditor();
    void initCustomScriptsEditor();
    void connectSubEditorsToShortcutsEditor();
    void openProjectSettingsEditor(int tab);
    bool isProjectOpen();
    void showExportMapImageWindow(ImageExporterMode mode);
    double getMetatilesZoomScale();
    void redrawMetatileSelection();
    void scrollMetatileSelectorToSelection();
    MapListToolBar* getMapListToolBar(int tab);
    MapListToolBar* getCurrentMapListToolBar();
    MapTree* getCurrentMapList();
    void setLocationComboBoxes(const QStringList &locations);
    void overrideMainTabIcons(const QIcon& icon);
    void tryUnlockMainTabIcon(const Map* map);
    QObjectList shortcutableObjects() const;
    void addCustomHeaderValue(QString key, QJsonValue value, bool isNew = false);

    void checkForUpdates(bool requestedByUser);
    void setDivingMapsVisible(bool visible);

    void setSmartPathsEnabled(bool enabled);
    void setBorderVisibility(bool visible);
    void setMirrorConnectionsEnabled(bool enabled);
};

// These are namespaced in a struct to avoid colliding with e.g. class Map.
struct MainTab {
    enum {
        Porymap, // CUSTOM ENGINE: the design view (Map Objects on 3 layers); the Finalmap is generated from it
        Map,     // CUSTOM ENGINE: this is the Finalmap, the real generated map, edited like in original Porymap
        Events,
        Header,
        Connections,
        WildPokemon,
    };
};

// Right-hand tabs of the Finalmap
struct MapViewTab {
    enum {
        Metatiles,
        Elevation, // CUSTOM ENGINE: was "Collision" (no collision bits any more, 8 elevation values)
    };
};

// CUSTOM ENGINE: right-hand tabs of the Porymap (design) view
struct PorymapViewTab {
    enum {
        MapObjects,
        Behaviors,
    };
};

struct MapListTab {
    enum {
        Groups = 0, Locations, Layouts
    };
};

#endif // MAINWINDOW_H
