#ifndef SHEETCONTROLSLEGEND_H
#define SHEETCONTROLSLEGEND_H

#include <QWidget>
#include <QPixmap>
#include <QStringList>

class QGridLayout;

// CUSTOM ENGINE: the legend of mouse and key controls that sits beside the sheets of the Tileset Editor. Every entry is a small row of pictograms
// (a mouse with the pressed button lit, key caps such as Cmd, Shift, Esc) followed by a text; a note is a whole line of text. The pictograms are
// drawn here, there are no image files. The rows have a fixed height and their texts are shortened with "..." when the area is narrow (the tooltip of
// an entry always has the full sentence), so the legend never changes the size of the window or of its surroundings.
class SheetControlsLegend : public QWidget {
    Q_OBJECT
public:
    // `columns` entries share a row; a note always takes a row of its own.
    explicit SheetControlsLegend(int columns, QWidget *parent = nullptr);

    // `pieces` are what the pictograms show, in order: "L" / "R" = the left / right mouse button (lit), "+" and "/" = the plain sign between two
    // pictograms, anything else = a key cap with that text ("Cmd", "Shift", "Esc", "Del").
    void addEntry(const QStringList &pieces, const QString &text, const QString &tooltip);
    void addNote(const QString &text, const QString &tooltip = QString());

    int entryCount() const { return this->entryTexts_.size(); }
    QStringList entryTexts() const { return this->entryTexts_; }
    QStringList noteTexts() const { return this->noteTexts_; }

    static QPixmap mouseIcon(bool leftLit, bool rightLit, qreal devicePixelRatio, const QPalette &palette);
    static QPixmap keyCap(const QString &label, qreal devicePixelRatio, const QPalette &palette, const QFont &font);

    static constexpr int kRowHeight = 22;

private:
    QGridLayout *grid = nullptr;
    int columns = 1;
    int nextCell = 0;                 // the next free cell, counted row by row
    QStringList entryTexts_;
    QStringList noteTexts_;
};

#endif // SHEETCONTROLSLEGEND_H
