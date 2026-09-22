#ifndef PREMAP_H
#define PREMAP_H

// CUSTOM ENGINE: the "pre-map" -- a map's layer-based, editor-only working state, painted with Porytiles. Three layers (Bottom / Middle / Top =
// BG3 / BG2 / BG1), each a grid of porytile ids the size of the map in 16x16 fields; 0 = Porytile 0, the default fill (there are no truly empty
// fields). A fourth grid holds the BEHAVIOR of every field: kAutoBehavior = "Auto", the field takes the behavior of its porytiles (the topmost
// with a behavior wins); any other value was placed by hand on the Behaviors tab and wins over the porytiles, like an elevation on the finished
// map. Saved as data/layers/<layoutId>/layers.json (version 2). Never read by pokeemerald/make -- only "Write to Finalmap" (not built yet)
// resolves the three porytiles and the behavior of a field into a real metatile in map.bin / metatiles.bin. Save writes only this file, never
// the ROM-facing data.

#include <QString>
#include <QVector>
#include <QSize>
#include <QMargins>
#include <cstdint>

class PreMap
{
public:
    static constexpr int kLayers = 3;
    static constexpr int kBehaviorLayer = 3;                // the behavior grid, addressed like a layer by at() / set() (strokes, undo)
    static constexpr uint16_t kAutoBehavior = 0xFFFF;       // "Auto": the field has no placed behavior, its porytiles decide
    static constexpr int kFileVersion = 2;

    // ---- the grids --------------------------------------------------------------------------------------------------------------------------
    QSize size() const { return m_size; }
    int width() const { return m_size.width(); }
    int height() const { return m_size.height(); }
    bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < m_size.width() && y < m_size.height(); }
    // layer 0..2 = a porytile id; layer kBehaviorLayer = the placed behavior (kAutoBehavior = none)
    uint16_t at(int layer, int x, int y) const { return contains(x, y) ? cells(layer).at(y * m_size.width() + x) : (layer == kBehaviorLayer ? kAutoBehavior : 0); }
    void set(int layer, int x, int y, uint16_t value) { if (contains(x, y)) cells(layer)[y * m_size.width() + x] = value; }
    const QVector<uint16_t> &layerCells(int layer) const { return cells(layer); }
    uint16_t behaviorAt(int x, int y) const { return at(kBehaviorLayer, x, y); }
    bool hasPlacedBehavior(int x, int y) const { return behaviorAt(x, y) != kAutoBehavior; }
    void setBehavior(int x, int y, uint16_t value) { set(kBehaviorLayer, x, y, value); }
    int placedBehaviorCount() const;
    void reset(const QSize &size);   // every layer all Porytile 0, every behavior Auto

    // CUSTOM ENGINE: marks a layer for real GBA alpha blending (BLDALPHA hardware register) -- light/shadow effects etc. Purely a stored flag
    // for now; the actual GBA-side implementation is handled separately later (not wired to anything yet, including Write to Finalmap).
    bool bottomAlpha = false;
    bool middleAlpha = false;
    bool topAlpha = false;
    bool &alphaFlag(int layerIndex); // 0=bottom, 1=middle, 2=top

    // ---- whole-map operations (Shift tool, Change Dimensions), undone through snapshots --------------------------------------------------------
    struct Snapshot {
        QSize size;
        QVector<uint16_t> cells[kLayers + 1];   // the three layers and the behaviors
    };
    Snapshot snapshot() const;
    void restore(const Snapshot &snapshot);
    // Moves every layer (and the behaviors) by (dx, dy) fields. Nothing wraps around: what leaves the map is gone, what comes in is Porytile 0 / Auto.
    void shiftAll(int dx, int dy);
    // Change Dimensions: the map grows / shrinks by the margins (a positive left / top margin moves the content right / down).
    void resize(const QSize &newSize, const QMargins &margins);

    // ---- the file -------------------------------------------------------------------------------------------------------------------------
    // What load() had to do: an older layers.json of the Map Object days was set aside (its path), or the grid in the file did not have the
    // layout's size and was cropped / extended.
    struct LoadReport {
        QString oldFormatAside;
        bool sizeAdjusted = false;
    };
    const LoadReport &lastLoadReport() const { return this->loadReport; }

    void load(const QString &layoutId, const QSize &layoutSize);
    bool save(const QString &layoutId); // false = the file could not be written
    static QString filepathFor(const QString &layoutId);

private:
    static int clampLayer(int layer) { return layer < 0 ? 0 : (layer >= kLayers ? kLayers - 1 : layer); }
    QVector<uint16_t> &cells(int layer) { return layer == kBehaviorLayer ? m_behaviors : m_cells[clampLayer(layer)]; }
    const QVector<uint16_t> &cells(int layer) const { return layer == kBehaviorLayer ? m_behaviors : m_cells[clampLayer(layer)]; }
    QSize m_size;
    QVector<uint16_t> m_cells[kLayers];
    QVector<uint16_t> m_behaviors;
    LoadReport loadReport;
};

#endif // PREMAP_H
