# HaloSkate3Wasm — Halo CE campaign + Skate 3 mechanics, in the browser

You are running **unattended** (overnight /loop). Never stop to ask: write open questions in
`STATUS.md` under "Questions for the user", pick the sensible default, and keep going.
Log every decision and milestone in `STATUS.md`. The full plan is `docs/PLAN.md`.

## Goal
Play the Halo CE campaign in the browser and skate through it with Skate 3 mechanics
(flick-it tricks, ollies, grinds, manuals, bails), shooting while on the board, à la chasmlol's
MW2×Skate 3 mashup. Live at https://andrewnakas.github.io/haloskate3wasm/

## Pre-answered scope (do not ask)
- Engine: `engine/` = vendored OpenCE (CC0, branch cc0-clean of ~/Documents/OpenCE-web). C, Emscripten, WebGL2.
- Skate: `skate/` = **C++17 port of skate-core** (verbatim logic, same module names), engine-free,
  `extern "C"` API in `skate/include/skate_api.h` (must stay portable: Snap Spectacles may reuse it).
  Reference source (read only): `~/Documents/haloskate-ref/skate-core`, `.../skate-game/src`, `.../kickflip-ops`.
- Data: numeric tuning + gesture->trick tables compiled in as constants. **Never ship Skate 3 animation
  banks, meshes, textures or audio.** Animation-driven physics bits get procedural substitutes; document
  each in `skate/PORTING.md`.
- Single-player browser only. 3rd-person chase cam on the board, 1st-person on foot. G toggles board.
- Maps: do NOT re-host. maps.json points at absolute same-origin bases
  `/halo-cleanroom/clean/`, `/halo-cleanroom-maps1/clean/`, `/halo-cleanroom-maps2/clean/`.
- Out of scope: networking, aigamemashups registration, native Spectacles target.

## Milestones (each ends deployed + playable)
M0 build+deploy plain Halo · M1 skate lib core + native tests · M2 OpenCE integration (mount, ride, ollie, chase cam)
· M3 flick tricks, board render, HUD overlay, bails, trick-kill scoring · M4 grinds/manuals/rotations/pump · M5 polish, docs, smoke test.

## Machine budget (hard)
- 8 GB RAM, **~7 GB free disk**. ninja/make `-j4` max. One headless browser at a time.
- Download at most ONE campaign map locally for testing (cache it in ~/Documents/haloskate-ref/maps).
- No Bevy/cargo builds; read Rust only. If free disk < 3 GB, delete build intermediates.
- Long builds run in the background; don't poll in tight loops.

## How to work
- Commit often, small plain-English subjects (git user andre / treesixtyweather@gmail.com), push to `main`.
  End commit messages with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Verify with real runs: native tests (`make -C skate test`), local web build + headless Chrome
  (`?level=a10&skate=1`), console logs + screenshots in `docs/shots/`.
- Keep STATUS.md current: Live URL, Done (dated), Known issues/next, Questions, For the user (what to test).
