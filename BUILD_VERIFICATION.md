# Build verification

Prepared from the working Cowboy Link v1.1.0 native assetless implementation and
restructured around the official `TwilitRealm/mod-template` build flow.

Local verification performed on Linux x86_64 against Dusklight v2.0.2:

- CMake configure: PASS
- C++ compilation: PASS
- Native module link: PASS
- `.dusk` packaging: PASS
- Generated platform library: `lib/linux-x86_64/mod.so`
- Complete `al_head.bmd`, `bl_head.bmd`, `Kmdl.arc`, and `Bmdl.arc` files: NOT INCLUDED

The public/distributable build must still be produced by the included GitHub Actions
workflow so all supported platform libraries are compiled and merged into `mod-combined`.