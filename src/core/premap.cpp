#include "core/premap.h"
#include "parseutil.h"
#include "project.h"
#include "tileset.h"
#include "config.h"
#include "log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

bool &PreMap::alphaFlag(int layerIndex) {
    switch (layerIndex) {
        case 0: return this->bottomAlpha;
        case 2: return this->topAlpha;
        default: return this->middleAlpha;
    }
}

void PreMap::reset(const QSize &size) {
    m_size = QSize(qMax(0, size.width()), qMax(0, size.height()));
    for (int layer = 0; layer < kLayers; layer++)
        m_cells[layer] = QVector<uint16_t>(m_size.width() * m_size.height(), 0);
    m_behaviors = QVector<uint16_t>(m_size.width() * m_size.height(), kAutoBehavior);
}

int PreMap::placedBehaviorCount() const {
    int n = 0;
    for (uint16_t value : m_behaviors)
        if (value != kAutoBehavior) n++;
    return n;
}

PreMap::Snapshot PreMap::snapshot() const {
    Snapshot s;
    s.size = m_size;
    for (int layer = 0; layer <= kLayers; layer++)
        s.cells[layer] = cells(layer);
    return s;
}

void PreMap::restore(const Snapshot &snapshot) {
    m_size = snapshot.size;
    for (int layer = 0; layer <= kLayers; layer++) {
        cells(layer) = snapshot.cells[layer];
        cells(layer).resize(m_size.width() * m_size.height());   // (a snapshot always fits; this only guards against a hand-made one)
    }
}

void PreMap::shiftAll(int dx, int dy) {
    if (dx == 0 && dy == 0)
        return;
    for (int layer = 0; layer <= kLayers; layer++) {
        QVector<uint16_t> moved(m_size.width() * m_size.height(), layer == kBehaviorLayer ? kAutoBehavior : 0);
        for (int y = 0; y < m_size.height(); y++) {
            const int sourceY = y - dy;
            if (sourceY < 0 || sourceY >= m_size.height())
                continue;
            for (int x = 0; x < m_size.width(); x++) {
                const int sourceX = x - dx;
                if (sourceX < 0 || sourceX >= m_size.width())
                    continue;
                moved[y * m_size.width() + x] = cells(layer).at(sourceY * m_size.width() + sourceX);
            }
        }
        cells(layer) = moved;
    }
}

void PreMap::resize(const QSize &newSize, const QMargins &margins) {
    const QSize oldSize = m_size;
    QVector<uint16_t> oldCells[kLayers + 1];
    for (int layer = 0; layer <= kLayers; layer++)
        oldCells[layer] = cells(layer);
    reset(newSize);
    for (int layer = 0; layer <= kLayers; layer++) {
        for (int y = 0; y < m_size.height(); y++) {
            const int sourceY = y - margins.top();
            if (sourceY < 0 || sourceY >= oldSize.height())
                continue;
            for (int x = 0; x < m_size.width(); x++) {
                const int sourceX = x - margins.left();
                if (sourceX < 0 || sourceX >= oldSize.width())
                    continue;
                cells(layer)[y * m_size.width() + x] = oldCells[layer].at(sourceY * oldSize.width() + sourceX);
            }
        }
    }
}

QString PreMap::filepathFor(const QString &layoutId) {
    return QString("%1/data/layers/%2/layers.json").arg(projectConfig.projectDir(), layoutId);
}

// The file: { "version": 2, "width": W, "height": H, "layers": { "bottom": [W*H ids], "middle": [...], "top": [...] }, "alpha_channel": {...},
//             "behaviors": [W*H values, -1 = Auto], "behavior_names": { "2": "MB_TALL_GRASS", ... } }.
// The behaviors are stored by value (like metatile attributes) and, for every value in use, by NAME too: when the project renumbers a behavior
// in metatile_behaviors.h the name decides and the value is corrected on load. A file of the Map Object days (no version, "layers" full of
// placements) is set aside as layers.json.mapobjects-<time> and the map starts blank; a file whose grid has another size than the layout is
// cropped / extended (top-left anchored); a file without "behaviors" (written before they existed) has every field on Auto.
void PreMap::load(const QString &layoutId, const QSize &layoutSize) {
    this->loadReport = LoadReport();
    reset(layoutSize);
    // Every early return below must leave a clean state: the flags of the previously shown layout must not leak into this one.
    this->bottomAlpha = this->middleAlpha = this->topAlpha = false;

    const QString filepath = PreMap::filepathFor(layoutId);
    if (!QFile::exists(filepath))
        return;

    ParseUtil parser;
    QJsonDocument doc;
    QString error;
    if (!parser.tryParseJsonFile(&doc, filepath, &error)) {
        logError(QString("Failed to read pre-map layer data from %1: %2").arg(filepath).arg(error));
        // Never overwrite what could not be read: keep it next to the new file so nothing is lost when the next save happens.
        const QString aside = filepath + QStringLiteral(".corrupt");
        QFile::remove(aside);
        if (QFile::rename(filepath, aside))
            logWarn(QString("The unreadable pre-map file was moved to %1").arg(aside));
        return;
    }

    const QJsonObject obj = doc.object();
    if (obj.value("version").toInt(1) < kFileVersion) {
        const QString aside = filepath + QString(".mapobjects-%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss"));
        if (QFile::rename(filepath, aside)) {
            this->loadReport.oldFormatAside = aside;
            logWarn(QString("%1 held Map Objects (an older format). It was moved to %2; the map starts with Porytile 0 everywhere.").arg(filepath, aside));
        }
        return;
    }

    const int fileWidth = obj.value("width").toInt(0), fileHeight = obj.value("height").toInt(0);
    const QJsonObject layers = obj.value("layers").toObject();
    const char *names[kLayers] = { "bottom", "middle", "top" };
    for (int layer = 0; layer < kLayers; layer++) {
        const QJsonArray ids = layers.value(names[layer]).toArray();
        for (int y = 0; y < qMin(fileHeight, m_size.height()); y++)
            for (int x = 0; x < qMin(fileWidth, m_size.width()); x++)
                m_cells[layer][y * m_size.width() + x] = static_cast<uint16_t>(ids.at(y * fileWidth + x).toInt(0));
    }

    // the behaviors: a value in the grid is corrected through its name when the project knows that name under another value
    QHash<int, int> corrected;   // file value -> current value
    const QJsonObject behaviorNames = obj.value("behavior_names").toObject();
    for (auto it = behaviorNames.constBegin(); it != behaviorNames.constEnd(); ++it) {
        bool ok = false;
        const int fileValue = it.key().toInt(&ok);
        const QString name = it.value().toString();
        if (!ok || name.isEmpty())
            continue;
        if (Tileset::behaviorNames.contains(name)) {
            const int current = static_cast<int>(Tileset::behaviorNames.value(name));
            if (current != fileValue) {
                corrected.insert(fileValue, current);
                logWarn(QString("%1: behavior %2 is 0x%3 in the project now (the file had 0x%4); corrected.").arg(filepath, name).arg(current, 2, 16, QChar('0')).arg(fileValue, 2, 16, QChar('0')));
            }
        } else {
            logWarn(QString("%1: the behavior %2 (0x%3) does not exist in the project any more; its fields keep the value.").arg(filepath, name).arg(fileValue, 2, 16, QChar('0')));
        }
    }
    const QJsonArray behaviors = obj.value("behaviors").toArray();
    if (!behaviors.isEmpty()) {
        for (int y = 0; y < qMin(fileHeight, m_size.height()); y++) {
            for (int x = 0; x < qMin(fileWidth, m_size.width()); x++) {
                const int value = behaviors.at(y * fileWidth + x).toInt(-1);
                if (value < 0 || value >= static_cast<int>(kAutoBehavior))
                    continue;   // Auto
                m_behaviors[y * m_size.width() + x] = static_cast<uint16_t>(corrected.value(value, value));
            }
        }
    }

    if (QSize(fileWidth, fileHeight) != m_size) {
        this->loadReport.sizeAdjusted = true;
        logWarn(QString("%1 holds a %2x%3 grid but the layout is %4x%5: cropped / extended.").arg(filepath).arg(fileWidth).arg(fileHeight).arg(m_size.width()).arg(m_size.height()));
    }

    const QJsonObject alpha = obj.value("alpha_channel").toObject();
    this->bottomAlpha = alpha.value("bottom").toBool(false);
    this->middleAlpha = alpha.value("middle").toBool(false);
    this->topAlpha = alpha.value("top").toBool(false);
}

bool PreMap::save(const QString &layoutId) {
    const QString filepath = PreMap::filepathFor(layoutId);
    QDir().mkpath(QFileInfo(filepath).absolutePath());

    QSaveFile file(filepath); // atomic: a crash or a full disk never leaves a half-written layers.json
    if (!file.open(QIODevice::WriteOnly)) {
        logError(QString("Error: Could not open %1 for writing").arg(filepath));
        return false;
    }

    QJsonObject layers;
    const char *names[kLayers] = { "bottom", "middle", "top" };
    for (int layer = 0; layer < kLayers; layer++) {
        QJsonArray ids;
        for (uint16_t id : m_cells[layer])
            ids.append(id);
        layers[names[layer]] = ids;
    }

    QJsonObject alpha;
    alpha["bottom"] = this->bottomAlpha;
    alpha["middle"] = this->middleAlpha;
    alpha["top"] = this->topAlpha;

    QJsonArray behaviors;
    QJsonObject behaviorNames;
    for (uint16_t value : m_behaviors) {
        behaviors.append(value == kAutoBehavior ? -1 : static_cast<int>(value));
        if (value != kAutoBehavior && Tileset::behaviorNamesInverse.contains(value))
            behaviorNames[QString::number(value)] = Tileset::behaviorNamesInverse.value(value);
    }

    QJsonObject root;
    root["version"] = kFileVersion;
    root["width"] = m_size.width();
    root["height"] = m_size.height();
    root["layers"] = layers;
    root["alpha_channel"] = alpha;
    root["behaviors"] = behaviors;
    root["behavior_names"] = behaviorNames;

    // (one line per layer row would be nicer to read; Qt's JSON writer has no such option, and a compact document is still diff-able)
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        logError(QString("Error: Could not write %1: %2").arg(filepath, file.errorString()));
        return false;
    }
    return true;
}
