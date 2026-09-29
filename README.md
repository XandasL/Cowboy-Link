# Cowboy Link

**Cowboy Link** is a Dusklight mod for *The Legend of Zelda: Twilight Princess HD*.
It replaces Link's classic cap with a western-style cowboy hat and includes support
for Hero and Ordon outfits, texture-resolution presets, leather styles, brightness,
and custom color controls.

## Asset delivery

Cowboy Link reconstructs the modified head at runtime from the player's local game
assets. The distributable package does not bundle complete `al_head.bmd` or
`bl_head.bmd` model files.

## Cross-platform build

This repository is structured from the official
[TwilitRealm/mod-template](https://github.com/TwilitRealm/mod-template).

A local build is useful for testing only and produces a package for the current
platform:

```sh
cmake -B build
cmake --build build
```

The distributable release should come from **GitHub Actions**. The included workflow
builds all platforms supported by the official template and merges them into a single
artifact named **`mod-combined`**.

### Publishing workflow

1. Create a GitHub repository and upload this project at its root.
2. Push to the default branch.
3. Open the repository's **Actions** tab and wait for the **Build** workflow to finish.
4. Open the completed workflow run.
5. Download the **`mod-combined`** artifact.
6. Use the `.dusk` inside that artifact as the replacement build for Dusklight review.

Do **not** submit a local Windows-only `build/mods/cowboy_link.dusk` as the public
release. The combined artifact contains the per-platform native libraries and has its
`abi`, `imports`, and `exports` metadata verified/updated by `symgen` during the merge.

## License and credits

Original Cowboy Link code/scripts by **XandasLegend** are released under the MIT
License. See `LICENSE`.

The Hat_Cooper model is by **THESTIG03** and is licensed under **CC BY 4.0**. See
`THIRD_PARTY_NOTICES.md` and `res/CREDITS.txt` for full attribution.

Nintendo-owned game assets remain the property of their respective rights holders and
are not covered by the Cowboy Link MIT License.
