# HaloSkate3Wasm: Halo CE campaign + Skate 3 mechanics in the browser (overnight loop)

## Context
You want something like chasmlol's MW2×Skate 3 mashup, but with the Halo CE campaign: skate through Halo levels using Skate 3 mechanics (flick-it tricks, grinds, manuals, bails) while still being able to shoot. It should be playable on GitHub Pages in a new repo `andrewnakas/haloskate3wasm`, and the work runs unattended overnight.

What the research found:
- **Halo side:** the engine is `andrewnakas/OpenCE`, a CC0 C decompilation of Halo CE. Its web port uses Emscripten 6.0.10, WebGL2, pthreads and a service worker that provides cross-origin isolation. A local checkout is at `~/Documents/OpenCE-web`, on the `cc0-clean` branch. The live clean-room site, `andrewnakas.github.io/halo-cleanroom`, already serves `maps.json` and every campaign map (from maps1/maps2). The new repo will be on the same origin, so **it can load those maps directly and needs no re-hosting**.
- **Skate side:** the mechanics live in `skate-3-rust-engine`, branch `cleanroom-web`, in the crate `crates/skate-core`. It is about 90k lines of pure Rust with no dependencies: the gesture recognizer, trick/riding/grind/manual/wipeout intentions, board physics, and world contact against a plain triangle soup (`BoardWorld::new(Vec<WorldTriangle>)`). Rails are derived from the geometry by `skate-game/src/grind_world/derive.rs`. Bevy can't run inside OpenCE, so the core logic gets ported.
- **Prior art:** `kickflip-ops` already does skate + shoot. Its patterns carry over: `Loco::{Foot,Skate}` with the G key to toggle, shooting allowed on the board, and the "TRICK KILL" synergy scoring in `src/core/score.rs`. Its CI and Pages smoke-test workflows are the template here.

Your decisions: a verbatim skate-core port, written in **C++** (it can also be reused on Snapchat Spectacles), browser single-player, third-person camera while on the board.

## Approach

### 1. The `skate/` library: a portable C++17 port of skate-core
- It is a standalone library in `haloskate3wasm/skate/` with no engine dependencies. It exposes one **`extern "C"` API** (`skate_api.h`), so OpenCE's C code and other hosts such as Spectacles can link it.
- Port module for module, verbatim, keeping the names from `skate-core`:
  - `input/` (gesture.rs, trick/riding/grind/manual/wipeout_intentions, pad, tick)
  - `riding/`
  - `air/` (ground_jump, trajectory)
  - `physics/` (board_runtime, board_step, rigid_body, world_contact, collision, grind_contact/forces/air, manual)
  - `player/` (state machine, wipeout, respawn)
  - `scoring`
- **Collision:** the host passes in a triangle soup, either the Halo BSP triangles of the current map or ones found by a radius query. Port `skate_world.rs` (weld/adjacency/edge flags) and `grind_world/derive.rs` (automatic rails) too.
- **Data policy (decided):** the numeric tuning values and the gesture→trick tables are compiled in as constants (`skate/data/*.inc`). **No Skate 3 animation banks, meshes, textures or audio ship.**
  - The physics pieces that are driven by animation (the `skeleton_*` bodies and the ABIN-driven poses) get a small procedural substitute that keeps the same interface.
  - Each substitution is documented in `skate/PORTING.md`.
- **Tests:** native unit tests (`skate/tests`, a plain clang++ build) check against the Rust test vectors in skate-core wherever they exist, e.g. flat-ground ollie height and the flick→trick mapping.

### 2. OpenCE integration: the repo's copy of the engine, in C
- Seed the repo by copying the OpenCE web source that halo-cleanroom deploys. The loop checks `~/Documents/OpenCE-web` and `andrewnakas/OpenCE@web` to see which one `games/halo/make_site.py` uses, and records the choice in STATUS.md.
- **Mount and dismount:** a G key (gamepad: a click of the right stick) adds a per-player `skating` flag. While it's set, `biped_update_moving` / `biped_update_physics` in `source/units/bipeds.c` hand position, velocity and facing to `skate_step()`. Getting off copies the velocity back, the same handoff as kickflip-ops' `enter_loco`.
- **Input:** the raw controller state comes from `source/game/player_control.c` (`handle_one_player_input`) and is turned into skate-core's pad packet. Halo's own mappings on the left/right stick are blocked while skating.
  - Right stick or arrow keys: flick-it.
  - Mouse: free camera orbit.
  - Fire still works.
- **Collision feed:** when a map loads, pull the triangles from the structure BSP (`source/physics/collision_bsp.c`) into the skate world. Grind rails are derived once per map and cached.
- **Third-person camera on the board:** use Halo's existing third-person/vehicle camera path for a chase cam behind the board.
- **Board visual:** a small CC0 board mesh. It is drawn from a hook in the D3D8→GL layer (`port/linux/src/d3d8_gl.c`) or attached as a scenery-object proxy, whichever proves simpler. Master Chief's yaw, lean and crouch follow the board state.
- **HUD:**
  - A DOM overlay on the page shows trick names, combo, score and the "TRICK KILL" synergy from kickflip-ops.
  - It is fed by a new `web_note_skate` message, following the existing `web_note_map` (kind 8) pattern in `port/web/src/web_main.c` and `app.js`.
- **Bails:** a hard landing or wall hit makes Master Chief stumble. After a short delay he respawns upright on the board, using the ported `player/respawn` logic.

### 3. Site and deploy
- The page is OpenCE's `port/web/site`, retitled "HaloSkate3". It shows a controls card and starts the campaign directly with `?level=a10` (a level picker covers the other nine).
- `maps.json` points at the existing absolute bases `/halo-cleanroom/clean/`, `/halo-cleanroom-maps1/clean/` and `/halo-cleanroom-maps2/clean/`, so only about 6 MB of wasm/js ships.
- **CI** (`.github/workflows/web.yml`):
  - Steps: set up emsdk pinned to the version OpenCE uses, run `configure.py --release && ninja web`, upload-pages-artifact, then deploy-pages.
  - If emsdk 6.0.10-git can't be reproduced in CI, the fallback is to build locally and push a `gh-pages` branch, the same way `tools/publish_halo.sh` does it.
- **Smoke test:** a `smoke.yml` modelled on kickflip-ops. Playwright opens the live URL at `?level=a10&skate=1`, waits for the log line that says the skate module is ready, and saves a screenshot.
- **Repo docs:** README.md (credits: OpenCE CC0, the skate-3-rust-engine port, chasmlol inspiration), CLEANROOM.md/ASSETS.md, CLAUDE.md (the unattended-loop rules) and STATUS.md.

### 4. Overnight loop mechanics
- Create the repo `andrewnakas/haloskate3wasm` (public, Pages via Actions) and `git init` in `~/Documents/HaloSkate3Wasm`.
- Write a `CLAUDE.md` in the style of skate3-cleanroom's:
  - "running unattended; never stop to ask; questions go to STATUS.md"
  - the machine budget
  - commit often, git user andre / treesixtyweather@gmail.com
- Run it with **`/loop` self-paced in this session**, with `caffeinate -dims` keeping the Mac awake.
- **Each iteration:**
  1. Read STATUS.md.
  2. Pick the next milestone task.
  3. Build and test.
  4. Commit and push.
  5. Update STATUS.md.
- **Machine budget:** 8 GB RAM, and **only 7.1 GB of free disk**.
  - At most `-j4`, one headless browser at a time.
  - Never download more than one campaign map locally (test on a10, or c20 if it's smaller).
  - No skate packs and no Bevy builds; read the Rust source only.
  - Clean `build/` intermediates if free space falls below 3 GB.
- **Milestones, in order.** Every milestone ends with a deployed, playable site.
  1. **M0:** repo, OpenCE web build reproduced locally, Pages deploy of plain Halo running against the existing maps.
  2. **M1:** the `skate/` C++ library core (gesture recognizer, riding, ollie, air, flips, landing) plus native tests.
  3. **M2:** integration in OpenCE: mount/dismount, riding and ollies on Halo terrain, chase cam.
  4. **M3:** flick-it tricks, board render, HUD overlay, bails/respawn, shooting while skating plus trick-kill scoring.
  5. **M4:** grinds on derived rails, manuals, rotations, pump.
  6. **M5:** polish, gamepad/touch, docs, smoke test green.

## Verification
- **Native:** `skate/tests` pass (`make -C skate test`) with each milestone's commit.
- **Local web:** `ninja web` plus a local static server. Then:
  1. Playwright/chrome-devtools loads `?level=a10&skate=1`.
  2. It drives synthetic input (G, W, Space, arrow flicks).
  3. It checks console logs for skate events (`ollie`, `kickflip landed`) and that Master Chief's position changes, and takes screenshots, which are kept under `docs/shots/`.
- **Live:** the CI deploy is green, the smoke workflow passes on `https://andrewnakas.github.io/haloskate3wasm/`, and the final STATUS.md has a "For the user: what to test in the morning" section.

## Out of scope tonight (noted in STATUS.md)
- Networked play.
- Registering on aigamemashups.com.
- A native Spectacles build. The library is kept portable, but there's no Spectacles target.
