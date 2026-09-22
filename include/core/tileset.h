#pragma once
#ifndef TILESET_H
#define TILESET_H

#include "metatile.h"
#include "tile.h"
#include <QImage>
#include <QHash>
#include <QMap>

struct MetatileLabelPair {
    QString owned;
    QString shared;
};

// CUSTOM ENGINE: the two kinds of blocks a tileset holds. Metatile = the generated 12-tile / 3-layer block the ROM uses. Porytile = the
// single-layer block of 2x2 tiles the design view is painted with (a template that can sit on any of the three layers of a field; a
// default behavior and an optional base label go with it). Porytiles live in porytiles.json next to metatiles.bin, are never read by
// make, and use the SAME id space as metatiles (primary ids from 0, secondary ids from the number of primary metatiles).
enum class BlockKind { Metatile, Porytile };

class Tileset
{
public:
    Tileset() = default;
    Tileset(const Tileset &other);
    Tileset &operator=(const Tileset &other);
    ~Tileset();

public:
    QString name;
    bool is_secondary;
    QString tiles_label;
    QString palettes_label;
    QString metatiles_label;
    QString metatiles_path;
    QString metatile_attrs_label;
    QString metatile_attrs_path;
    QString porytiles_path;   // CUSTOM ENGINE: porytiles.json (derived from metatiles_path)
    QString tilesImagePath;
    QStringList palettePaths;

    QHash<int, QString> metatileLabels;
    QHash<int, QString> porytileLabels;   // CUSTOM ENGINE: the base labels of the porytiles (by id)
    QList<QList<QRgb>> palettes;
    QList<QList<QRgb>> palettePreviews;

    static QString stripPrefix(const QString &fullName);
    static Tileset* getPaletteTileset(int, Tileset*, Tileset*);
    static const Tileset* getPaletteTileset(int, const Tileset*, const Tileset*);
    static Tileset* getMetatileTileset(int, Tileset*, Tileset*);
    static const Tileset* getMetatileTileset(int, const Tileset*, const Tileset*);
    static Tileset* getTileTileset(int, Tileset*, Tileset*);
    static const Tileset* getTileTileset(int, const Tileset*, const Tileset*);
    static Metatile* getMetatile(int, Tileset*, Tileset*);
    static const Metatile* getMetatile(int, const Tileset*, const Tileset*);
    static Tileset* getMetatileLabelTileset(int, Tileset*, Tileset*);
    static QString getMetatileLabel(int, Tileset *, Tileset *);
    static QString getOwnedMetatileLabel(int, Tileset *, Tileset *);
    static MetatileLabelPair getMetatileLabelPair(int metatileId, Tileset *primaryTileset, Tileset *secondaryTileset);
    static bool setMetatileLabel(int, QString, Tileset *, Tileset *);
    QString getMetatileLabelPrefix();
    static QString getMetatileLabelPrefix(const QString &name);
    static QList<QList<QRgb>> getBlockPalettes(const Tileset*, const Tileset*, bool useTruePalettes = false);
    static QList<QRgb> getPalette(int, const Tileset*, const Tileset*, bool useTruePalettes = false);
    static bool metatileIsValid(uint16_t metatileId, const Tileset*, const Tileset*);

    // ---- CUSTOM ENGINE: porytiles, and the kind-generic access that the Tileset Editor's sheets use for either kind ----------------
    static constexpr int kDefaultNumPorytiles = 64;
    static constexpr int tilesPerPorytile() { return Metatile::tilesPerLayer(); }
    static int tilesPerBlock(BlockKind kind);
    static int layersPerBlock(BlockKind kind) { return kind == BlockKind::Porytile ? 1 : 3; }
    static Metatile* getPorytile(int porytileId, Tileset*, Tileset*);
    static const Metatile* getPorytile(int porytileId, const Tileset*, const Tileset*);
    static Metatile* getBlock(BlockKind kind, int id, Tileset *primary, Tileset *secondary);
    static const Metatile* getBlock(BlockKind kind, int id, const Tileset *primary, const Tileset *secondary);
    static bool blockIsValid(BlockKind kind, uint16_t id, const Tileset *primary, const Tileset *secondary);
    static QString getOwnedBlockLabel(BlockKind kind, int id, Tileset *primary, Tileset *secondary);
    static bool setBlockLabel(BlockKind kind, int id, const QString &label, Tileset *primary, Tileset *secondary);
    QHash<int, QString> &blockLabels(BlockKind kind) { return kind == BlockKind::Porytile ? this->porytileLabels : this->metatileLabels; }
    const QHash<int, QString> &blockLabels(BlockKind kind) const { return kind == BlockKind::Porytile ? this->porytileLabels : this->metatileLabels; }
    const QList<Metatile*> &blocks(BlockKind kind) const { return kind == BlockKind::Porytile ? m_porytiles : m_metatiles; }
    int numBlocks(BlockKind kind) const { return blocks(kind).length(); }
    bool containsBlockId(BlockKind kind, uint16_t id) const { return id >= firstMetatileId() && static_cast<int>(id) < firstMetatileId() + numBlocks(kind); }
    void resizeBlocks(BlockKind kind, int count) { if (kind == BlockKind::Porytile) resizePorytiles(count); else resizeMetatiles(count); }
    const QList<Metatile*> &porytiles() const { return m_porytiles; }
    int numPorytiles() const { return m_porytiles.length(); }
    void resizePorytiles(int count);
    void clearPorytiles();
    bool loadPorytiles();   // never fails the tileset: a missing file means kDefaultNumPorytiles blank porytiles, a corrupt one is set aside (see porytilesLoadError)
    bool savePorytiles();
    QString porytilesLoadError;   // what was wrong with porytiles.json when it was loaded (empty: nothing); the file was renamed to *.corrupt-<time> then
    static QString porytilesPathFor(const QString &metatilesPath);
    // The project's behavior names (MB_...), for porytiles.json (the project fills them in when it reads the behaviors).
    static QMap<QString, uint32_t> behaviorNames;
    static QMap<uint32_t, QString> behaviorNamesInverse;
    static QHash<int, QString> getHeaderMemberMap(bool usingAsm);
    static QString getExpectedDir(QString tilesetName, bool isSecondary);
    QString getExpectedDir();

    bool load();
    bool loadMetatiles();
    bool loadMetatileAttributes();
    bool loadTilesImage(QImage *importedImage = nullptr);
    bool loadPalettes();

    bool save();
    bool saveMetatileAttributes();
    bool saveMetatiles();
    bool saveTilesImage();
    bool savePalettes();

    bool appendToHeaders(const QString &filepath, const QString &friendlyName, bool usingAsm);
    bool appendToGraphics(const QString &filepath, const QString &friendlyName, bool usingAsm);
    bool appendToMetatiles(const QString &filepath, const QString &friendlyName, bool usingAsm);

    void setTilesImage(const QImage &image);

    void setMetatiles(const QList<Metatile*> &metatiles);
    void addMetatile(Metatile* metatile);

    const QList<Metatile*> &metatiles() const { return m_metatiles; }
    const Metatile* metatileAt(unsigned int i) const { return m_metatiles.at(i); }

    void clearMetatiles();
    void resizeMetatiles(int newNumMetatiles);
    int numMetatiles() const { return m_metatiles.length(); }
    int maxMetatiles() const;

    uint16_t firstMetatileId() const;
    uint16_t lastMetatileId() const;
    bool containsMetatileId(uint16_t metatileId) const { return metatileId >= firstMetatileId() && metatileId <= lastMetatileId(); }

    uint16_t firstTileId() const;
    uint16_t lastTileId() const;
    bool containsTileId(uint16_t tileId) const { return tileId >= firstTileId() && tileId <= lastTileId(); }

    int numTiles() const { return m_tiles.length(); }
    int maxTiles() const;

    QImage tileImage(uint16_t tileId) const { return m_tiles.value(Tile::getIndexInTileset(tileId)); }

    QSet<int> getUnusedColorIds(int paletteId, const Tileset *pairedTileset, const QSet<int> &searchColors = {}) const;
    QList<uint16_t> findMetatilesUsingColor(int paletteId, int colorId, const Tileset *pairedTileset) const;

    static constexpr int maxPalettes() { return 16; }
    static constexpr int numColorsPerPalette() { return 16; }

private:
    QList<Metatile*> m_metatiles;
    QList<Metatile*> m_porytiles;

    QList<QImage> m_tiles;
    QImage m_tilesImage;
    bool m_hasUnsavedTilesImage = false;
};

#endif // TILESET_H
