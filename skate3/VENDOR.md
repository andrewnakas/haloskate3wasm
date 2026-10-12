# skate3/: the full Skate 3 runtime

`src/s3/{core,data,game}` is copied unchanged from [skatecore-cpp](https://github.com/andrewnakas/skatecore-cpp)
at commit `f6c351a` ("full Skate 3 runtime live on Pages"). That repository is the C++ port of the cleanroom Rust
engine (skate-core + skate-game's simulation), and it matches the Rust reference tick for tick.

The one local change is `Sim::travel_to` (`src/s3/game/driver.{hpp,cpp}`), which moves the skater onto the board at a
given position and facing. It goes through the runtime's existing manual-return teleport
(`SkaterRuntime::travel_to`). HaloSkate3 uses it to put the board under Master Chief.

`src/halo_host.cpp` / `include/s3_halo.h` is the C API the engine bridge (`engine/source/skate/skate_halo.c`) calls:
- **The pack:** `s3h_pack_alloc` / `s3h_pack_install`.
- **The world:** `s3h_world_*`. It takes a triangle soup and builds an in-memory `SkateMap`. Triangles are
  Morton-sorted so each 64-triangle query chunk stays tight. The runtime derives the grind rails from the soup itself.
- **The simulation:** `s3h_step` and the view, board and events getters.

## Data
The simulation needs the Skate 3 cleanroom pack (`core.pack`, about 95 MB: animation banks, state graphs, VLT
tuning, gesture patterns). The page fetches it at run time from `/skate3-cleanroom/`, the same origin as the Halo
maps, and keeps a copy in Cache Storage. **It is never committed or shipped here.** Without it, the lite skater in
`../skate` rides instead.

## Build and test
- `make -C skate3 test` builds the runtime natively (about 690 files, about 10 minutes the first time at `-j4`). It
  then runs `tests/halo_host_test.cpp` against `~/Documents/haloskate-ref/cleanpack/core.pack` (`PACK=...` to
  override). The tests cover:
  - flat ground: push, ollie, travel
  - a quarter-pipe: ride up the transition
  - an obstacle course: 3000 ticks of random input
- The web build compiles the same sources through `engine/tools/web_build.py`, using `-fwasm-exceptions` and
  `-ffp-contract=off`.

## Updating
Copy `src/s3` again from skatecore-cpp and re-apply `Sim::travel_to`. Then run `make -C skate3 test`.
