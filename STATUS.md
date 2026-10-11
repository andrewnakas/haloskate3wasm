# STATUS

Live: https://andrewnakas.github.io/haloskate3wasm/

## Done (2026-10-11)
- Repo created; OpenCE cc0-clean vendored into `engine/`; plan in `docs/PLAN.md`.

- **M0 done.** `ninja web` builds locally (halo.wasm 3.4 MB) and in CI (`.github/workflows/pages.yml`, emsdk 6.0.10);
  Pages deploy green. `clean/maps.json` = live halo-cleanroom manifest with absolute bases (refreshed at build).
  Local check: `node tools/devserver.mjs` (proxies + caches maps in ~/Documents/haloskate-ref/maps) then
  `node tools/play.mjs --url "http://localhost:8642/haloskate3wasm/?auto=1&map=a10&level=a10&room=0&fps=1"`:
  a10 runs headless at ~50 fps (docs/shots/m0-a10.png).

## Decision: how "verbatim" the skate port can be (see docs/PORTMAP.md)
skate-core's runtime is animation-driven (ABIN clips + state graphs set pop/push/turn and the flip board pose) and
its VLT tuning numbers are not in the repo or on this machine. So the C++ library is layered:
1. **Ported verbatim** where skate-core is self-contained: gesture recognizer + 270-row gesture->trick table,
   grind-rail derivation (`grind_world/derive.rs`), ground-jump/air math, manual balance, wipeout triggers, respawn.
2. **Procedural substitute** for the animation-driven parts (pop timing, push cadence, board flip rotation), with
   flip timings from kickflip-ops' clean sim; board/rider as one kinematic body on Halo collision raycasts.
3. Full 7-body `BoardRuntime` port is a later stage behind the same `skate_api.h` (not needed to play).

- **M1 done.** `skate/` C++17 library: flick-it recognizer + Square trick table ported verbatim, skater controller
  (push/carve/ollie/flips/spins/grabs/manual/powerslide/grind/bail/respawn), C API, 72 native checks (`make -C skate test`).
- **M2 done.** Bridge `engine/source/skate/skate_halo.c` (hooks: `bipeds.c` biped_update, `director.c` chase cam,
  `xinput_sdl.c` keys, `render.c` board, `damage.c` kills). Verified headless on Blood Gulch + a10: mount (B), push,
  carve, ollie/kickflip/shove-it flicks, chase camera (docs/shots/m2-chasecam.png).
- **M3 (mostly) done.** Board drawn with Halo's debug triangles (deck, kicktails, trucks, wheels; docs/shots/m3-board.png);
  DOM HUD (`engine/port/web/site/skate-hud.js`: trick names, combo x mult, score, speed, bail flash, controls strip;
  docs/shots/m3-hud.png); kills from the board add "<trick> Kill" / "Airborne Kill" to the combo; page retitled
  HaloSkate 3 with a controls card. Service-worker cache prefix renamed (`haloskate3-`) so it never touches
  halo-cleanroom's caches on the same origin. Default public room disabled (single player).

## Testing tips (for the loop)
- Fast route to gameplay: `?auto=1&quick=host&map=bloodgulch&room=0&env=HALO_SKATE_AUTO=1`, then press Enter twice
  (lobby -> start). Campaign: `?auto=1&map=a10&level=a10&room=0&env=HALO_SKATE_AUTO=1,HALO_SKATE_FAST=1` (the a10 intro
  is unskippable; control comes back after ~2 min).
- Don't send keys while a map is loading (page input during the a10 load stalled it once): use `--after "skate: ready"`.

## Next
- M4: grind rails derived from the BSP at map load (port `grind_world/derive.rs`), rotations naming, pump.
- M5: gamepad right-stick vs aim, touch, docs (README/CLEANROOM/ASSETS), smoke workflow.

## Questions for the user
(none yet)

## For the user
(filled in at the end of the night)
