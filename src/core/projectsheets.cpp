#include "projectsheets.h"
#include "behaviorcolor.h"
#include "log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>

namespace ProjectSheets {

QString elevationSheetPath() { return QStringLiteral("graphics/porymap/elevation_sheet.png"); }
QString behaviorSheetPath()  { return QStringLiteral("graphics/porymap/behavior_sheet.png"); }

// The 8 elevation icons are taken from Porymap's built-in vanilla collision sheet (its white,
// "passable" column): vanilla row N shows elevation N -- arrows for 0 (transition between
// elevations), waves for 1 (surf), the digits 2-6, and the four-way arrow on row 15 for
// multi-level. The custom engine's 8 values map onto those rows.
QImage renderElevationSheet() {
    const QImage vanilla = QImage(QStringLiteral(":/images/collisions.png")).convertToFormat(QImage::Format_ARGB32);
    static const int vanillaRow[8] = { 0, 1, 2, 3, 4, 5, 6, 15 };

    QImage sheet(cellSize, cellSize * 8, QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    QPainter painter(&sheet);
    for (int elevation = 0; elevation < 8; elevation++)
        painter.drawImage(0, elevation * cellSize, vanilla, 0, vanillaRow[elevation] * cellSize, cellSize, cellSize);
    painter.end();
    return sheet;
}

// Classic 5x7 pixel font for the hex digits, one row per string ('#' = ink). Drawn by hand so the
// sheet looks the same on every machine and never depends on an installed font.
static const char *const glyphs[16][7] = {
    { ".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###." }, // 0
    { "..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###." }, // 1
    { ".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####" }, // 2
    { "####.", "....#", "....#", ".###.", "....#", "....#", "####." }, // 3
    { "...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#." }, // 4
    { "#####", "#....", "####.", "....#", "....#", "#...#", ".###." }, // 5
    { "..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###." }, // 6
    { "#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..." }, // 7
    { ".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###." }, // 8
    { ".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.." }, // 9
    { ".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#" }, // A
    { "####.", "#...#", "#...#", "####.", "#...#", "#...#", "####." }, // B
    { ".###.", "#...#", "#....", "#....", "#....", "#...#", ".###." }, // C
    { "###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.." }, // D
    { "#####", "#....", "#....", "####.", "#....", "#....", "#####" }, // E
    { "#####", "#....", "#....", "####.", "#....", "#....", "#...." }, // F
};

static void drawGlyph(QImage &image, int originX, int originY, int digit, QRgb ink) {
    for (int y = 0; y < 7; y++)
        for (int x = 0; x < 5; x++)
            if (glyphs[digit][y][x] == '#')
                image.setPixel(originX + x, originY + y, ink);
}

// One 16x16 cell: the behavior's colour, its two hex digits on top (a 5x7 font each, one pixel apart, centred).
static void paintBehaviorCell(QImage &sheet, int cellX, int cellY, int id, int fillAlpha) {
    const QColor tint = BehaviorColor::forId(id);
    QColor fill = tint;
    fill.setAlpha(fillAlpha);
    QPainter painter(&sheet);   // (the default SourceOver, exactly like the generator of the older builds: the byte-for-byte comparison of the legacy sheet needs the same rounding)
    painter.fillRect(QRect(cellX, cellY, cellSize, cellSize), fill);
    painter.end();
    const QRgb ink = BehaviorColor::inkFor(tint).rgb();
    drawGlyph(sheet, cellX + 2, cellY + 4, (id >> 4) & 0xF, ink);
    drawGlyph(sheet, cellX + 9, cellY + 4, id & 0xF, ink);
}

static QImage renderBehaviorSheetWith(int fillAlpha, bool withCellZero) {
    const int columns = behaviorColumns;
    QImage sheet(columns * cellSize, columns * cellSize, QImage::Format_ARGB32);
    sheet.fill(Qt::transparent);
    for (int id = withCellZero ? 0 : 1; id < columns * columns; id++)
        paintBehaviorCell(sheet, (id % columns) * cellSize, (id / columns) * cellSize, id, fillAlpha);
    return sheet;
}

QImage renderBehaviorSheet() { return renderBehaviorSheetWith(255, true); }
QImage renderLegacyBehaviorSheet() { return renderBehaviorSheetWith(200, false); }

QImage renderBehaviorCell(int behaviorId) {
    QImage cell(cellSize, cellSize, QImage::Format_ARGB32);
    cell.fill(Qt::transparent);
    if (behaviorId < 0 || behaviorId > 255) {   // (no such behavior on the sheet: a plain magenta cell without digits, never the look of 0xFF)
        cell.fill(QColor(255, 0, 255));
        return cell;
    }
    paintBehaviorCell(cell, 0, 0, behaviorId, 255);
    return cell;
}

// Writes a PNG so that it is either complete or not there: into a temporary file that replaces the target only when everything was written
// (QImage::save on the path would truncate the file first, and a full disk or a kill would leave a broken PNG that is never repaired).
static bool writePngAtomically(const QImage &image, const QString &absolutePath) {
    QSaveFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

static QString ensureSheet(const QString &projectDir, const QString &relativePath, const QImage &image) {
    const QString absolutePath = QDir(projectDir).filePath(relativePath);
    if (QFile::exists(absolutePath))
        return relativePath; // never overwrite: the user may have edited it
    QDir().mkpath(QFileInfo(absolutePath).absolutePath());
    if (!writePngAtomically(image, absolutePath)) {
        logWarn(QString("Could not write the generated image '%1'.").arg(absolutePath));
        return QString();
    }
    logInfo(QString("Generated '%1'.").arg(absolutePath));
    return relativePath;
}

QString ensureElevationSheet(const QString &projectDir) {
    return ensureSheet(projectDir, elevationSheetPath(), renderElevationSheet());
}

QString ensureBehaviorSheet(const QString &projectDir) {
    const QString absolutePath = QDir(projectDir).filePath(behaviorSheetPath());
    // A sheet that an earlier build generated and that is still exactly as generated (nobody edited it) is replaced by the current one:
    // its cell 0x00 is transparent, and 0x00 must be seen like every other behavior. Anything else is the user's and stays.
    if (QFile::exists(absolutePath)) {
        const QImage existing = QImage(absolutePath).convertToFormat(QImage::Format_ARGB32);
        if (!existing.isNull() && existing == renderLegacyBehaviorSheet()) {
            if (writePngAtomically(renderBehaviorSheet(), absolutePath))
                logInfo(QString("Replaced the generated behavior sheet '%1' by the current one (it now shows 0x00 too).").arg(absolutePath));
            else
                logWarn(QString("Could not update the generated behavior sheet '%1'.").arg(absolutePath));
        }
    }
    return ensureSheet(projectDir, behaviorSheetPath(), renderBehaviorSheet());
}

QImage behaviorCell(const QImage &sheet, int behaviorId) {
    const int x = (behaviorId % behaviorColumns) * cellSize;
    const int y = (behaviorId / behaviorColumns) * cellSize;
    if (behaviorId < 0 || x + cellSize > sheet.width() || y + cellSize > sheet.height())
        return behaviorId == 0 ? renderBehaviorCell(0) : QImage();
    QImage cell = sheet.copy(x, y, cellSize, cellSize);
    if (behaviorId == 0) {   // a sheet without a 0x00 cell (an edited old one): the built-in cell, so 0x00 is always seen
        bool empty = true;
        const QImage argb = cell.convertToFormat(QImage::Format_ARGB32);
        for (int py = 0; py < argb.height() && empty; py++)
            for (int px = 0; px < argb.width(); px++)
                if (qAlpha(argb.pixel(px, py)) > 0) { empty = false; break; }
        if (empty)
            return renderBehaviorCell(0);
    }
    return cell;
}

} // namespace ProjectSheets
