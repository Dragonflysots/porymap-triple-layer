# Fork architecture: the Porymap/porytile workflow

This document is for anyone (human or AI agent) picking up this fork of [Porymap](https://github.com/huderlem/porymap)
with no other context. It explains what was added on top of upstream, where it lives, and how the pieces fit together,
so you can extend it, fix it, or reproduce it from a fresh checkout of upstream Porymap + a pokeemerald-family project.

If you only want to *use* the fork, read `README.md` and the manual page
[`docsrc/manual/porytiles-workflow.rst`](docsrc/manual/porytiles-workflow.rst) instead. This file is implementation-level.

## The idea in one paragraph

A GBA map's real data is **metatiles**: every combination that appears on the map (house on grass, tree on sand, the
same tree on grass) needs its own metatile. Building metatiles by hand is *assembling parts*, not *drawing a map*.
This fork keeps that data model 100% intact (nothing about the generated `.bin` files or the vanilla Tileset Editor
workflow is removed) and adds a second, complementary way to work: paint the map on **three layers** (Bottom, Middle,
Top) with small, single-layer building blocks called **porytiles**, then let the tool turn that into real metatiles
and `map.bin` (**Write to Finalmap**). The reverse also exists: **Pull to Porymap** rebuilds the three layers from an
existing map, pixel-for-pixel, so the two representations can be kept in sync in either direction.

```
 Porytiles (2x2 tiles, one layer)             Metatiles (12 tiles = 3 layers)
        |  painted on 3 layers                        ^
        v                                              |  Write to Finalmap (dedups, reuses, fills, grows)
   [ Porymap view ] ------------------------------->  [ Finalmap = the real map.bin ]
        ^                                              |
        +------------  Pull to Porymap  <-------------+  (rebuilds the layers, pixel-for-pixel)
```

This only makes sense for a project whose metatiles use **three background layers** instead of two — an existing,
pre-fork Porymap project setting (`Project Settings > Enable triple layer metatiles`, `tripleLayerMetatilesEnabled` /
`enable_triple_layer_metatiles`). The whole workflow is gated on that one flag; see "The on/off switch" below.

## Data model

| File | What it holds | Written by |
|---|---|---|
| `data/layers/<layoutId>/layers.json` | The Porymap view of one layout: a grid of porytile ids per layer (Bottom/Middle/Top), per-layer alpha flags, and per-field behaviors. Written after **every** edit — it is not a save-on-demand file, it behaves like autosave. | `PreMap` (`src/core/premap.cpp`) |
| `<tileset dir>/porytiles.json` (next to `metatiles.bin`) | The porytiles of one tileset: `{count, porytiles: [{id, tiles: [4 raw tile values], behavior, label}]}`. Never read by the game; porytile 0 is the fixed erase id, exactly like metatile 0. | `Tileset` (`src/core/tileset.cpp`, `Tileset::porytilesPathFor`) |
| `metatiles.bin`, `map.bin`, `metatile_attributes.bin` | The real, game-facing data. Unchanged format — this fork does not touch how metatiles/maps are serialized, only how you can arrive at their contents. | vanilla Porymap code paths |

`PreMap` (`include/core/premap.h`, `src/core/premap.cpp`) is the in-memory + on-disk model of one layout's Porymap
view: a 3-layer grid of porytile ids, per-layer alpha bits, and per-field behavior overrides, with JSON (de)serialization
and a `LoadReport` describing what happened when a `layers.json` was read (size mismatch, corrupt file, old-format
data set aside, etc).

## The two directions: `MapTransfer`

`src/core/maptransfer.cpp` / `include/core/maptransfer.h` implement both **Write to Finalmap** and **Pull to
Porymap** as a **plan, then apply** pair (`planPush` / `planPull`, then `applyPush` / `applyPull`), so the UI can show
the user exactly what will happen before anything changes:

- **Write to Finalmap**: for every 3x3-tile field of the Porymap view, look at what the three layers currently show.
  If a metatile with exactly that combination of tiles/attributes already exists, reuse it (by id). If not, take the
  next free slot (existing empty slot first, then grow the tileset by one, primary tileset first, then secondary —
  the same order as `Change Number of Metatiles`) and write the new metatile there, giving it an auto-generated label.
  **Existing data is never overwritten or moved** — Write only fills empty slots or appends. The whole plan is
  verified field-by-field against the source before it is offered; if verification fails, nothing changes and the
  exact field is reported.
- **Pull to Porymap**: the reverse — read the map's metatiles field by field and reconstruct the layers/porytiles,
  reusing existing porytiles the same way Write reuses metatiles. Running Write immediately after a Pull is a no-op
  (`changedFields == 0`), which is itself one of the invariants the test harness checks.

Both are one `QUndoCommand` each (`include/ui/premapcommands.h` has the porytile-editing commands; the transfer
commands live alongside `MapTransfer`), so either direction is a single Undo step, including every tileset slot it
touched.

## The Porymap view (UI)

- `PreMapPixmapItem` (`src/ui/premappixmapitem.cpp` / `include/ui/premappixmapitem.h`) is the canvas: renders the
  three layers, handles the paint/bucket/pointer/eyedropper/shift tools, per-field behavior overlay, and the
  edge-of-map painting behavior (a block selection may hang over the map edge; the in-bounds part is placed/previewed
  firmly, the rest is previewed faintly and dropped on release; the pointer keeps reacting up to `kMinReach` fields
  beyond the edge so a large block can be anchored outside and still reach in). `mapRect()` is the map in scene
  coordinates; `boundingRect()` is deliberately wider so the view can scroll into the margin.
- `BehaviorOverlayItem` (`src/ui/behavioroverlayitem.cpp`) draws the per-field behavior hex numbers on top of the
  Porymap view, opacity-controlled (`porymapConfig.behaviorOverlayOpacity`).
- `MainWindow`'s `MainTab` enum (`include/mainwindow.h`) gained a `Porymap` tab *before* the original `Map` tab
  (which is now internally called "the Finalmap" in comments/messages, but is otherwise the original map view
  unchanged). A synthetic `topTabBar` collapses `Porymap`+`Map` back into a single visible top-level "Maps" tab with
  its own second-row sub-tab strip (`MainWindow::initTopTabBar`, `syncTopTabBar`), so the rest of the top-level tab
  order (Events, Header, Connections, Wild Pokemon) is unchanged from upstream.
- The layer bar (eyes + active-layer buttons + alpha checkboxes), the Write/Pull buttons, and the eraser
  ("Clear Layers" on the Porymap tab, "Clean the Map" on the Finalmap tab) are added directly in
  `MainWindow::initExtraSignals()` / `forms/mainwindow.ui`.

## The Tileset Editor rework

The single-sheet, small-dialog original Tileset Editor became **Porytiles | Generated Metatiles** tabs
(`ui->tabWidget_TilesetEditor`, `ui->tab_Porytiles` / `ui->tab_GeneratedMetatiles`), each with its own
**Paint | Behavior** page (`ui->tabWidget_MetatileEditor`, which is physically moved into whichever outer tab is
showing — see `TilesetEditor::onKindTabChanged` in `src/ui/tileseteditor.cpp`). Both the Porytiles and the Generated
Metatiles sheets support:
- Direct painting on the sheet with a `TileBrush` (`src/core/tilebrush.cpp`) picked from the source tile sheet
  (metatiles) or dragged as a block selection (porytiles), with per-stroke Undo grouping.
- A layer bar (metatiles only — porytiles are single-layer by definition).
- Selection framing, cut/copy/paste, entry-0 protection (metatile 0 / porytile 0 can never be edited — see
  `allowEntryZero` / `refuseEntryZero`).
- A behavior page: coloured hex numbers with an opacity slider, a filter (by behavior or by label, with
  crossed-out marks for filtered-out entries), numbered labels.
- Four independent Undo histories (`paintHistory`, `behaviorHistory`, `porytilePaintHistory`,
  `porytileBehaviorHistory`).
- `TilesetDividerItem` (`include/ui/tilesetdivideritem.h`) — a `QGraphicsLineItem` drawing the thick red line between
  the primary and secondary tileset/porytile set, with a cosmetic (DPI-scaled) pen so it stays a constant thickness
  in device pixels at any zoom. Used on every metatile/porytile selector, including both main-window palettes
  (`MetatileSelector::updateDivider`), gated on "both sides non-empty" and `porymapConfig.showTilesetEditorDivider`
  (`View > Show Tileset Divider`).

`Project` and `Tileset` code (`src/project.cpp`, `src/core/tileset.cpp`) load/save `porytiles.json` alongside
`metatiles.bin`, resolve behavior names for it (`Tileset::behaviorNames`), and handle a corrupt file by renaming it
aside (`*.corrupt-<time>`) and starting from a blank set rather than failing to open the project.

## Files removed

The old **Map Objects / prefabs** system this fork's Porymap-view workflow replaced is gone:
`include/ui/prefab.h`, `prefabcreationdialog.h`, `prefabframe.h`, `include/ui/metatilelayersitem.h` and their `.cpp`
files, `forms/prefabcreationdialog.ui`, `forms/prefabframe.ui`. If you are diffing against upstream and wondering
where these went, that's why.

## The on/off switch

Added last, on top of everything above: the whole custom UI is gated on the **same** project setting that gates
triple-layer metatiles themselves (`projectConfig.tripleLayerMetatilesEnabled`, checkbox in
`Project Settings > Layout/Metatiles`). No new config key was introduced — the workflow is entirely about
triple-layer metatiles, so a separate flag would only be a second thing that could disagree with the first.

- `MainWindow::applyPorytileWorkflowVisibility()` (`src/mainwindow.cpp`) is the single decision point: hides the
  `Porymap` tab, forces the current tab back to `Map` if needed, and hides the Write/Pull buttons and the eraser
  (`MainWindow::updateTransferButtons()`). `MainWindow::syncTopTabBar()` also hides the whole second-row sub-tab
  strip under "Maps" when the switch is off, rather than leaving a single-tab strip with nothing to switch to.
- `TilesetEditor::updatePorytilesTabVisibility()` (`src/ui/tileseteditor.cpp`) mirrors this for the Tileset Editor:
  hides the `Porytiles` outer tab and defaults to `Generated Metatiles`.
- Both are called once from `MainWindow::setProjectUI()` when a project opens. Toggling the checkbox in Project
  Settings already prompts "reload project to apply changes?" for every setting (`ProjectSettingsEditor::closeEvent`);
  a Yes there re-opens the project and re-applies visibility through the same path — no separate live-wiring needed.

Net effect: a project with the setting off — including any unrelated, non-fork project opened with this same
binary — looks and behaves exactly like upstream Porymap. This is what makes it safe to use one build of this fork
for both a custom-engine project and an ordinary pokeemerald/pokeruby/pokefirered checkout.

## Testing

There is no unit test suite; correctness is verified with a **headless UI test harness** that boots the real
`MainWindow`, sends real synthetic mouse/keyboard events against a cloned project, and compares rendered pixels and
on-disk JSON. It is **not part of this repository** — it lives one level up, at `tools/porymap-uitest/` in the
workspace this fork was developed in (sibling of `porymap/` and of the pokeemerald-family project it tests against),
because it needs both trees side by side and writes into a throwaway clone rather than the real project. If you
don't have that harness, the short version of how it works: `uitest_main.cpp` builds against Porymap's own object
files with `#define private public`, drives the window through `scenario_*.inc` files (one per feature area; the
ones most relevant here are `scenario_porytiles*.inc`, `scenario_porymap*.inc`, `scenario_transfer.inc`,
`scenario_divider.inc`, `scenario_edge.inc`, and `scenario_engine_toggle.inc` for the on/off switch), and
`sabotage.py` provides a negative control for every check (patch in a deliberate bug, the scenario must fail, revert
byte-for-byte) — every behavior described in this document has at least one.

## Rebuilding this from a bare upstream checkout

You almost never need to do this — the code is already in this repository's history (see `git log`, two commits:
"Add layer-based porytile workflow" and "Add on/off switch for the Porymap/porytile workflow"). This section is for
the hypothetical case of re-deriving the design from scratch, or explaining it to someone who wants to port the idea
onto a *different* fork of Porymap:

1. Start from upstream Porymap with a pokeemerald-family project that already has triple-layer metatiles
   (`tripleLayerMetatilesEnabled`) — that project setting must exist first; this fork does not add it.
2. Add the data model: `PreMap` (per-layout 3-layer grid + behaviors, JSON in `data/layers/<id>/layers.json`) and
   `porytiles.json` support in `Tileset` (single-layer 2x2 blocks, id 0 reserved).
3. Add `MapTransfer` (plan/verify/apply for both directions) before any UI — it is pure data transformation and is
   what everything else calls.
4. Add the Porymap view: `PreMapPixmapItem` + `BehaviorOverlayItem`, the new `MainTab::Porymap` tab and its layer
   bar, wired through `MainWindow`.
5. Rework the Tileset Editor into the two-outer-tab / two-inner-tab structure, add `TileBrush` sheet painting,
   `TilesetDividerItem`, the behavior page's filter/opacity.
6. Remove the old Map Objects/prefab system if it's being replaced (this fork did; a straight port that keeps prefabs
   would skip this step).
7. Add the on/off switch last (see above) — it is a thin visibility layer over everything built in steps 2-6, easiest
   to get right once those are stable.
8. Build a test harness alongside (headless `MainWindow`, synthetic events, pixel/JSON comparison, negative
   controls) — the design in this fork's harness generalizes to any Qt Porymap fork.

Build: `qmake && make` (Qt 6, macOS/Linux/Windows, same as upstream — no new build dependencies were introduced).
