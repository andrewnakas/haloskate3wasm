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

- **M4 done.** Grind rails derived from each level's collision BSP at mount / BSP switch (C++ port of
  `grind_world/derive.rs`, its 7 Rust tests ported): Blood Gulch 296 rails, a10 1078, a30 1518. Rails near you glow
  faintly. Verified in game with `HALO_SKATE_RAILTEST=1`: 50-50 grind, ride off the end, bank.
- **M5 done.** Live smoke test green (smoke run 38136209030: rails derived, on the board, ollie popped).
  Campaign verified on a30 (docs/shots/m5-a30-campaign.png): board on as soon as you have
  control, 9 m/s pushes, kickflip, downhill rolling. Board pauses during cutscenes / scripted control / vehicles and
  resumes where Chief ends up. Gamepad right stick flicks (Halo's look is centred while skating). README, CLEANROOM,
  ASSETS, live smoke workflow (`.github/workflows/smoke.yml`).

## 2026-10-12: full Skate 3 runtime (plan: ~/.claude/plans/snazzy-waddling-wombat.md)
User's choice: replace the lite skater with the **full Skate 3 runtime** (skatecore-cpp's `driver::Sim`, bit-identical
to the Rust engine), **Master Chief stays the rider**, campaign starts on foot and B switches to the board.
- `skate3/` = skatecore-cpp `src/s3` vendored at f6c351a (+ `Sim::travel_to`), C API `skate3/include/s3_halo.h`,
  native tests `make -C skate3 test` (18 checks: flat push/ollie/teleport, quarter-pipe ride to 4.35 m, 3000-tick
  random course).
- The page streams the cleanroom pack (core.pack, 90 MB) from `/skate3-cleanroom/` after a level loads (Cache Storage
  `haloskate3-skatepack`); never committed. `?skate=lite` or no pack -> the lite skater.
- The runtime's world = the level's collision BSP as triangles (faced along each surface's plane, two-sided ones
  both ways), rebuilt on BSP switches; the runtime derives grind rails from it itself.
- Verified headless: Blood Gulch (5.5k tris, world 0.8 s): push 8 m/s, ollie + kickflip landed, HUD "OLLIE + KICKFLIP";
  campaign a30 (28k tris, 1.3 s): walk, B on, push, ollie, B off, walk, B on (docs/shots/full-bg-keys.png,
  full-a30-keys.png).
- halo.wasm grew 3.5 -> 11 MB.

## Testing tips (for the loop)
- Fast route to gameplay: `?auto=1&quick=host&map=bloodgulch&room=0&env=HALO_SKATE_AUTO=1`, then press Enter twice
  (lobby -> start). Campaign: `?auto=1&map=a10&level=a10&room=0&env=HALO_SKATE_AUTO=1,HALO_SKATE_FAST=1` (the a10 intro
  is unskippable; control comes back after ~2 min).
- `tools/play.mjs` uses a persistent Chrome profile (~/Documents/haloskate-ref/chrome-profile): fresh contexts have a
  small storage quota and sometimes stalled copying a 160 MB level.
- a10's opening (cryo, look calibration) can't be played headless: test campaign on a30.
- Don't send keys while a map is loading (page input during the a10 load stalled it once): use `--after "skate: ready"`.

## Next
- M4: grind rails derived from the BSP at map load (port `grind_world/derive.rs`), rotations naming, pump.
- M5: gamepad right-stick vs aim, touch, docs (README/CLEANROOM/ASSETS), smoke workflow.

## Questions for the user
- License for `skate/`: it ports skate-3-rust-engine code, which has no LICENSE file upstream (its README says GPL-3.0).
  No license file was added here; engine/ is CC0 (OpenCE). Pick one when you can.
- Touch controls: Halo's touch pad still works on foot, but there is no on-screen flick stick for skating yet.

## Known issues / next
- Full runtime: camera is still Halo's chase cam (not Skate 3's); Chief stays upright on walls/vert; scenery
  objects (crates, vehicles) are not in the skate world; key mapping for B/X/Y buttons is a guess (Shift = X,
  X = B, Q/C = grab triggers); wasm size (11 MB) could shrink.
- Skate tuning is hand-authored (no stock VLT data): feel free to tweak `skate/src/skater.hpp` `Tuning`.
- The board is boxes drawn with Halo's debug triangles; Chief keeps Halo's idle/crouch animation (no skate poses).
- Flick patterns are authored (16 tricks); late flips, grabs by name, rotations naming and pumping are simple.
- a10's opening is the scripted cryo sequence: the board waits until you have control.
- Rarely, a level's first download stalls in a fresh browser profile (halo-cleanroom's known first-download issue);
  reloading fixes it.

## For the user (what to test in the morning)
1. Open https://andrewnakas.github.io/haloskate3wasm/ , start the campaign (or a Blood Gulch match).
2. Press **B** to drop the board. W/Space push, A/D carve, arrows to flick: ↓ then ↑ ollie, ↓ then ↖ kickflip,
   ↓ then ↗ heelflip, ↓ then sweep ←/→ shove-it, ← → hardflip. Shift manual, Q grab, X powerslide.
3. Look for the faint blue strips: land on them to grind. Shoot while airborne for "Kickflip Kill" style bonuses.
4. Gamepad: d-pad ↓ board, right stick flicks.
