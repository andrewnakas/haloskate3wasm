# HaloSkate 3

**Play the Halo: Combat Evolved campaign in your browser, and skate through it with Skate 3 mechanics.**

▶ **https://andrewnakas.github.io/haloskate3wasm/**

Press **B** to drop your board anywhere. Push down the Pillar of Autumn's corridors, ollie Covenant
grunts, kickflip off the Silent Cartographer's ramps, grind the edges of Halo's own architecture, and
keep shooting the whole time: kills in the middle of a trick land in your combo. It's the
MW2 × Skate 3 mashup idea (chasmlol), but with Halo CE.

## Controls

| Key (gamepad) | On foot | On the board |
|---|---|---|
| **B** (d-pad ↓) | get on the board | get off |
| W / Space (A, left stick ↑) | walk / jump | push (kicks) |
| S (left stick ↓) | walk back | brake |
| A D (left stick) | strafe | carve, spin in the air |
| **Arrows** (right stick) | — | **flick-it**: pull ↓ and flick ↑ = ollie, ↓ then ↖ = kickflip, ↓ then ↗ = heelflip, ↓ then sweep ← / → = shove-its, ↑ then ↓ = nollie, ← → = hardflip, the scoop ← ↙ ↓ ↗ = 360 flip |
| Shift / C (left stick click) | — | manual / nose manual (W S to balance) |
| Q (white, black) | flashlight | grab |
| X | — | powerslide while carving |
| Mouse | aim, shoot | aim, shoot |
| E | action | action (vehicles, weapons) |
| H | — | show/hide the controls strip |

Land on a rail-like edge to grind it: rails are found automatically in each level's collision (the
faint blue strips near you). Your combo banks after a second of clean rolling; bail and it's gone.

## How it works

- `engine/` — [OpenCE](https://github.com/OpenCommunityEdition/OpenCE) (CC0), the Halo CE Xbox
  decompilation, with fqlx's WebAssembly port (Emscripten, WebGL 2, threads). Vendored from the
  `cc0-clean` branch used by [halo-cleanroom](https://github.com/andrewnakas/halo-cleanroom).
- `skate/` — a **C++17 port of skate-core** from
  [skate-3-rust-engine](https://github.com/SK8-ENGINE/skate-3-rust-engine) (the Rust/Bevy Skate 3
  reverse-engineering project): the flick-it gesture recognizer and trick mapping table line for line,
  grind-rail derivation from a triangle soup, manual and landing rules, plus a procedural layer where the
  original is driven by animation data. No engine dependencies, plain C API (`skate/include/skate_api.h`),
  so other hosts can reuse it. Details: [`skate/PORTING.md`](skate/PORTING.md), [`docs/PORTMAP.md`](docs/PORTMAP.md).
- `engine/source/skate/skate_halo.c` — the bridge: while you ride, the board moves Master Chief instead of
  Halo's walking code, Halo's collision answers the board's ray casts, the director switches to the chase
  camera, the board is drawn with Halo's debug triangles, and the page's HUD (`engine/port/web/site/skate-hud.js`)
  shows tricks and score.
- Maps are **not hosted here**: the page loads the clean-room maps from
  [halo-cleanroom](https://andrewnakas.github.io/halo-cleanroom/) and its two map repositories on the same
  origin (`site/clean/maps.json`, refreshed at every build).

## Build

```sh
# the skate library's native tests
make -C skate test

# the web build (Emscripten 6.0.10, ninja, python3)
cd engine && python3 configure.py --release --web-clean && ninja web && cd ..
tools/make_site.sh dist            # dist/ = the Pages site

# play it locally (proxies and caches the maps from github.io)
node tools/devserver.mjs           # http://localhost:8642/haloskate3wasm/
```

GitHub Actions builds and deploys on every push to `main` (`.github/workflows/pages.yml`), then
`smoke.yml` loads the live site headless and rides a board in Blood Gulch.

Headless checks used during development: `node tools/play.mjs --url "<url>" --after "skate: ready" --keys "b:0.2,w:2,ArrowDown:0.3,ArrowUp:0.12"`.
Test switches (`?env=`): `HALO_SKATE_AUTO=1` (board on as soon as you have control), `HALO_SKATE_RAILTEST=1`
(drop onto the nearest rail), `HALO_SKATE_FAST=1` (cutscenes at 8×).

## Credits

- Halo: Combat Evolved © Microsoft / Bungie. Skate 3 © Electronic Arts. This is a fan project; no
  retail art, audio or video ships here (see [CLEANROOM.md](CLEANROOM.md), [ASSETS.md](ASSETS.md)).
- OpenCE: OpenCommunityEdition (CC0); web port: fqlx; clean-room maps: halo-cleanroom.
- skate-core: SK8-ENGINE / skate-3-rust-engine contributors.
- Mashup idea: chasmlol's Modern Warfare 2 × Skate 3. Skate-and-shoot scoring after
  [kickflip-ops](https://github.com/andrewnakas/kickflip-ops).
