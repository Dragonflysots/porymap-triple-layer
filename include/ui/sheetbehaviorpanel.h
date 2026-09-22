#ifndef SHEETBEHAVIORPANEL_H
#define SHEETBEHAVIORPANEL_H

#include <QWidget>
#include <QMap>
#include <QSet>

class QListWidget;
class QLineEdit;
class QLabel;
class QVBoxLayout;
class QPushButton;

// CUSTOM ENGINE: the right-hand column of a Behavior page (Metatiles now, Porytiles later): a filter box, the behavior chooser (0xNN NAME with a
// colour swatch; ONE click gives the behavior to every selected field, there is no Apply button), a line that says what is selected, and a slot
// for the label editor. It knows nothing about the data: the Tileset Editor tells it what is selected and does what behaviorChosen asks for.
class SheetBehaviorPanel : public QWidget {
    Q_OBJECT
public:
    explicit SheetBehaviorPanel(QWidget *parent = nullptr);

    void setBehaviors(const QMap<int, QString> &valueToName);
    // How many fields are selected and which behavior values they carry. All the same value: that behavior is highlighted in the list.
    void setSelection(int fieldCount, const QSet<int> &values, const QString &unitName);
    void setLabelWidget(QWidget *labelEditor);     // put under the list (reparented)
    QListWidget *list() const { return this->behaviorList; }
    QLineEdit *filterEdit() const { return this->filter; }
    QLabel *infoLabel() const { return this->info; }
    QString nameOf(int value) const { return this->names.value(value); }
    QPushButton *clearButton() const { return this->clearBehaviorButton; }
    // The name of the key Qt calls Control: "Cmd" on a Mac (Qt swaps Ctrl and Cmd there), "Ctrl" everywhere else. For the help texts.
    static QString cmdKeyName();

signals:
    void behaviorChosen(int value);
    void clearBehaviorRequested();   // the "Clear Behavior" button: the selected fields lose their behavior (their tiles and labels stay)

private:
    QMap<int, QString> names;
    QLineEdit *filter = nullptr;
    QListWidget *behaviorList = nullptr;
    QLabel *info = nullptr;
    QVBoxLayout *column = nullptr;
    QPushButton *clearBehaviorButton = nullptr;
    void applyFilter(const QString &text);
};

#endif // SHEETBEHAVIORPANEL_H
