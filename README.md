# Porymap (triple-layer / porytiles fork)

[![Actions Status](https://github.com/huderlem/porymap/workflows/Build%20Porymap/badge.svg)](https://github.com/huderlem/porymap/actions)

A map editor for the Pokémon generation 3 decompilation projects ([pokeruby][pokeruby], [pokeemerald][pokeemerald], and [pokefirered][pokefirered]).

**This is a fork** of the upstream project linked above, built for a custom triple-layer-metatile pokeemerald engine.
On top of everything upstream Porymap does, it adds a second, complementary way to build maps: paint them on three
layers with small building blocks called **porytiles**, then let the tool generate the real metatiles for you (and
pull them back out of an existing map the other way). See:

- [`docsrc/manual/porytiles-workflow.rst`][porytiles-manual] — the user-facing manual page (what it does, how to use it).
- [`ARCHITECTURE.md`][architecture] — the implementation-level explanation (file map, data formats, how to reproduce
  or extend this on another Porymap fork).
- [`CHANGELOG.md`][changelog] — the `[Unreleased] - This fork` section at the top lists everything this fork adds.

The whole workflow is opt-in per project (`Project Settings > Enable triple layer metatiles`) — with it off, this
build behaves exactly like upstream Porymap, so it's also safe to use on an unrelated / vanilla project.

To get started with **upstream** Porymap itself, view the full online guide here: https://huderlem.github.io/porymap/

## Download

This fork does not currently publish prebuilt binaries — build it from source, see [INSTALL.md](INSTALL.md) (same
build process as upstream, no new dependencies). The links below are for **upstream** Porymap, not this fork:

 - [Download Porymap for Windows](https://github.com/huderlem/porymap/releases/latest/download/porymap-windows.zip).
 - [Download Porymap for macOS latest (arm)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-latest.zip).
 - [Download Porymap for macOS 15 (intel)](https://github.com/huderlem/porymap/releases/latest/download/porymap-macos-15-intel.zip).

Linux users must compile Porymap from source.

<details>
    <summary><i>Pre-compiled builds for Linux...</i></summary>

>   If you are a Linux user and you do not want to compile Porymap from source, you may find Porymap on an external package repository like Flathub or AUR.
>   Builds installed through an external package manager are not explicitly maintained by Porymap and may be out of date.
</details>

Read [INSTALL.md](INSTALL.md) for instructions on how to compile Porymap from source.

![Porymap Preview](docsrc/manual/images/introduction/porymap-loaded-project.png)

[pokeruby]: https://github.com/pret/pokeruby
[pokeemerald]: https://github.com/pret/pokeemerald
[pokefirered]: https://github.com/pret/pokefirered
[changelog]: CHANGELOG.md
[releases]: https://github.com/huderlem/porymap/releases
[porytiles-manual]: docsrc/manual/porytiles-workflow.rst
[architecture]: ARCHITECTURE.md
