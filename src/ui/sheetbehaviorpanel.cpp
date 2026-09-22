#include "sheetbehaviorpanel.h"
#include "behaviorcolor.h"
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPixmap>
#include <QPainter>
#include <algorithm>

QString SheetBehaviorPanel::cmdKeyName() {
#ifdef Q_OS_MACOS
    return QStringLiteral("Cmd");
#else
    return QStringLiteral("Ctrl");
#endif
}

SheetBehaviorPanel::SheetBehaviorPanel(QWidget *parent) : QWidget(parent) {
    this->column = new QVBoxLayout(this);
    this->column->setContentsMargins(0, 0, 0, 0);

    this->info = new QLabel(this);
    this->info->setObjectName(QStringLiteral("label_BehaviorSelectionInfo"));
    this->info->setWordWrap(true);
    this->column->addWidget(this->info);

    this->filter = new QLineEdit(this);
    this->filter->setObjectName(QStringLiteral("lineEdit_BehaviorFilter"));
    this->filter->setPlaceholderText(QStringLiteral("Filter the list: name or hex ID (grass, 1A)"));
    this->filter->setToolTip(QStringLiteral("Narrows the list of behaviors down. Type part of a name (grass) or a hex ID (1A or 0x1A)."));
    this->filter->setClearButtonEnabled(true);
    this->column->addWidget(this->filter);

    this->behaviorList = new QListWidget(this);
    this->behaviorList->setObjectName(QStringLiteral("listWidget_BehaviorChooser"));
    this->behaviorList->setMinimumWidth(190);
    this->behaviorList->setToolTip(QStringLiteral("Click a behavior: every selected field gets it at once. MB_NORMAL (0x00) takes a behavior away."));
    this->column->addWidget(this->behaviorList, 1);

    // (the label editor is put in here by setLabelWidget, above this button)
    this->clearBehaviorButton = new QPushButton(QStringLiteral("Clear Behavior"), this);
    this->clearBehaviorButton->setObjectName(QStringLiteral("pushButton_BehaviorClearBehavior"));
    this->clearBehaviorButton->setToolTip(QStringLiteral("Takes the behavior away from the selected fields (MB_NORMAL). Their tiles and labels stay; one undo step.\n"
                                                         "To empty a field completely (all layers, behavior and label) use Clear Field on the Paint page."));
    this->column->addWidget(this->clearBehaviorButton);
    connect(this->clearBehaviorButton, &QPushButton::clicked, this, &SheetBehaviorPanel::clearBehaviorRequested);

    connect(this->filter, &QLineEdit::textChanged, this, &SheetBehaviorPanel::applyFilter);
    auto choose = [this](QListWidgetItem *item) { if (item) emit behaviorChosen(item->data(Qt::UserRole).toInt()); };
    connect(this->behaviorList, &QListWidget::itemClicked, this, choose);
    connect(this->behaviorList, &QListWidget::itemActivated, this, choose);
    setSelection(0, {}, QStringLiteral("field"));
}

void SheetBehaviorPanel::setBehaviors(const QMap<int, QString> &valueToName) {
    this->names = valueToName;
    this->behaviorList->clear();
    for (auto it = valueToName.constBegin(); it != valueToName.constEnd(); ++it) {
        const QString digits = QString("%1").arg(it.key(), 2, 16, QChar('0')).toUpper();
        auto *item = new QListWidgetItem(QString("0x%1  %2").arg(digits, it.value()), this->behaviorList);
        item->setData(Qt::UserRole, it.key());
        QPixmap swatch(14, 14);   // (0x00 has its colour too: the sheets show it)
        swatch.fill(BehaviorColor::forId(it.key()));
        item->setIcon(QIcon(swatch));
    }
    applyFilter(this->filter->text());
}

void SheetBehaviorPanel::applyFilter(const QString &text) {
    const QString needle = text.trimmed();
    for (int i = 0; i < this->behaviorList->count(); i++) {
        QListWidgetItem *item = this->behaviorList->item(i);
        item->setHidden(!needle.isEmpty() && !item->text().contains(needle, Qt::CaseInsensitive));
    }
}

void SheetBehaviorPanel::setSelection(int fieldCount, const QSet<int> &values, const QString &unitName) {
    if (fieldCount <= 0) {
        this->info->setText(QString("No %1 selected. Left-click one, or Left-drag a rectangle (%2+Left-click and the Right mouse button add more).").arg(unitName, cmdKeyName()));
    } else {
        QStringList shown;
        QList<int> sorted(values.begin(), values.end());
        std::sort(sorted.begin(), sorted.end());
        for (int value : sorted)
            shown << (this->names.contains(value) ? this->names.value(value) : QString("0x%1").arg(value, 2, 16, QChar('0')));
        this->info->setText(QString("%1 %2%3 selected. %4: %5")
                                .arg(fieldCount).arg(unitName).arg(fieldCount == 1 ? "" : "s")
                                .arg(sorted.size() > 1 ? "Mixed behaviors" : "Behavior").arg(shown.join(", ")));
    }
    // highlight: all the same -> that entry; mixed or nothing -> none
    const QSignalBlocker blocker(this->behaviorList);
    this->behaviorList->clearSelection();
    this->behaviorList->setCurrentItem(nullptr);
    if (fieldCount > 0 && values.size() == 1) {
        for (int i = 0; i < this->behaviorList->count(); i++) {
            QListWidgetItem *item = this->behaviorList->item(i);
            if (item->data(Qt::UserRole).toInt() == *values.begin()) {
                item->setSelected(true);
                this->behaviorList->scrollToItem(item);
                break;
            }
        }
    }
}

void SheetBehaviorPanel::setLabelWidget(QWidget *labelEditor) {
    if (!labelEditor)
        return;
    labelEditor->setParent(this);
    this->column->insertWidget(this->column->count() - 1, labelEditor);   // (above the Clear Behavior button)
    labelEditor->show();
}
