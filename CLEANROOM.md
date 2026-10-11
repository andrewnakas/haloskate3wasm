# Clean room

What ships on the site, and where it comes from.

## Halo side
- **Engine code**: OpenCE (CC0 decompilation), vendored in `engine/`.
- **Maps**: not hosted in this repository. The page loads the clean-room `.map` files published by
  [halo-cleanroom](https://github.com/andrewnakas/halo-cleanroom) (its README and STATUS document the
  process: every texture, sound and voice regenerated from CC0 sources and checked by a taint scan;
  geometry, collision, scripts and tuning are the original level data).
- **Fonts and UI**: OpenCE's `--web-clean` build (Kenney CC0 fonts, no retail title pictures).

## Skate side
- **No Skate 3 data ships**: no animation banks, meshes, textures, sounds or gesture pattern files.
- **Code**: `skate/` is a C++ port of skate-core, the reverse-engineered logic in
  skate-3-rust-engine (gesture recognizer, trick mapping table, rail derivation, manual and landing
  rules). The 270-row gesture→trick table's Square group is compiled in as data, as it is in skate-core.
- **Authored for this project**: the 16 flick-it stick patterns (the 360 flip uses skate-core's
  recovered test vector), the trick timings (from kickflip-ops' clean sim), every tuning number
  (`skate/src/skater.hpp`, marked TODO-DATA), and the board model (boxes drawn in code).

## Mashup layer
HUD, scoring synergies and the page are original to this repository.
