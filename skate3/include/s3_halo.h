/* s3_halo.h: C API over the full Skate 3 runtime (skate3/src/s3, s3::game::driver::Sim) for a host game.
 * Only scalars and pointers cross it; nothing throws. Coordinates are the runtime's: Y up, metres.
 * Every call returns 0 / NULL on failure (see s3h_error). Not thread safe: call from one thread. */
#ifndef S3_HALO_H
#define S3_HALO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the data pack (core.pack, fetched at run time; never shipped) */
/* Allocate a buffer of `size` bytes for the host to fill with core.pack. */
uint8_t *s3h_pack_alloc(uint32_t size);
/* Install the filled buffer as the VFS. Returns the file count. */
int s3h_pack_install(void);
int s3h_pack_ready(void);

/* ---- the world: a triangle soup, outward (counter-clockwise seen from outside) winding */
void s3h_world_begin(void);
/* `xyz` = 9 floats per triangle. Degenerate triangles are dropped. */
void s3h_world_add(const float *xyz, int count);
int s3h_world_count(void);
/* Build the collision world (rails are derived from it) and load the simulation, skater at `spawn`
 * facing `forward`. Returns 1 on success. Replaces any previous world and simulation. */
int s3h_world_end(const float spawn[3], const float forward[3]);
int s3h_ready(void);
/* Drop the simulation and world (frees memory; the pack stays). */
void s3h_unload(void);

/* ---- control */
/* Teleport the skater (on the board) to `pos` facing `forward`; applied on the next step. */
int s3h_travel(const float pos[3], const float forward[3]);
/* One fixed tick (s3h_time_step seconds) with a raw XInput sample. */
int s3h_step(int buttons, int lx, int ly, int rx, int ry, int lt, int rt);
float s3h_time_step(void);
void s3h_set_aspect(float aspect);

/* ---- output of the last tick
 * view: [0..15] root animation_to_world (column-major), [16..18] camera position, [19..27] camera basis columns
 * right/up/at, [28] fov degrees, [29] has camera, [30] camera discontinuity, [31] teleport, [32] period s,
 * [33] pose generation, [34] board speed, [35] wheel surface, [36] grind surface, [37] powersliding,
 * [38] footstep strength, [39] foot push speed, [40] grounded, [41] wiping out, [42] landed, [43] contacts,
 * [44] physical state id, [45] tick, [46] sequence score, [47] multiplier, [48] line score, [49] total score,
 * [50..52] rider root position, [53..55] rider root velocity. */
#define S3H_VIEW_FLOATS 64
const float *s3h_view(void);
/* 7 bodies (wheel_rf, wheel_lf, wheel_rb, wheel_lb, truck_f, truck_b, deck) x 13 floats:
 * position 3, orientation 4 (x y z w), linear velocity 3, angular velocity 3. */
#define S3H_BOARD_DECK 6
const float *s3h_board(void);
/* This tick's events, one per line (TRICK:x, SCORE:x, LANDING, WIPEOUT, BAIL, AIRBORNE, LANDED:typeN, STATE:a>b...). */
const char *s3h_events(void);
const char *s3h_state_name(void);
const char *s3h_motion_state(void);
const char *s3h_trick_name(void);
const char *s3h_score_name(void);
const char *s3h_error(void);

/* XInput button bits (for building the pad). */
#define S3H_DPAD_UP 0x0001
#define S3H_DPAD_DOWN 0x0002
#define S3H_DPAD_LEFT 0x0004
#define S3H_DPAD_RIGHT 0x0008
#define S3H_START 0x0010
#define S3H_BACK 0x0020
#define S3H_LTHUMB 0x0040
#define S3H_RTHUMB 0x0080
#define S3H_LB 0x0100
#define S3H_RB 0x0200
#define S3H_A 0x1000
#define S3H_B 0x2000
#define S3H_X 0x4000
#define S3H_Y 0x8000

#ifdef __cplusplus
}
#endif

#endif
