# PORTMAP — Skate 3 mechanics → C++17 lib inside Halo CE

Paths are relative to `~/Documents/haloskate-ref/`. `core/` = `skate-core/src/`, `game/` = `skate-game/src/`.
The number after each file is its total line count. "PURE" means numeric in, numeric out, with no animation, graph or data object needed beyond a settings struct.

---

## 0. Read this first: the facts that shape the port

1. **The recovered runtime is animation-driven at every layer.** Each tick runs the stock ActionGraph and MotionGraph (`.stategraph`). These evaluate ABIN clips, and clip *attributes and events* set the physics input flags:
   - pop = clip attribute `"jump"`
   - push = `push_contact` event
   - brake = `brake_contact`
   - Turn, Balance, BodySpin and similar values are graph scalar attributes, not raw stick values
   - the flip rotation of the board is the clip's board bone, pulled on by a hook-drive spring

   Physics consumes `ProcessedPhysicsInput` (`flags_2468/2472/2476/...`), not the controller.

2. **None of the stock data is on this machine**: `skater-collections.json` (VLT/AttribSys), `OnBoard.abin` / `OffBoard.abin`, `physics-skeletons.json`, `*.pat`, `*.stategraph`. The loader crate `skate_data` is not in this folder either; copies are in `~/Documents/skate3-mw2/crates/skate-data` and `~/Documents/mw2-skate-web/skate/crates/skate-data`. Every game loader returns `Err` when data is missing.
   - **Numbers that exist in code:** the board geometry, masses, joint and drive `STOCK` constants (§2.1).
   - **Numbers that do not exist anywhere:** DeckMass, frictions and restitutions, WorldGravity, RWMaxIterations, FreezingEnergy, push and steer tuning, jump heights, curves, landing limits, gesture patterns.
   - **Consequence:** the port must ship a hand-authored `SkateTuning` table, with every field tagged `TODO-DATA`, plus a procedural animation substitute (§2.5).
   - `~/Documents/skate3-mw2/docs/*.md` has 79 notes (360-FLIP, flip-ladder, grind-*, DIFFICULTY, …) that may help with tuning.

3. **The rider is a 26-part ragdoll** (24 bones plus COM and lifted-COM bodies, 22 joints, up to 48 drive rows). It is solved *in the same island* as the 7-body board (`ATTACHED_REACTION_BASE = 8`). The skeleton depends entirely on ABIN pose and physics-skeletons.json.
   - **Recommendation:** do not port the skeleton. The board can step alone: `BoardRuntime::advance(contacts, truck_targets, step)` with an empty `AttachedStep` is used by the host test `GamePhysics::advance_board` (`game/physics.rs:380-410`).
   - Replace the skeleton with one "rider COM" rigid body. Either attach it with a hard drive above the deck, or simply add rider mass and inertia to the deck. This is the main fidelity trade-off.

4. **The fallback design.** `kickflip-ops/src/sim/skate.rs` (844) is a clean-room procedural sim that is not Skate 3 code: one point body, state enum, flip = 2π roll over 0.42 s, 7 tests. Use it as the design template for the procedural substitute layer and as a sanity reference, not as the physics.

---

## 1. Per-tick call graph (Bevy FixedUpdate: Input → Controls → Physics)

Scheduling, in `game/physics.rs` (676 lines), `PhysicsPlugin::build`:

| Set | System | Location |
|---|---|---|
| `SimulationSet::Input` | `input::publish_actions` → `ControllerInput::tick_input()` | `game/input.rs` (≈90) |
| `SimulationSet::Controls` | `controls::sample` → `PlayerControls::update_for_physics` → `DerivedControllerInput::update` (`core/input/controller.rs`), then `publish_gestures` → `GestureInput::publish` (`game/input/gesture_input.rs` 203) | `game/physics/controls.rs` 264 |
| `SimulationSet::Physics` | `advance` → `frame::advance` | `game/physics/frame.rs` 417 |

`frame::advance` runs these steps in order. The marks show what to do with each step:

- ★ = port
- ◇ = replace with the procedural substitute
- ✗ = skip

1. ✗ climbing, camera rate requests, `biped_air::consume_selector` (off-board walking).
2. ★ `physics.board.clear_forces()` (`core/physics/board_runtime.rs` 438).
3. ★ `riding.start_wheel_queries` (`game/physics/riding_outputs.rs` 381):
   - `core/physics/board_ground.rs` (371) `wheel_lines()` builds 4 segments of 0.2 m along −up, starting at the wheel centres.
   - `BoardWorld::query_thin_line` → `query_swept_line` → `triangle_query::triangle_segment` (`core/physics/board_world.rs` 406, `triangle_query.rs` 123, `triangle_sweep*.rs` 323).
   - **This maps to a Halo raycast.**
4. ✗ `foot_ik_queries::query` (skeleton feet).
5. ◇ `animation_phase::advance` (`game/physics/animation_phase.rs` 439; `graph_host/*`; `core/graph/controller.rs` 268). This runs the graphs and clips and produces the packet (attributes and events).
6. ◇/★ `input_phase::advance` (`game/physics/input_phase.rs` 342 → `player_input.process_stage`, `core/player/input_phase/*` 1708):
   - Turns the packet into `ProcessedPhysicsInput` flags and scalars:
     - `core/animation/skeleton_input/contact_events.rs:52` (push bits 24-27)
     - `extended_attributes.rs:90` ("jump" → 0x400000)
     - `attribute_finalization.rs:72-128` (PrepareJump 0x200000, TrickHeight → jump_strength)
     - `scalar_attributes.rs:120-212` (Turn, HardTurn, Balance, Brake, Slide, BodySpin, FlipTrick → flags2472 bit 22)
   - Builds `BoardToolkit` (`core/physics/board_toolkit.rs` 144, ★ PURE).
   - Runs grind PreUpdate (`game/physics/player_input/grind/pre.rs` 159 → `core/physics/grind_contact/investigator.rs` 214).
7. ★ `riding.finish_wheel_queries` → `WheelLineState::publish` (board_ground.rs).
8. ★ `player_state::post_input_and_select` → `core/player/selector/{mod 250, ground 142, air 152, conditions 57, input 73}`:
   - Ground→Air when `!colliding && off_ground`.
   - Air→Ground (or GroundAnimation if the board is animated) when colliding. Settle 0.08 s, KnownAir grace 0.04 s.
   - Then `wipeout.state.clear_after_selection()`.
9. Dispatch on `PhysicalStateId`:
   - **PhysicsGround (100)** ★:
     - `riding.update_ground_reckoning` → `body_spin::update_ground`, `GroundOrientation::update` (`core/riding/ground_orientation.rs` 307), `ReckoningFrames::{calculate_transform, calculate_dynamic_lean, calculate_tilt}` (`riding/reckoning_frames.rs` 267). Needs `animation_com_to_deck_752`; the substitute supplies it.
     - `ground_phase::advance` (`game/physics/ground_phase.rs` 308) → `GroundState::update` (`game/physics/ground_runtime/update.rs` 121; settings `ground_runtime/settings.rs` 388, input `ground_runtime/input.rs` 274, services `ground_runtime/services.rs` 293) → `core/riding/grounded/state/board.rs` (160) `update`, which runs:
       - `steering::calculate_tilt` → `speed_wobble::calculate` (101) → `TruckSteeringState::update` (`riding/steering.rs` 130). This yields **`truck_targets[2]`**.
       - `deck_angular_correction::apply_ground_body_torque` (`core/physics/deck_angular_correction.rs` 257).
       - One branch: animated / `collision_response` / `ordinary::update` (`grounded/state/ordinary.rs` 293). `ordinary::update` runs:
         - `propulsion::calculate` (`grounded/propulsion.rs` 136): `braking::calculate_braking` (96, tag 2) and `push::calculate_acceleration` (112, tag 3).
         - `ground_force::calculate` ×2 (44, tags 4 and 5).
         - `speed_model::update` (189, tag 6).
         - `slide_friction::calculate` (47, tag 1, powerslide and side friction).
         - `pumping::calculate` (`riding/pumping/*` 363, tag 8).
         - `straighten` (38), `heading` (115), `anti_flip` (50).
         - **`manual::controller::calculate`** (`core/physics/manual/controller.rs` 219).
         - `GroundDragInput::calculate` (`grounded/drag.rs` 103).
         - hang-ups, wheel catches, pinning (`grounded/state/corrections.rs` 392).
       - The core also has a composed `grounded/state/update.rs` (428), but the host uses its own `ground_runtime` composition. **Port the host composition**, using core leaves.
     - ✗ `handplant::ground_update`; ✗ `input_phase::update_ground` (skeleton drive targets).
   - **GroundAnimation** (pop frame) ★: `game/physics/ground_animation/board.rs:49` (207) → `core/air/ground_jump/mod.rs` (84) `calculate`, plus `math.rs` 65 and `types.rs` 49. PURE given settings.
     - The board then gets `set_animated_velocity`, and the trajectory selector is launched with `player_jumped=true`.
   - **PhysicsAir / PhysicsAirSecondary** ★: `game/physics/air_phase.rs` (217) → `core/air/state/{update 193, runtime 76, data 124, lifecycle 95, math 56, board 32, jump_velocity 65, post_physics 15}`.
     - Spin goes through `core/air/body_spin.rs` (203) and `air/reckoning/*` (344, via `game/physics/air_reckoning.rs` 102).
     - Board flip target: `game/physics/skeleton_air.rs` (178) → `core/physics/skeleton_air_frames.rs` (177) → `core/physics/board_animation.rs` (123) `BoardAnimation::apply` → `board.set_hook_transform` → `core/physics/hook_drive/mod.rs` (92).
   - **KnownAir** (predicted-trajectory air) ◇ optional: `game/physics/known_air.rs` (200) → `core/air/known/{update 106, trajectory 165, body 155}`. Trajectory prediction lives in `core/air/trajectory/*` (2055).
   - **LandingOnDeck** ✗/◇: `game/physics/landing_on_deck.rs` (192), `core/physics/skeleton_landing*.rs` (skeleton compression only).
   - **SlideGround (101 = powerslide)** ★: `game/physics/slide_state.rs` (+ `slide_state/update.rs`), `core/player/slide_state/*` (283).
   - **Grind 400-405 / Nonspecific 701** ★: `game/physics/grind.rs` (≈60) + `game/physics/grind/advance.rs` (136) → `core/physics/grind_contact/*`, `grind_forces/*` (see §1.4).
   - **WipeoutGround** ◇: `game/physics/wipeout_states/*` (≈770), all ragdoll drives (see §1.5).
   - **Teleporting** ★: `core/player/teleport_state.rs` (71) → `game/physics/respawn.rs` (307) `request`.
10. ★ `solve::advance` (`game/physics/solve.rs` 147). For a board-only port, call `BoardRuntime::advance(contacts, truck_targets, step)` instead. It runs:
    - `colliders::world_volumes` (`game/physics/colliders.rs` 148): 4 wheel spheres, 2 truck capsules, deck rounded box and end capsules.
    - `BoardWorld::query_primitives` (`core/physics/world_contact/*` 2622, `collision/*` 655), with edge cos 0.999, padding 0.05, max separation 0.5 and convexity epsilon 0.01 (`game/physics/ground.rs:253`).
    - `BoardStep::advance[_attached]` (`core/physics/board_step.rs` 356):
      1. `apply_deck_forces` (`force_queue.rs` 100, `point_force.rs` 51).
      2. `prepare_drive_frames` (`truck_frames.rs` 46, `drive_frames.rs` 433).
      3. `contact::generate_contact` (330) and `contact_solver::build_contact_jacobian` (87).
      4. `BoardConstraints::build` (`assembly.rs` 204, `joint_builder.rs` 447, `joint_records.rs` 241, `drive_solver.rs` 128 + `drive_solver/builder.rs` 296, `drive_parameters.rs` 189).
      5. `solver::solve_constraints` (`solver.rs` 66 + `solver/*` ≈1065): per pass, contact → joint → drive, for RWMaxIterations passes.
      6. `rigid_body::integrate_body_rates` (433 + `rigid_body/dynamic_update.rs` 196).
      7. `board_reports::collect`.
11. ★ `physics.finish_skater` (`game/physics.rs:495`):
    - `riding.probes.prepare_wall` (`core/physics/board_probes.rs` 154).
    - `riding.finish_post_physics` → `BoardGroundState::update` (normals, contact counts, wheel angular drag), `sample_accelerations`, `BoardMotionOutput::from_board`.
    - `air_phase::update_apex` (latches when deck vy < 0).
    - `grind::post` (`core/physics/grind_forces/post.rs` 62).
    - **`wipeout::check_after_physics`** (`game/physics/wipeout.rs` 150 → `core/player/wipeout/*`).
12. ★ `respawn::observe` (records checkpoints) and `filtered_state` (`core/physics/filtered_state.rs` 215). The filtered-state transition to Ground triggers `core/animation/landing_quality.rs` (148).

### 1.1 Push / ride / steer, as a checklist

| # | File | Symbol | Lines | Notes |
|---|---|---|---|---|
| 1 | core/physics/board.rs | `BodyId`, retail geometry consts | 35 | 7 bodies: wheels 0-3 (RF, LF, RB, LB), FrontTruck 4, BackTruck 5, Deck 6 |
| 2 | core/physics/construction/{board_parts 104, authored_transforms 85, deck_geometry 331, primitive_mass 138, mass_moments 127, principal_axes 84} | `*::STOCK`, authored matrices | 869 | PURE |
| 3 | core/physics/mass.rs | body inertias, drag | 364 | deck angular drag ×60, wheel max ω 60000 |
| 4 | core/physics/rigid_body.rs + rigid_body/ | `RetailSimulationStep::fixed_60_hz`, `integrate_body_rates` | 629 | PURE |
| 5 | core/physics/{joint_builder, joint_records, drive_frames, drive_parameters, drive_solver(+builder), truck_frames, hook_drive, assembly} | 6 joints, 2 truck drives, 1 hook drive | ≈2076 | PURE |
| 6 | core/physics/{contact, contact_solver, contact_layout, constraint_frames, solver/*} | jacobians, PGS solver | ≈1500 | PURE |
| 7 | core/physics/{board_world + board_world/ (439), triangle_query, triangle_closest, triangle_sweep*, collision/*, world_contact/*} | `WorldTriangle`, `query_primitives`, `query_thin_line` | ≈4600 | PURE; the **Halo collision adapter** goes here |
| 8 | core/physics/{board_step, board_runtime, force_queue, point_force} | `BoardStep`, `BoardRuntime` | 945 | PURE |
| 9 | core/physics/{board_ground, board_probes, board_toolkit, board_motion_output, filtered_state} | wheel lines, ground state, toolkit | ≈1000 | PURE |
| 10 | core/riding/{ground_orientation, reckoning_frames, vector} + air body_spin ground part | reckoning frames | ≈600 | PURE plus a com_to_deck vector |
| 11 | core/riding/{push 112, braking 96, ground_force 44, slide_friction 47, speed_model 189, steering 130, speed_wobble 101, heading 115, straighten 38, anti_flip 50, pumping/* 363, speed_and_slope, push_behaviors 365} | force producers | ≈1700 | PURE |
| 12 | core/riding/grounded/{propulsion 136, drag 103, manual_entry 112, mode 24, state/* ≈2300} | ground state composition | ≈2700 | PURE except input flags |
| 13 | game/physics/ground_runtime/{update 121, input 274, services 293, settings 388}, ground_phase.rs 308, riding_outputs.rs 381 | host glue | ≈1700 | rewrite in C++ against a `SkateInput` struct |

### 1.2 Ollie / nollie

1. **Gesture.** `skater.pat` patterns "Ollie" and "Nollie": crouch then flick, e.g. `coord 0 1`, `coord 0 -1`, tolerance 0.55. A match emits intents `Trick`, `Ollie` and `GestureSpeed`.
2. **Graph** (◇). `game/graph_host/motion_tricks.rs` (257) `Operation::Height`:
   - TrickHeight comes from GestureSpeed.
   - It emits JumpHeightOverride = gesture × 0.2 (0.27 from manual, 1.0 from grind).
   - The clip fires `"jump"` at its pop frame, which sets flags2468 |= 0x400000.
3. **Strength.** `attribute_finalization.rs` `finish()` (l.104-128) gives `jump_strength` from 0 to 1. It snaps to MinJump or 1.0.
4. **Launch** (★ PURE, `core/air/ground_jump/mod.rs` `calculate`):
   - height = lerp(MinHeightVsSpeed(v), MaxHeightVsSpeed(v), strength), clamped to AbsoluteMinHeight.
   - remaining = max(height − (animCOM − groundRef)·up, 0).
   - v_up = sqrt(−2·g·remaining) × VerticalResponse(groundAngle), applied along the ground normal and added to the planar velocity.
   - If |v_cur − v_prep|² > 7.0, the speed is clamped by SpeedScalarVsAngle.
   - The Y bonus is capped at JumpYBonusMax. Stick adjust uses JumpAdjustX/ZFactor.
   - Drop-in: forward × 2.3 + prepared, then y += 1.
5. **First 12 air frames** (★ PURE, `core/air/state/jump_velocity.rs` `calculate_velocity_from_jump`):
   - predicted = v_jump + g·dt·n.
   - If vertical error ≤ −3, keep the current vertical velocity.
   - If horizontal error² ≥ 9, clamp the horizontal speed.
6. **Nollie** uses the same path; the Nose/Tail gesture tables remap the keys.

### 1.3 Flip tricks (flick-it)

1. **Recognizer** (★ PURE, `core/input/gesture.rs` 248, `Recognizer` / `Node`):
   - Each pattern has 2-15 points and `tolerance²`.
   - Sampling is once per 60 Hz tick. Y is negated, and any axis with |v| < 0.1 is zeroed (`game/input/gesture_input.rs` ~l.60).
   - A node activates when the sample is near p0 and advances when it is near the next point, accumulating dist² and ticks. A miss increments a counter, and the node resets once misses > `NumTicksPatternNotInRangeBeforeCulling`. Holding at p0 does not age the node.
   - Best match score = n⁴ / (clamp(dist/n, 0.15) · elapsed).
   - **GestureSpeed** = 1 if elapsed/n ≤ low, 0 if ≥ high, linear between. (low, high) = (1.75, 4.4), or (1.5, 3.0) when difficulty == 2.
   - One-tick refractory reset after a match. The match is "held" until the stick leaves the last point.
   - Hold patterns: Kickflip, Heelflip, N_Kickflip, N_Heelflip.
2. **Pattern sets** (◇ data): right stick uses `skater.pat`, `skater90.pat`, `skaterN90.pat`, `skater_air.pat`, `skater_fingerflip.pat`; left stick uses `skaterls.pat`, `skaterstep.pat`. **These files are missing; author your own.**
3. **Mapping** (★ embedded data):
   - `game/input/gesture_mapping_data.rs` (413) `TABLES[7]`: 270 rows of (gesture, normal, mirrored, dark-catch category). Groups: Square, Nose, Tail, 90Nose, 90Tail, N90Nose, N90Tail.
   - `gesture_catalog.rs` (149) and `gesture_mapping.rs` (125):
     - `select()`: hash bucket = sum of key words mod 43, plus the DarkCatch override.
     - `State::begin/update/end`: Kickflip → KickflipHold, plus the `U_` underflip prefix.
   - Mirrored pairs: Kickflip↔Heelflip, 360Flip↔Laserflip, Hardflip↔InwardHeelflip, PopShuvit↔FSPopShuvit.
   - Gate: `permitted()` checks actor-flag bits 3/4/5/8/12.
4. **Board rotation is animation, not impulses** (◇):
   - The board bone of the clip gives the target: `skeleton_air_frames::prepare_animated` → `BoardAnimation::apply`, which slerps the phys→anim error to identity using PhysToAnimSlow/Fast and stops once the fraction ≥ 0.95.
   - The target is applied with `set_hook_transform`. The hook drive is angular-only in air: spring 20000, damping 2√k ≈ 282.8, max 35999.996. The linear soft drive is 5000 / 1799.9998 and is disabled in air.
   - In KnownAir the board velocity is also forced to (target − deckCOM)/dt (`board_animation.rs:120`).
   - Multi-flips come from clip cycles `B_<TRICK>_CYC1..3`, gated by TimeToLand < 0.525 / 0.8 (see `game/tests/flip_playback.rs` 311).
5. **Procedural substitute.** Supply the board target orientation q(t) relative to takeoff yaw:
   - kickflip = +2π roll about the deck's long axis (Z) over T ≈ 0.42 s
   - heelflip = −2π roll
   - shove-it = π yaw
   - 360 flip = 2π roll + 2π yaw over 0.55 s
   - hardflip = 2π roll + π yaw over 0.48 s

   Timings are from kickflip-ops `skate.rs:104-121`. Keep the hook drive as the actuator so the board stays physical and can be caught or bailed.

### 1.4 Grinds / slides (detail is in the grind report embedded below)

- **Rails from a triangle soup** (★ PURE f64): `game/grind_world/derive.rs` (586) `rails()` line 227.
  - Weld vertices.
  - Keep edges with a top face whose n.y ≥ 0.7, a fold ≥ 40° (cos −0.766), a slope ≤ 0.7, a drop ≥ 0.25 found by an outer probe (0.12 out, 0.05 lift), and headroom 0.3.
  - Chain edges (cos 0.9, min length 0.6). Merge thin bars (0.35 / 0.05). Dedupe within 0.3. Grid cell 2.0.
  - **Run this offline on the Halo BSP at map load** (Y-up metres, see §5).
- **Spatial index:** `game/grind_world/{provider 349, octree 236, spline 193}` with ±1.2 m box queries. A simple AABB grid replaces it.
- **Detection:** `core/physics/grind_contact.rs` (326) has `deck_contact`, `truck_contacts`, `boardslide_candidate` (upright·up > 0.65) and `fifty_fifty_candidate_on_splines`. Further files:
  - `investigator.rs` (214) `investigate`
  - `families.rs` (278)
  - `admission.rs` (107): drop-in < 1.45 m/s; approach limits 17° / 90° / 24°
  - `entry.rs` (206) `Engagement::update`: tipslide window 8 frames
  - `manager.rs` (210)
  - `control.rs` (144)
  - `balance.rs` (287): target-up smoothing 0.925/0.075 (0.85/0.15 for tip states), 14° clamp, exit lean from the `ExitLeanAngleVsTime` curve, forced exit above 28°
- **Air → grind acquisition:** `core/air/trajectory/grind*` (≈690) and `core/physics/grind_air*` (≈550). Both need world ray probes.
- **Forces:** `core/physics/grind_forces.rs` (146) plus `grind_forces/*` (≈565): lateral pin, material friction ×{1.7, 2.6, 0.6, 4.0, 5.0, 1.3}, launch, release, slide, support, noise (needs an RNG; see `game/physics/grind/rng.rs`).
- **Host tick:** `game/physics/grind/advance.rs` (136). Exit rules:
  - speed < 0.15 after 60 updates
  - force flag
  - jump
  - leaving_updates > 35 → wipeout reason 17
- **Names:** `game/physics/grind_chromosome.rs` (250) and `grind_names.rs` (49), as static tables.
- **Input:** `core/input/grind_intentions.rs` (26), e.g. GrindBalanceX = −leftX (normally graph-filtered; feed it directly).

### 1.5 Manuals

- **Intent** (`core/input/manual_intentions.rs` 36): manual = ±|stick|. ManualBrake applies when |stick| > 0.9, scaled (len − 0.9) × ≈10.
- **Controller** (★ PURE except an angle trait; `core/physics/manual/{controller 219, settings 43, state 60, angle 30}`), called from `ordinary.rs:67`:
  - target tilt = sign·(0.25 + 0.75|bal|)·MaxTilt
  - error filter 0.95/0.05, then PID with D clamped
  - one-axle scaling ×0.5
  - brake force −400·v
- **Entry:** `core/riding/grounded/manual_entry.rs` (112).
- **Ending:** there is no fail condition in the manual code. The graph ends the manual; bails come from generic ground wipeout checks (deck error reason 3, balance reason 11).

### 1.6 Bails / wipeouts

- **Triggers** (★ PURE over a `Frame` struct; `core/player/wipeout/{data 109, ground 141, air 105, common 52, requests 71}`):
  - `air::bad_landing`: |landing angle| > curve(tangent speed)·scale → reason 5.
  - Into-surface speed > max_landing_speed (stairs variant) → reason 6.
  - Upside down → reason 4.
  - `ground::check`: deck error → 3; closing velocity → 2 (runout); balance accumulator → 11.
  - **Runout vs wipeout:** a single reason 2/24/10 with |up.y| > 0.81 counts as a runout.
  - Cooldowns: 0.04 s, or 0.4 s after a teleport.
  - Host wrapper: `game/physics/wipeout.rs` (150) `check_after_physics` (l.80).
- **Skeleton-only inputs:** `pose_error`, `maximum_pose_error` (squash) and `regions_force`. Pass 0 / false.
- **WipeoutGround (ragdoll)** (◇): `game/physics/wipeout_states/*` (≈770) and `core/player/wipeout_state/*` (1638). Replace it with one rigid capsule:
  - Enter: velocity limit 20.
  - Each tick: drag = clamp(0.15·settled + 0.05, 0, 0.5); limit vy spikes above 5.
- **Keep recovery verbatim:** `recovery.rs` (129), using the body velocity for hips and neck: `over` = speed < over_speed after min time, then a fade, then a 2-frame countdown → `request_teleport`.

### 1.7 Respawn

- `core/player/respawn.rs` (236) `History`:
  - Records after 120 measurements, with a 20-frame cooldown.
  - Skips candidates within 1.5 m of an existing one. Ring of 32 entries.
  - Surface scores: 100 / 50 / 20.
  - `pop_best` and `automatic` pick the respawn point.
- `core/player/teleport_state.rs` (71): next state 100 (on board).
- Host: `game/physics/respawn.rs` (307), with `request` l.53, `observe` l.80 and `heading` l.133. Its scene predicates use raycasts.
- In Halo, this can be replaced by the engine's own teleport. Keep `History` if you want "skate to last good spot".

---

## 2. Data dependencies

### 2.1 VLT/AttribSys Collections (`skater-collections.json`): **required, absent**

Loaded by `skate_data::collections::Collections::load`. Curves are `PointGraph`s (core `point_graph.rs` 34) of 8 or 16 points. **Stock values present in code:**

| Field | Value | Source |
|---|---|---|
| WheelRadius | 0.031 (0x3CFDF3B6) | core/physics/board.rs:30, construction/board_parts.rs |
| WheelMass × SkateboardMassFactor | 0.083 × 5.0 | board_parts.rs:19-46 |
| WheelXDist | 0.095 | board_parts.rs, authored_transforms.rs:25 |
| TruckMass × factor | 0.39 × 5 | board_parts.rs |
| Truck RadiusScalar / HalfHeightScalar | 0.5375 / 1.35 | board_parts.rs |
| TruckYPos / TruckZPosFront,Back | −0.0565 / −0.052 | board.rs:31-33 |
| TruckRotationAxisAngle | 31° | board.rs:34 |
| DeckMidLength / Width / Thickness / BackEndSize | 0.59 / 0.24 / 0.015 / 0.165 | construction/deck_geometry.rs STOCK |
| Deck end angles front / back, end capsules | 13° / 12.5°, 5 | deck_geometry.rs |
| Deck mass, angular drag | 6.0, 0.45 × 60 | mass.rs:184-191 (labelled stock in code) |
| Truck drives | angular displacement 10, damping 0, strength 11, hard, linear off | drive_parameters.rs:45 |
| Joints | truck twist limit 86.1°, twist angle 14°, wheel swing 1e6 | joint_records.rs:46 |
| Hook drive | soft angular 20000 / max 36000; soft linear 5000 / 1800 | hook_drive/mod.rs:40-46 |
| Skeleton hard target drive | velocity 600, strength 36000 | skeleton_body/targets.rs |
| Wheel line length / deck probe radius | 0.2 / 0.15 | board_ground.rs:18, board_probes.rs:55 |
| Air COM gravity | −9.8 (0xC11CCCCD) | game/physics/air_phase.rs:17 |
| Truck tightness / wheel hardness | 0.7 | AnimationProfile |
| Floor material | friction 0/0, restitution 1 (host hard-code) | game/physics/settings.rs |

**Values that are not in code (pick placeholders and tag `TODO-DATA`):**

| Group | Fields | Suggested placeholder / source |
|---|---|---|
| `physics` | WorldGravity, FreezingEnergy, RWMaxIterations | (0, −9.8, 0); iterations around 8-10 |
| Contact materials | Wheel, Deck and Truck Static/Dynamic Friction and Restitution | 0.8 / 0.7, as in the `tests/wheel_ground_solver.rs` fixture |
| `physics_reckoning` (17 fields) | GroundNormalSmoothing, UpVectorSmoothing*, … | |
| `physics_heading` | TurnTorqueVsSpeed/Slope, HeadingAdjustMaxSpeed | |
| `physics_steering` (12), `physics_speed_wobble` (12) | | |
| `physics_brakes` | FootBrakeForce, TailBrakeForce, SlowSpeed | |
| `physics_push` | MaxPushableSpeed | |
| `physics_mode/<easy\|normal\|hardcore>` | MaxPushDVStart/End, JumpMin/MaxHeight | |
| `physics_speed_conservation` | 5 fields | |
| `physics_surfaces/<5 surfaces>` | 6 fields each | |
| `physics_feet` | | |
| `physics_friction`, `physics_manual` | | MaxTilt, BrakeTilt, PID, NoiseVsSpeed |
| `physics_jump` | 11 fields and curves | |
| `physics_airstates` | PhysToAnimSlow/Fast | |
| `physics_wipeout` | max landing speed / angle curve, … | Test fixture `core/player/wipeout/tests.rs:74-120`: landing speed 6, stairs 8, grind 5 |
| `physics_grinds` | ExitLeanAngleVsTime, DVEntryThreshScalarVsSinSlope, … | |
| `physics_grinds_air` | | |
| `physics_trajectory` | ≈40 fields | |
| `anim_motion/pushing` | push curves | |
| `recognizer/*` | NumTicksPatternNotInRangeBeforeCulling | 3, from the gesture test |

**Can physics run without it?** The board-only solver can, using the STOCK consts plus guessed friction, gravity and iterations. Riding feel cannot: every force producer is parameterised by these fields. The C++ lib should expose one `SkateTuning` struct that mirrors these classes field-for-field, so real values can be dropped in later (the `tools/asset_pipeline` in skate3-mw2 knows the names).

### 2.2 ABIN animation banks (`OnBoard.abin`, `OffBoard.abin`) and `physics-skeletons.json`: **absent**

What the animation drives in physics:

1. **The skeleton bodies:** 26 rigid bodies with drive targets from the evaluated pose (`game/physics/skeleton_input_general.rs`, `skeleton_input_runtime.rs`). **The substitute is to drop them** and use rider mass on the deck or a single COM body.
2. **The deck→COM vector** `animation_com_to_deck_752` (and `_delta_768`): the mass-weighted COM of the 24 animated parts relative to the board bone (`core/physics/skeleton_animation_record.rs:146`). It feeds reckoning (`riding_outputs`), `air_reckoning`, `ground_animation` (the pop height "remaining" term), pumping and `center_of_mass_height`. **Substitute:** a constant ≈ (0, 0.9-1.0 m, 0) in board space. Lower it when crouching (the crouch scalar lerps it to about 0.65 m), and shift Z ± for nollie or manual lean.
3. **Board pose in air and during flips:** the clip board bone becomes the hook target (§1.3). **Substitute:** a procedural q(t) per trick.
4. **Push timing and strength:** push clips `R_PUSH{LSP,HSP}_{HSTR,LSTR}_{N,MONGO}_0_CYC1` give the length (frames/fps) and the `HStr_Vel_B` / `LStr_Vel_B` / `Vel_E` attributes (`game/graph_host/pushing_settings.rs`). The push target speed is the trajectory-bone length / dt. **Substitute:** push cycle about 0.6 s (kickflip-ops uses 0.62 s interval and a 1.7 m/s kick), with contact window flags bits 24-27 set for N ticks mid-cycle.
5. **Pop timing:** the `"jump"` attribute frame. **Substitute:** pop on stick release or flick-up, N ≈ 2-4 ticks after the ollie gesture completes.
6. **Scalar attributes that the graph derives from the stick:** Turn, HardTurn, RawTurn, Balance, Brake, Slide, BodySpin, Spin, FlipTrick. **Substitute:** filtered stick values.

### 2.3 Joystick gesture patterns (`private/stock/data/joystick/*.pat`): **absent**

- **Format** (`skate_data::gesture_patterns::parse`): `global_tolerance_dist d`, `pattern Name`, `tolerance_dist d`, `coord x y`, with `//` comments. Time and speed directives are ignored.
- **Known vectors:**
  - Ollie: `coord 0 1`, `coord 0 -1`, tolerance 0.55 (test fixture).
  - 360Flip: (−0.965714, 0.268571), (−0.497143, 0.84), (0.211429, 0.988571), (0.908571, −0.405714), tol² 0.16 (`core/input/gesture.rs:189-247`).
- **Substitute:** author `.pat` files for Ollie, Nollie, Kickflip, Heelflip, PopShuvit, FSPopShuvit, 360Flip, Hardflip, Varial, and so on, in the stick space after the Y negation (positive y = stick down). Ship the parser as-is.
- **Can physics run without it?** Yes. Only trick recognition is lost.

### 2.4 State graphs (`ActionGraph_OnBoard.stategraph`, `MotionGraph_OnBoard.stategraph`, via `private/game.json`): **absent**

They decide:
- every behavioural transition and which clip plays
- `ForcePhysics(mode)`: AnimationData 0, PhysicsBoard 1, AnimationBoard 2
- push ordering, trick attributes and scoring

The interpreter is `core/graph/*` (1092) plus `game/graph_host/*` (≈3000+). **Do not port it.** Replace it with a hand-written C++ state machine, `SkateIntentFSM`. It reads `SkateInput` and the physical state, and writes the `ProcessedPhysicsInput`-equivalent fields that physics consumes:
- push bits 24-27, brake bits 23/29/30, carve bit 31
- PrepareJump 0x200000 and Jump 0x400000
- jump_strength
- FlipTrick (flags2472 bit 22), board-animated / force_mode, PlayingLanding
- turn / balance / bodyspin scalars
- manual sign and balance
- grind balance / translation

### 2.5 Procedural substitute layer: what it must provide each tick

`struct AnimSubstitute` should provide:
1. Processed flags and scalars (§2.4).
2. `com_to_deck` (Vector3, board space).
3. In air, `board_target` (quaternion, plus position = predicted board position). Leave it unset on ground.
4. Push contact window and push target speed.
5. Pop event and `jump_strength`.
6. PhysToAnim catch blend rate (fixed slerp, e.g. 0.15/tick).
7. Body-spin target (deg).
8. Landing-quality inputs (spin, side).
9. For the wipeout `Frame`: deck-vs-animation angle error, which is 0 when the anim board equals the phys board.

---

## 3. Port mechanically vs coupled

**Pure math, port mechanically** (non-test lines, approximate; many files contain inline tests):

| Area | Paths | ≈Lines |
|---|---|---|
| Board rigid-body stack: bodies, construction, mass, integrator, joints, drives, contact, solver, force queue | core/physics/{board, construction/, mass, rigid_body*, joint_*, drive_*, truck_frames, hook_drive, assembly, contact*, solver*, constraint_*, force_queue, point_force, board_step, board_runtime} | 5,500 |
| Triangle world and queries | core/physics/{board_world*, triangle_*, collision/, world_contact/} | 4,600 |
| Ground state and probes | core/physics/{board_ground, board_probes, board_toolkit, board_motion_output, filtered_state, deck_angular_correction, centre_of_mass_filter} | 1,300 |
| Riding force producers and grounded state | core/riding/* | 6,000 |
| Jump, air state, reckoning, spin, flip | core/air/{ground_jump, state, reckoning, body_spin, body_flip} | 1,700 (plus known 970, trajectory 2,050 optional) |
| Manual | core/physics/manual/* | 350 |
| Grinds | core/physics/{grind_contact*, grind_forces*, grind_air*}, core/air/trajectory/grind*, game/grind_world/derive.rs | 3,800 |
| Wipeout triggers, recovery, respawn, selector | core/player/{wipeout, wipeout_state/recovery, respawn, teleport_state, selector} | 1,600 |
| Input: gesture recognizer, stick math, intentions; mapping tables | core/input/{gesture, trick_intentions, controller, history, manual_intentions, grind_intentions, steering_intentions, riding_intentions}; game/input/gesture_* | 2,000 + 690 |
| Landing quality | core/animation/landing_quality.rs | 150 |

Total ≈ 27-30k Rust lines for full stages A-D. Expect a C++ ratio of about 0.8-1.0×.

**Deeply coupled; replace, do not port:**

| Area | Paths | ≈Lines |
|---|---|---|
| Graph interpreter and leaves | core/graph, game/graph_host | 4k |
| Animation evaluation, playback, VBR, foot IK, posture | core/animation/* except landing_quality | 8.5k |
| Skeleton ragdoll | core/physics/skeleton_*, core/player/wipeout_state body/drives | 4k |
| Input-phase packet translation | core/player/input_phase, core/animation/skeleton_input/* | 2.6k (re-implement only the outputs) |
| Off-board walking | core/player/offboard | 14k (skip) |
| Camera | core/camera | 6k (skip; Halo has its own) |
| Scoring | core/scoring | 1.1k (optional) |
| Audio, render, multiplayer | game/* | skip |

---

## 4. Staged porting order

All stages share three conventions:
- **Internal convention:** Skate native (Y-up, metres, 60 Hz). Convert only at the Halo boundary (§5).
- **Test harness:** C++ `skate_tests` with hand-made triangle-soup fixtures (flat plane, 15° ramp, box ledge, quarter-pipe strip, rail bar).
- **Tests:** port the Rust `#[test]`s listed for each stage as vectors. Bit-exact hex constants should be compared with `memcpy` to `uint32_t`.

### Stage A: ride, ollie and land on a triangle soup

**Port, in this order:**
1. `core/math.rs`, `trigonometry.rs` (149), `point_graph.rs`, `riding/vector.rs`.
2. `core/physics/board.rs`, `construction/*`, `mass.rs`, `rigid_body.rs` + `rigid_body/`.
3. `joint_builder`, `joint_records`, `drive_frames`, `drive_parameters`, `drive_solver` + `builder`, `truck_frames`, `hook_drive`, `assembly`.
4. `contact`, `contact_solver`, `contact_layout`, `constraint_frames`, `solver.rs` + `solver/*`, `force_queue`, `point_force`, `board_step`, `board_runtime`.
5. `board_world` + `board_world/*`, `triangle_query/closest/sweep*`, `collision/*`, `world_contact/*`.
6. `board_ground`, `board_probes`, `board_toolkit`, `board_motion_output`, `filtered_state`, `deck_angular_correction`.
7. `core/riding/*` (all) and `riding/grounded/*`. Host glue comes from `game/physics/{ground_runtime/*, ground_phase.rs, riding_outputs.rs, colliders.rs, ground.rs (query settings l.253)}`.
8. `core/air/ground_jump/*`, `core/air/state/*`, `air/reckoning/*`, `air/body_spin.rs`, plus host `game/physics/{air_phase.rs, ground_animation/board.rs, air_reckoning.rs}`.
9. `core/player/selector/*` (ground ↔ air ↔ groundanimation only), `core/animation/landing_quality.rs`.
10. `core/input/{controller, steering_intentions, riding_intentions, trick_intentions}` and the `AnimSubstitute` and `SkateIntentFSM` from §2.5 (push, pop).

**Reusable tests:**

| Test file | Lines | What it covers |
|---|---|---|
| `core/physics/tests/joint_records.rs` | | 32 bit-exact hex words |
| `tests/drive_frames.rs` | | 17 checks, stock truck transforms, wheels on y = 0 |
| `tests/contact.rs` | | tangent constants |
| `tests/board_ground.rs` | 255 | |
| `tests/board_step.rs` | 331 | push momentum = F·dt within 1e-4; steering reaches the wheels; braking |
| `tests/board_world.rs` | 273 | 4 ordered wheel contacts, backface rejection, fatness |
| `tests/rigid_body.rs` | 274 | |
| `tests/primitive_query.rs` | 277 | |
| `tests/world_dispatch.rs` | 353 | |
| `tests/board_runtime.rs` | 244 | |
| `tests/hook_step`, `tests/mass_moments`, `tests/drive_solver`, `tests/joint_builder`, `tests/triangle_query`, `tests/assembly` | | `assembly` checks 6 joints and 3 drives |
| `core/physics/board_world/tests.rs` | 276 | |
| `skate-core/tests/wheel_ground_solver.rs` | 190 | g = 9.8, friction 0.8 / 0.7, rolling, static → dynamic transition |
| `tests/contact_iteration.rs`, `constraint_batch.rs` | | |
| `tests/physics_rebuild_{mass,constraints,contact_build,authored_transforms}.rs` | | |
| `skate-core/tests/riding_grounded_propulsion.rs` | 382 | exact brake (−1, 0, 0), push (0, 24, 36), queue tags [2, 3] |
| `tests/board_toolkit_speed.rs` | | signed-zero cases |
| `core/riding/tests/push.rs` | 82 | target 10, scale 0.5, dt 0.125, limits (8.5, 2, 1): z = 8.0; at 4.25: 6.0; at 8.25: 1.0; above 9: 0 |
| `core/riding/tests/{steering, push_behaviors, ground_orientation, speed_and_slope}` | | |
| `skate-core/tests/riding_pumping_controller.rs` | 338 | |
| `core/air/state/tests/mod.rs` | 551 | `jump_velocity…` l.375 → [2, 4, 0, 5]; com launch l.391 |
| `core/air/reckoning/tests.rs` | | |
| `core/player/selector/tests.rs` | 270 | |
| `core/input/trick_intentions.rs` tests | | (0.25, −0.5) → angle 0.4636476, magnitude 0.559017 |

**Acceptance criteria (tick the board-only `BoardRuntime::advance` path):**
- The board spawned 5 cm above a plane settles and rolls straight.
- A push accelerates the board to MaxPushableSpeed.
- Steering carves.
- The ollie apex height matches `ground_jump` for the given strength.
- Landing at ≤ 30° returns to PhysicsGround with no NaN.

### Stage B: flick tricks

**Port:**
- `core/input/gesture.rs` (Recognizer), `core/input/history.rs`, `body_flip_signal.rs`.
- `game/input/{gesture_input.rs, gesture_mapping.rs, gesture_mapping_data.rs, gesture_catalog.rs}`.
- `core/physics/board_animation.rs`, `skeleton_air_frames.rs` (board-target part only).
- `core/air/body_flip.rs`.
- `core/player/wipeout/air.rs` (flip-upside-down reason 4, bad_landing reason 5 / 6).

**Write new:**
- the `.pat` set
- the procedural trick table: name → (roll turns, yaw turns, duration, axis sign)
- hold handling (KickflipHold)

**Tests:**
- `core/input/gesture.rs:189-247`: the 360Flip vector. Hold p0 for 120 ticks, then p1..p3 → (pattern 0, strength 1.0, elapsed 4). Disconnected or reversed input gives no match.
- `game/input/gesture_mapping.rs` tests: 360 ↔ Laser mirror, Kickflip → KickflipHold.
- `core/input/history_tests.rs` (5).
- `core/air/known/tests/mod.rs` (10, if KnownAir is ported).
- `game/tests/flip_playback.rs` doc comments give the expected multi-flip gating.

### Stage C: grinds and manual

**Port, manual:**
- `core/physics/manual/*`, `core/riding/grounded/manual_entry.rs`, `core/input/manual_intentions.rs`.
- Hook-up already exists in `ordinary.rs:67`.

**Port, grind:**
- `game/grind_world/derive.rs`, run on the Halo BSP at load, plus a simple rail index.
- `core/physics/grind_contact.rs` + `grind_contact/*`, `grind_forces.rs` + `grind_forces/*`.
- `core/input/grind_intentions.rs`.
- Host `game/physics/grind/{advance.rs, post.rs, rng.rs}`, `game/physics/player_input/grind/{pre.rs, post.rs}`, `game/physics/grind_materials.rs`, `grind_air_settings.rs`.
- `grind_chromosome.rs` and `grind_names.rs` for names.
- Optional: air acquisition via `core/air/trajectory/grind*` and `core/physics/grind_air*`.

**Tests:**

| Test file | Count |
|---|---|
| `core/physics/grind_contact/balance_tests.rs` (421) | 12: 14° clamp, entry delays, 15° / 28° force exit |
| `admission_tests.rs` | 4 |
| `entry_tests.rs` | 5 |
| `manager_tests.rs` | 5 |
| `grind_contact/tests.rs` | 4 |
| `investigator_tests.rs` | 1 |
| `core/physics/grind_forces/tests.rs` | 5 |
| `core/air/trajectory/grind_surface/tests.rs` | 8 |
| `core/input/grind_intentions/tests.rs` | 3 |
| `game/grind_world/derive.rs` tests (l.445+) | flat → 0 edges; ledge box → 4 edges at y = 0.5; stairs; thin bar → 1 centred rail; wide bar; winding independence |
| `game/grind_world/tests.rs` | 10 |
| `game/physics/player_input/grind/{tests, world_tests}.rs` | 6 + 11 |
| `skate-core/tests/riding_manual.rs` | 11 |
| `riding_manual_entry.rs` | 7 |
| `riding_manual_physical.rs` | 3 |
| `manual_input_animation.rs` | 3 |
| `tailwalk_input.rs` | |

### Stage D: bails and respawn

**Port:**
- `core/player/wipeout/*` (all triggers, with pose_error and regions_force = 0) and `core/player/wipeout_state/recovery.rs` (verbatim).
- `core/player/respawn.rs`, `core/player/teleport_state.rs`.
- Host `game/physics/wipeout.rs` and `game/physics/respawn.rs` (raycast predicates).

**Write new:**
- the single-body ragdoll stand-in (§1.6)
- the board left as a free 7-body object during the bail (collision group 7 in retail)

**Tests:**
- `core/player/wipeout/tests.rs` (6). The fixture Settings is at l.74-120.
- `core/player/tests/respawn.rs` (6).
- `core/player/tests/lifecycle.rs` (378).

---

## 5. Conventions and Halo conversion

| | Skate native (this code) | Halo CE |
|---|---|---|
| Units | metres (wheel r 0.031, deck half-length 0.59) | 1 world unit = 10 ft = 3.048 m |
| Up | +Y | +Z |
| Board forward / nose | +Z (front truck at +Z) | +X (forward) |
| Board +X | rider's **left** (RightFrontWheel = index 0 at x = −0.095) | +Y = left |
| Handedness | right-handed (X = Y × Z, i.e. up × forward = left) | right-handed |
| Basis storage | `Basis3.columns`, column vectors in Ri/Up/At order (X, Y, Z); `RetailAffineTransform` = basis + translation (`core/physics/drive_frames.rs:34`) | |
| Quaternion | (x, y, z, w) lanes (`rigid_body.rs:145`) | Halo uses (i, j, k, w) |
| Gravity | (0, −9.8, 0) (air COM uses 0xC11CCCCD = −9.8; WorldGravity itself is data) | Halo default ≈ 9.8 m/s² equivalent (in wu/tick²) |
| Fixed step | 1/60 s = `f32::from_bits(0x3C888889)` (0.016666668) for physics, input, gestures and timers. `joint_records` uses frequency 0x426FFFFF (59.999996). `clock.rs` slow-mo changes only the wall timer, never dt. | 30 Hz game tick |

**Axis mapping:** this is a cyclic permutation, so det = +1 and cross products are preserved.

```
halo.x = skate.z      skate.x = halo.y
halo.y = skate.x      skate.y = halo.z
halo.z = skate.y      skate.z = halo.x
```

Positions: `halo = perm(skate) / 3.048` and `skate = perm⁻¹(halo) × 3.048`. Velocities convert the same way, then divide by the tick rate as Halo needs. Quaternions: permute the vector part the same way and keep w. Add a C++ unit test asserting `perm(a × b) == perm(a) × perm(b)`.

**Timestep:** keep dt = 1/60 internally and **run 2 substeps per Halo 30 Hz tick**. Do not change dt to 1/30: many constants are per-tick, e.g.
- 12-frame jump correction
- tipslide 8 frames
- grind exit 60 / 35 updates
- respawn 120 / 20 frames
- `DeckAngularDrag × 60`
- gesture thresholds 1.75 / 4.4 ticks per point
- trajectory requery every 7 ticks

For input, sample the Halo stick once per Halo tick and feed both substeps. Better, feed substep 1 the midpoint between the previous and current sample, so flicks keep 2 samples per Halo tick for the recognizer.

**Collision adapter:** build `WorldTriangle` from the Halo BSP surfaces near the skater, transformed to Skate space, each tick or cached per cluster. You need:
- vertices
- normal
- edge cosines with the `USE_EDGE_COSINES = 0x100` and `ONE_SIDED = 0x10` flags (compute adjacency from the BSP edges)
- material (static and dynamic friction, restitution)
- tag (`>>7 & 31` = physics surface: smooth / rough / slow / slippery / veryslow)

Wheel lines can use the native segment-triangle query on the same gathered set, or Halo `collision_test_vector`; return fraction, normal and tag. The gather radius is about 2 m around the deck. `QueryMetadata` is an optional broadphase; without it the query scans linearly, which is fine for under 500 triangles.

**Spawn:** `translation.y = ground + WheelRadius − authored[0].translation.y` (`game/physics.rs:300-320`). Heading is `rotation_y(h)`.
