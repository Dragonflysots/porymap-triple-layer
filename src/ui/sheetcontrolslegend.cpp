#include "sheetcontrolslegend.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>

namespace {

// A one-line text that is cut off with "..." instead of asking for more room.
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return QSize(24, QLabel::sizeHint().height()); }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.setFont(font());
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};

QPixmap newPixmap(const QSizeF &size, qreal dpr) {
    QPixmap pixmap(QSize(qCeil(size.width() * dpr), qCeil(size.height() * dpr)));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    return pixmap;
}

} // namespace

SheetControlsLegend::SheetControlsLegend(int columns, QWidget *parent) : QWidget(parent), columns(qMax(1, columns)) {
    this->grid = new QGridLayout(this);
    this->grid->setContentsMargins(0, 0, 0, 0);
    this->grid->setHorizontalSpacing(14);
    this->grid->setVerticalSpacing(2);
    for (int c = 0; c < this->columns; c++)
        this->grid->setColumnStretch(c, 1);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QPixmap SheetControlsLegend::mouseIcon(bool leftLit, bool rightLit, qreal dpr, const QPalette &palette) {
    const qreal w = 15, h = 20;
    QPixmap pixmap = newPixmap(QSizeF(w, h), dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF body(0.75, 0.75, w - 1.5, h - 1.5);
    QPainterPath outline;
    outline.addRoundedRect(body, 6.5, 6.5);
    const qreal split = w / 2, buttonsBottom = 8.5;
    painter.save();
    painter.setClipPath(outline);
    painter.fillRect(QRectF(0, 0, w, h), palette.color(QPalette::Base));
    const QColor lit = palette.color(QPalette::Highlight);
    if (leftLit)
        painter.fillRect(QRectF(0, 0, split, buttonsBottom), lit);
    if (rightLit)
        painter.fillRect(QRectF(split, 0, split, buttonsBottom), lit);
    painter.restore();
    painter.setPen(QPen(palette.color(QPalette::Dark), 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
    painter.drawLine(QPointF(split, 0.75), QPointF(split, buttonsBottom));
    painter.drawLine(QPointF(0.75, buttonsBottom), QPointF(w - 0.75, buttonsBottom));
    return pixmap;
}

QPixmap SheetControlsLegend::keyCap(const QString &label, qreal dpr, const QPalette &palette, const QFont &font) {
    const QFontMetricsF metrics(font);
    const qreal w = qMax<qreal>(18, metrics.horizontalAdvance(label) + 10), h = 18;
    QPixmap pixmap = newPixmap(QSizeF(w, h), dpr);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette.color(QPalette::Dark), 1));
    painter.setBrush(palette.color(QPalette::Base));
    painter.drawRoundedRect(QRectF(0.5, 0.5, w - 1, h - 2.5), 4, 4);
    painter.setPen(QPen(palette.color(QPalette::Mid), 1.5));                       // (the edge of a key cap)
    painter.drawLine(QPointF(4, h - 1), QPointF(w - 4, h - 1));
    painter.setFont(font);
    painter.setPen(palette.color(QPalette::Text));
    painter.drawText(QRectF(0, 0, w, h - 2), Qt::AlignCenter, label);
    return pixmap;
}

void SheetControlsLegend::addEntry(const QStringList &pieces, const QString &text, const QString &tooltip) {
    auto *chip = new QWidget(this);
    chip->setObjectName(QStringLiteral("legendEntry"));
    chip->setFixedHeight(kRowHeight);
    chip->setToolTip(tooltip);
    auto *row = new QHBoxLayout(chip);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(3);
    const qreal dpr = devicePixelRatioF();
    QFont capFont = font();
    capFont.setPointSizeF(qMax(8.0, capFont.pointSizeF() - 3.0));
    capFont.setBold(true);
    for (const QString &piece : pieces) {
        auto *label = new QLabel(chip);
        if (piece == "L" || piece == "R") {
            label->setObjectName(piece == "L" ? QStringLiteral("legendMouseLeft") : QStringLiteral("legendMouseRight"));
            label->setPixmap(mouseIcon(piece == "L", piece == "R", dpr, palette()));
        } else if (piece == "+" || piece == "/") {
            label->setText(piece);
        } else {
            label->setObjectName(QStringLiteral("legendKey"));
            label->setPixmap(keyCap(piece, dpr, palette(), capFont));
            label->setProperty("key", piece);
        }
        row->addWidget(label);
    }
    auto *textLabel = new ElidedLabel(text, chip);
    textLabel->setObjectName(QStringLiteral("legendText"));
    textLabel->setTextFormat(Qt::PlainText);
    textLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addSpacing(3);
    row->addWidget(textLabel, 1);
    this->grid->addWidget(chip, this->nextCell / this->columns, this->nextCell % this->columns);
    this->nextCell++;
    this->entryTexts_.append(text);
}

void SheetControlsLegend::addNote(const QString &text, const QString &tooltip) {
    if (this->nextCell % this->columns != 0)
        this->nextCell += this->columns - this->nextCell % this->columns;   // (a note starts on a new row)
    auto *label = new ElidedLabel(text, this);
    label->setObjectName(QStringLiteral("legendNote"));
    label->setTextFormat(Qt::PlainText);
    label->setToolTip(tooltip.isEmpty() ? text : tooltip);
    label->setFixedHeight(kRowHeight);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QFont italic = label->font();
    italic.setItalic(true);
    label->setFont(italic);
    this->grid->addWidget(label, this->nextCell / this->columns, 0, 1, this->columns);
    this->nextCell += this->columns;
    this->noteTexts_.append(text);
}
