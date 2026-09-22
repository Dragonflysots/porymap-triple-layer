#ifndef PROJECTSHEETS_H
#define PROJECTSHEETS_H

// CUSTOM ENGINE: icon sheets that Porymap generates INTO the project folder (graphics/porymap/).
// A sheet is only ever written when it is missing, so an image the user edited by hand is never
// overwritten -- delete the file to get a fresh one.
//
//  * elevation sheet: 1 column x 8 rows of 16x16 icons, row = elevation value. Replaces the
//    vanilla "collision" sheet (2 columns x 16 rows) now that map.bin has no collision bits and
//    only 8 elevation values. Loaded through the existing collision_sheet_path project setting.
//  * behavior sheet: 16 x 16 cells of 16x16 icons, cell (id % 16, id / 16) = metatile behavior id
//    (0x00-0xFF). Each cell is the behavior's color (BehaviorColor::forId) with its two hex digits
//    on top -- cell 0x00 (MB_NORMAL) included: it is a slate colour with "00", not transparent. Used by the main window's
//    Behaviors tab. A sheet that an earlier build generated (transparent 0x00 cell, translucent fill) and that nobody edited is
//    replaced by the current one when the project opens; an edited sheet is never touched, and a cell 0x00 that such a sheet
//    leaves empty is drawn by behaviorCell() from the built-in one.

#include <QImage>
#include <QString>

namespace ProjectSheets {

constexpr int cellSize = 16;     // one icon = one metatile
constexpr int behaviorColumns = 16;

QString elevationSheetPath();    // project-relative, e.g. "graphics/porymap/elevation_sheet.png"
QString behaviorSheetPath();

QImage renderElevationSheet();
QImage renderBehaviorSheet();                 // the current sheet: every cell opaque (the Behaviors tab's Opacity slider does the fading), 0x00 included
QImage renderLegacyBehaviorSheet();           // what earlier builds generated (translucent fill, no cell 0x00) -- only to recognise an unedited old file
QImage renderBehaviorCell(int behaviorId);    // one built-in cell

// Writes the sheet under projectDir unless a file is already there. Returns the project-relative
// path, or an empty string if it could not be written.
QString ensureElevationSheet(const QString &projectDir);
QString ensureBehaviorSheet(const QString &projectDir);

// The icon of one behavior id out of a behavior sheet (null image if the sheet has no such cell).
QImage behaviorCell(const QImage &sheet, int behaviorId);

} // namespace ProjectSheets

#endif // PROJECTSHEETS_H
