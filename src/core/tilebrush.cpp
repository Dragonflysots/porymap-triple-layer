#include "tilebrush.h"

TileBrush::TileBrush(int cols, int rows, const QList<Tile> &rowMajor) {
    if (cols <= 0 || rows <= 0)
        return;
    this->numCols = qMin(cols, kMaxCols);
    this->numRows = rows;
    this->entries.reserve(this->numCols * this->numRows);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < this->numCols; c++)
            this->entries.append(rowMajor.value(r * cols + c));   // (row stride is the ORIGINAL width)
}

Tile TileBrush::at(int col, int row) const {
    if (col < 0 || row < 0 || col >= this->numCols || row >= this->numRows)
        return Tile();
    return this->entries.at(row * this->numCols + col);
}

TileBrush TileBrush::flipped(bool x, bool y) const {
    if (isNull() || (!x && !y))
        return *this;
    TileBrush result;
    result.numCols = this->numCols;
    result.numRows = this->numRows;
    result.entries.reserve(this->entries.size());
    for (int r = 0; r < this->numRows; r++) {
        for (int c = 0; c < this->numCols; c++) {
            Tile tile = at(x ? this->numCols - 1 - c : c, y ? this->numRows - 1 - r : r);
            tile.xflip ^= x;
            tile.yflip ^= y;
            result.entries.append(tile);
        }
    }
    return result;
}

TileBrush TileBrush::withPalette(int palette) const {
    TileBrush result = *this;
    for (Tile &tile : result.entries)
        tile.palette = palette;
    return result;
}

TileBrush TileBrush::cropped(int maxCols, int maxRows) const {
    maxCols = qMax(1, maxCols);
    maxRows = qMax(1, maxRows);
    if (isNull() || (this->numCols <= maxCols && this->numRows <= maxRows))
        return *this;
    const int cols = qMin(this->numCols, maxCols), rows = qMin(this->numRows, maxRows);
    QList<Tile> kept;
    kept.reserve(cols * rows);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++)
            kept.append(at(c, r));
    return TileBrush(cols, rows, kept);
}

TileBrush TileBrush::fromSourceRect(const QRect &tileRect, int sheetCols, int palette) {
    QList<Tile> tiles;
    for (int y = tileRect.top(); y <= tileRect.bottom(); y++)
        for (int x = tileRect.left(); x <= tileRect.right(); x++)
            tiles.append(Tile(static_cast<uint16_t>(y * sheetCols + x), false, false, static_cast<uint16_t>(palette)));
    return TileBrush(tileRect.width(), tileRect.height(), tiles);
}

TileBrush TileBrush::fromGrid(const QRect &tileRect, const std::function<Tile(int, int)> &tileAt) {
    QList<Tile> tiles;
    for (int y = tileRect.top(); y <= tileRect.bottom(); y++)
        for (int x = tileRect.left(); x <= tileRect.right(); x++)
            tiles.append(tileAt(x, y));
    return TileBrush(tileRect.width(), tileRect.height(), tiles);
}

TileBrush TileBrush::single(const Tile &tile) {
    return TileBrush(1, 1, { tile });
}

Tile TileBrush::normalizedForStore(const Tile &tile) {
    return tile.tileId == 0 ? Tile() : tile;
}

QList<TileBrush::StampWrite> TileBrush::plan(const QPoint &originTile, const std::function<bool(const QPoint &)> &isValid) const {
    QList<StampWrite> writes;
    for (int r = 0; r < this->numRows; r++) {
        for (int c = 0; c < this->numCols; c++) {
            const QPoint destination(originTile.x() + c, originTile.y() + r);
            if (isValid && !isValid(destination))
                continue;
            StampWrite write;
            write.tile = destination;
            write.col = c;
            write.row = r;
            write.value = normalizedForStore(at(c, r));
            writes.append(write);
        }
    }
    return writes;
}
