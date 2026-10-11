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

## Next
- M1: `skate/` C++ library + native tests.

## Questions for the user
(none yet)

## For the user
(filled in at the end of the night)
