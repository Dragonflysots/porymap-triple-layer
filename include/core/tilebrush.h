#pragma once
#ifndef TILEBRUSH_H
#define TILEBRUSH_H

#include "tile.h"
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <functional>

// CUSTOM ENGINE: what a click on the Tileset Editor's sheets stamps: a rectangle of complete tile entries (tile id, palette and both
// flip bits). It comes from the source tile sheet (with the current palette / flips), or verbatim from a right-click / right-drag pick
// on a sheet. Pure data, no GUI: everything the paint code has to get right (mirroring, cropping, clipping) is testable on its own.
class TileBrush {
public:
    // A brush is at most as wide as the source tile sheet (16 tiles = 8 metatile fields).
    static constexpr int kMaxCols = 16;

    TileBrush() = default;
    // rowMajor holds cols*rows entries (missing ones are blank tiles). More than kMaxCols columns are cropped GEOMETRICALLY (the columns
    // on the right go; every remaining tile keeps its own row and column).
    TileBrush(int cols, int rows, const QList<Tile> &rowMajor);

    bool isNull() const { return this->numCols <= 0 || this->numRows <= 0; }
    int cols() const { return this->numCols; }
    int rows() const { return this->numRows; }
    QSize size() const { return QSize(this->numCols, this->numRows); }
    Tile at(int col, int row) const;
    const QList<Tile> &tiles() const { return this->entries; }

    // Mirrors the whole arrangement AND toggles each tile's own flip bit, so an already flipped tile becomes unflipped: flips are
    // relative to what the brush holds. Flipping twice gives the brush back.
    TileBrush flipped(bool x, bool y) const;
    TileBrush withPalette(int palette) const;
    TileBrush cropped(int maxCols, int maxRows) const;   // top-left anchored

    // A rectangle of the source sheet (tile id = y * sheetCols + x), every tile in the given palette, no flips.
    static TileBrush fromSourceRect(const QRect &tileRect, int sheetCols, int palette);
    // A rectangle read from a sheet, verbatim: id, palette and both flips as they are. tileAt is asked for every (x, y) of the rectangle.
    static TileBrush fromGrid(const QRect &tileRect, const std::function<Tile(int, int)> &tileAt);
    static TileBrush single(const Tile &tile);

    // The stored form of a tile: tile id 0 (the blank tile; colour 0 is transparent) is always the raw word 0x0000, whatever palette
    // or flips it came with, so "blank" has ONE representation (and painting tile 0 is the eraser).
    static Tile normalizedForStore(const Tile &tile);

    struct StampWrite {
        QPoint tile;   // destination tile
        int col = 0;   // where in the brush the value comes from
        int row = 0;
        Tile value;    // normalizedForStore() of the brush entry
    };
    // The tiles a stamp with its top-left corner on originTile writes: one entry for every brush tile whose destination isValid().
    // Always addressed by (col, row) -- never by a running counter, so a clipped tile cannot shift the ones after it.
    QList<StampWrite> plan(const QPoint &originTile, const std::function<bool(const QPoint &)> &isValid) const;

    bool operator==(const TileBrush &other) const { return this->numCols == other.numCols && this->numRows == other.numRows && this->entries == other.entries; }
    bool operator!=(const TileBrush &other) const { return !(*this == other); }

private:
    int numCols = 0;
    int numRows = 0;
    QList<Tile> entries;
};

#endif // TILEBRUSH_H
