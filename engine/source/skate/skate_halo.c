/*
SKATE_HALO.C

HaloSkate3: the player's biped rides a skateboard (../../skate, the C++ port of skate-core).

The skate library works in skate space (metres, +Y up, nose +Z at heading 0); Halo in world units
(10 ft = 3.048 m), +Z up, at 30 ticks a second. The axis change is a cyclic permutation, so it
keeps handedness:  halo = (z, x, y) / 3.048,  skate = (y, z, x) * 3.048.

While skating, biped_update hands the player's biped to skate_halo_update_biped instead of
Halo's own walking: the board moves the biped, the director shows the chase camera, and Halo
keeps everything else (aiming, shooting, damage, AI, scripts).

Controls (keyboard / gamepad):
	B / d-pad down (pad only)   get on or off the board
	W, Space / A                push          S / left stick back   brake
	A D / left stick            carve, spin in the air
	arrows / right stick        flick-it: pull back then flick (ollie, kickflip, 360 flip...)
	Shift                       manual        C / left stick click  nose manual
	Q / white, black            grabs         X (key) / X (pad)     powerslide (with a carve)
	E                           still Halo's action: vehicles, weapons
*/

#include "cseries.h"
#include "cutscene/cinematics.h"
#include "game/game.h"
#include "game/players.h"
#include "input/input.h"
#include "objects/objects.h"
#include "physics/collision_bsp.h"
#include "physics/collision_bsp_definitions.h"
#include "physics/collisions.h"
#include "scenario/scenario.h"
#include "rasterizer/rasterizer.h"
#include "units/bipeds.h"
#include "units/units.h"
#include "skate_halo.h"
#include "../../../skate/include/skate_api.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- the platform layer's (port/linux/src) */

void platform_log(char const *format, ...);
void web_note_skate(char const *json); /* port/web/src/web_main.c */
extern unsigned skate_platform_keys;
extern short skate_platform_right_stick[2];
extern int skate_platform_skating;

/* ---------- constants */

#define METRES_PER_WORLD_UNIT 3.048f
#define HALO_TICKS_PER_SECOND 30.f
#define DECK_HEIGHT_METRES 0.11f

/* ---------- prototypes */

static void skate_halo_board_pose(skate_pose const *pose);
static void skate_halo_hud_event(skate_event const *event);
static void skate_halo_hud_status(skate_pose const *pose);
static void skate_halo_hud_post(char const *json);
static void skate_halo_derive_rails(void);

/* ---------- globals */

static struct
{
	boolean initialized;
	boolean skating;
	boolean auto_mount;
	boolean fast_cinematics; /* tests: HALO_SKATE_FAST=1 runs cutscenes at 8x */
	boolean rail_test;       /* tests: HALO_SKATE_RAILTEST=1 drops the rider onto the nearest rail */
	long biped_index;
	skate_world *world;
	skate_skater *skater;
	unsigned previous_keys;
	short previous_dpad_down;
	short previous_right_thumb;
	long ticks;
	unsigned long long last_banked;
	float stick_previous[2];
	real feet_height; /* the biped's origin above its feet, world units */
	void *rails_bsp;  /* the collision BSP the rails were derived from */
	skate_pose pose;  /* the latest, for the board's render */
	boolean have_pose;
	boolean reenter; /* the rider sat in a vehicle: back on the board where they got out */
} skate_globals;

/* ---------- conversion */

static void halo_to_skate(real_point3d const *h, float *s)
{
	s[0] = h->y * METRES_PER_WORLD_UNIT;
	s[1] = h->z * METRES_PER_WORLD_UNIT;
	s[2] = h->x * METRES_PER_WORLD_UNIT;
}

static void skate_to_halo(float const *s, real_point3d *h)
{
	h->x = s[2] / METRES_PER_WORLD_UNIT;
	h->y = s[0] / METRES_PER_WORLD_UNIT;
	h->z = s[1] / METRES_PER_WORLD_UNIT;
}

/* ---------- collision for the library */

static int skate_halo_raycast(void *user, float const origin[3], float const delta[3], skate_hit *hit)
{
	long ignore = *(long const *)user;
	real_point3d point;
	real_vector3d vector;
	struct collision_result collision;
	unsigned long flags =
		FLAG(_collision_test_front_facing_surfaces_bit) |
		FLAG(_collision_test_structure_bit) |
		FLAG(_collision_test_objects_bit) |
		_collision_test_objects_sight_blocking_flags;

	skate_to_halo(origin, &point);
	skate_to_halo(delta, (real_point3d *)&vector);
	if (!collision_test_vector(flags, &point, &vector, ignore, &collision))
		return 0;

	halo_to_skate(&collision.point, hit->point);
	halo_to_skate((real_point3d *)&collision.plane.n, hit->normal);
	hit->fraction = collision.t;
	hit->surface = collision.material_type;
	return 1;
}

/* ---------- input */

static float stick_value(short value)
{
	float v = (float)value / 32767.f;

	if (v > 1.f)
		v = 1.f;
	if (v < -1.f)
		v = -1.f;
	return v;
}

static void skate_halo_read_input(short gamepad_index, skate_input *in)
{
	struct gamepad_state const *pad = NULL;
	unsigned keys = skate_platform_keys;
	float rx = 0.f;
	float ry = 0.f;

	memset(in, 0, sizeof(*in));
	if (gamepad_index != NONE && input_has_gamepad(gamepad_index))
		pad = input_get_gamepad_state(gamepad_index);
	if (pad)
	{
		in->left_stick[0] = stick_value(pad->sticks[_gamepad_stick_left].x);
		in->left_stick[1] = stick_value(pad->sticks[_gamepad_stick_left].y);
		/* the raw stick: the game's copy is centred while skating (port/linux/src/xinput_sdl.c) */
		rx = stick_value(skate_platform_right_stick[0]);
		ry = stick_value(skate_platform_right_stick[1]);
		if (pad->analog_buttons[_gamepad_analog_button_a] > 64 || in->left_stick[1] > 0.5f)
			in->buttons |= SKATE_BUTTON_PUSH;
		if (in->left_stick[1] < -0.5f)
			in->buttons |= SKATE_BUTTON_BRAKE;
		if (pad->analog_buttons[_gamepad_analog_button_white] > 64 && !(keys & SKATE_KEY_Q))
			in->buttons |= SKATE_BUTTON_GRAB_LEFT;
		if (pad->analog_buttons[_gamepad_analog_button_black] > 64 && !(keys & SKATE_KEY_X))
			in->buttons |= SKATE_BUTTON_GRAB_RIGHT;
		if (pad->analog_buttons[_gamepad_analog_button_x] > 64 && !(keys & SKATE_KEY_E))
			in->buttons |= SKATE_BUTTON_POWERSLIDE;
		if (pad->buttons[_gamepad_binary_button_left_thumb])
			in->buttons |= SKATE_BUTTON_NOSE_MANUAL;
	}

	/* the arrow keys flick like a stick (digital: full deflection, diagonals on the circle) */
	{
		int x = ((keys & SKATE_KEY_RIGHT) ? 1 : 0) - ((keys & SKATE_KEY_LEFT) ? 1 : 0);
		int y = ((keys & SKATE_KEY_UP) ? 1 : 0) - ((keys & SKATE_KEY_DOWN) ? 1 : 0);

		if (x || y)
		{
			float length = (x && y) ? 0.70710678f : 1.f;
			rx = (float)x * length;
			ry = (float)y * length;
		}
	}
	/* the keyboard directly (its emulated pad is merged into Halo's own input elsewhere) */
	{
		int x = ((keys & SKATE_KEY_D) ? 1 : 0) - ((keys & SKATE_KEY_A) ? 1 : 0);

		if (x)
			in->left_stick[0] = (float)x;
		if (keys & SKATE_KEY_W)
			in->left_stick[1] = 1.f;
		if (keys & SKATE_KEY_S)
			in->left_stick[1] = -1.f;
	}
	if (keys & (SKATE_KEY_SPACE | SKATE_KEY_W))
		in->buttons |= SKATE_BUTTON_PUSH;
	if (keys & SKATE_KEY_S)
		in->buttons |= SKATE_BUTTON_BRAKE;
	if (keys & SKATE_KEY_SHIFT)
		in->buttons |= SKATE_BUTTON_MANUAL;
	if (keys & SKATE_KEY_C)
		in->buttons |= SKATE_BUTTON_NOSE_MANUAL;
	if (keys & SKATE_KEY_Q)
		in->buttons |= SKATE_BUTTON_GRAB_LEFT;
	if (keys & SKATE_KEY_X)
		in->buttons = (in->buttons & ~SKATE_BUTTON_GRAB_RIGHT) | SKATE_BUTTON_POWERSLIDE;

	in->right_stick[0] = rx;
	in->right_stick[1] = ry;
}

/* ---------- grind rails

Halo's levels have no grind splines: skate-game's derivation (skate/src/derive.cpp) finds them in the
level's collision, once per structure BSP (a level can switch BSPs as you play through it). */

static void skate_halo_derive_rails(void)
{
	struct collision_bsp *bsp = global_collision_bsp_get();
	long surface_index;
	long point_total = 0;
	long triangle_total = 0;
	float *vertices;
	int *indices;
	long v = 0;
	long t = 0;
	real_point3d points[MAXIMUM_VERTICES_PER_COLLISION_SURFACE];
	int found;

	if (!bsp || (void *)bsp == skate_globals.rails_bsp || !skate_globals.world)
		return;
	skate_globals.rails_bsp = bsp;
	skate_world_clear_rails(skate_globals.world);

	for (surface_index = 0; surface_index < bsp->surfaces.count; ++surface_index)
	{
		short count = collision_surface_polygon(bsp, surface_index, points);
		if (count >= 3)
		{
			point_total += count;
			triangle_total += count - 2;
		}
	}
	vertices = (float *)malloc(sizeof(float) * 3 * (size_t)point_total);
	indices = (int *)malloc(sizeof(int) * 3 * (size_t)triangle_total);
	if (!vertices || !indices)
	{
		free(vertices);
		free(indices);
		return;
	}
	for (surface_index = 0; surface_index < bsp->surfaces.count; ++surface_index)
	{
		short count = collision_surface_polygon(bsp, surface_index, points);
		short i;
		long first = v;
		if (count < 3)
			continue;
		for (i = 0; i < count; ++i)
		{
			halo_to_skate(&points[i], &vertices[v * 3]);
			++v;
		}
		for (i = 1; i + 1 < count; ++i)
		{
			indices[t * 3 + 0] = (int)first;
			indices[t * 3 + 1] = (int)(first + i);
			indices[t * 3 + 2] = (int)(first + i + 1);
			++t;
		}
	}
	found = skate_world_derive_rails(skate_globals.world, vertices, (int)v, indices, (int)t);
	free(vertices);
	free(indices);
	platform_log("skate: %d grind rails derived from %ld collision surfaces (%ld triangles)",
		found, bsp->surfaces.count, t);
}

/* ---------- mounting */

static void skate_halo_mount(long biped_index)
{
	struct biped_datum *biped = biped_get(biped_index);
	float position[3];
	float velocity[3];
	real_vector3d forward;
	float facing;

	unit_get_facing_vector(biped_index, &forward);
	facing = (float)atan2(forward.j, forward.i);
	if (!skate_globals.skater)
		skate_globals.skater = skate_skater_new();
	/* one world; its raycasts ignore whichever biped rides */
	if (!skate_globals.world)
		skate_globals.world = skate_world_new(skate_halo_raycast, &skate_globals.biped_index);
	skate_globals.biped_index = biped_index;
	skate_halo_derive_rails();

	/* the biped's origin is at its feet (the pill's bottom) */
	skate_globals.feet_height = 0.f;
	{
		real_point3d feet = biped->object.position;
		feet.z -= skate_globals.feet_height;
		halo_to_skate(&feet, position);
	}
	position[1] += 0.05f;
	halo_to_skate((real_point3d *)&biped->object.translational_velocity, velocity);
	velocity[0] *= HALO_TICKS_PER_SECOND;
	velocity[1] *= HALO_TICKS_PER_SECOND;
	velocity[2] *= HALO_TICKS_PER_SECOND;
	skate_skater_enter(skate_globals.skater, position, velocity, facing);
	skate_globals.skating = TRUE;
	skate_globals.biped_index = biped_index;
	skate_halo_hud_post("{\"e\":\"on\"}");
	platform_log("skate: on the board at %.2f %.2f %.2f (feet %.3f below the origin)", biped->object.position.x,
		biped->object.position.y, biped->object.position.z, skate_globals.feet_height);
}

static void skate_halo_dismount(char const *why)
{
	if (skate_globals.skating && skate_globals.biped_index != NONE)
	{
		struct biped_datum *biped = biped_get(skate_globals.biped_index);

		/* hand the momentum back to Halo's walking physics */
		SET_FLAG(biped->biped.flags, _biped_airborne_bit, TRUE);
	}
	skate_globals.skating = FALSE;
	skate_globals.biped_index = NONE;
	skate_platform_skating = FALSE;
	skate_halo_hud_post("{\"e\":\"off\"}");
	platform_log("skate: off the board (%s)", why);
}

/* ---------- public code */

void skate_halo_initialize(void)
{
	char const *automatic = getenv("HALO_SKATE_AUTO");

	memset(&skate_globals, 0, sizeof(skate_globals));
	skate_globals.biped_index = NONE;
	skate_globals.auto_mount = automatic && automatic[0] == '1';
	automatic = getenv("HALO_SKATE_FAST");
	skate_globals.fast_cinematics = automatic && automatic[0] == '1';
	automatic = getenv("HALO_SKATE_RAILTEST");
	skate_globals.rail_test = automatic && automatic[0] == '1';
	skate_globals.initialized = TRUE;
	platform_log("skate: ready (HaloSkate3, auto mount %s)", skate_globals.auto_mount ? "on" : "off");
}

/* cutscenes and scripted moments own the player: the board waits for them */
static boolean skate_halo_paused(long unit_index)
{
	if (cinematic_in_progress() || !player_input_enabled())
		return TRUE;
	if (unit_index != NONE && TEST_FLAG(unit_get(unit_index)->unit.flags, _unit_suspended_bit))
		return TRUE;
	return FALSE;
}

boolean skate_halo_unit_is_skating(long unit_index)
{
	if (!skate_globals.skating || unit_index == NONE || unit_index != skate_globals.biped_index)
		return FALSE;
	if (object_get(unit_index)->object.parent_object_index != NONE || skate_halo_paused(unit_index))
	{
		/* in a vehicle (or carried): the board waits */
		skate_globals.reenter = TRUE;
		skate_platform_skating = FALSE;
		return FALSE;
	}
	return TRUE;
}

/* biped_update, every tick, for every biped standing on its own: decides whether this one
rides, and returns TRUE when the board moved it this tick (Halo's walking is skipped). */
boolean skate_halo_update_biped(long biped_index, struct unit_animation_update_data *animation)
{
	struct biped_datum *biped = biped_get(biped_index);
	short gamepad_index = NONE;
	unsigned keys;
	boolean toggle = FALSE;

	if (!skate_globals.initialized)
		skate_halo_initialize();
	if (biped->unit.player_index == NONE)
		return FALSE;
	if (skate_globals.fast_cinematics)
		game_time_set_speed(cinematic_in_progress() ? 8.f : 1.f);
	{
		struct player_datum *player = player_get(biped->unit.player_index);
		gamepad_index = player->local_player_index;
		if (gamepad_index != 0)
			return FALSE; /* single player: the first local player rides */
	}

	if (skate_globals.skating && skate_globals.biped_index != biped_index)
		skate_halo_dismount("new body");
	if (TEST_FLAG(biped->object.damage_flags, _object_dead_bit))
	{
		if (skate_globals.skating)
			skate_halo_dismount("dead");
		return FALSE;
	}

	keys = skate_platform_keys;
	if ((keys & SKATE_KEY_BOARD) && !(skate_globals.previous_keys & SKATE_KEY_BOARD))
		toggle = TRUE;
	skate_globals.previous_keys = keys;
	if (input_has_gamepad(gamepad_index))
	{
		struct gamepad_state const *pad = input_get_gamepad_state(gamepad_index);
		/* the d-pad without the arrow keys: a real gamepad's d-pad down */
		short down = pad->buttons[_gamepad_binary_button_dpad_down] && !(keys & SKATE_KEY_DOWN);
		if (down && !skate_globals.previous_dpad_down)
			toggle = TRUE;
		skate_globals.previous_dpad_down = down;
	}
	if (skate_globals.auto_mount && !skate_globals.skating && skate_globals.ticks > 60 && !skate_halo_paused(biped_index))
	{
		skate_globals.auto_mount = FALSE;
		toggle = TRUE;
	}
	++skate_globals.ticks;

	if (toggle)
	{
		if (skate_globals.skating)
			skate_halo_dismount("button");
		else
			skate_halo_mount(biped_index);
	}
	if (!skate_globals.skating)
		return FALSE;
	if (skate_halo_paused(biped_index))
	{
		/* Halo moves the body; ride on from wherever it ends up */
		skate_globals.reenter = TRUE;
		skate_platform_skating = FALSE;
		skate_globals.have_pose = FALSE;
		return FALSE;
	}
	if (skate_globals.rail_test && skate_globals.ticks > 90)
	{
		float here[3];
		float point[3];
		float tangent[3];
		int rail;
		skate_globals.rail_test = FALSE;
		halo_to_skate(&biped->object.position, here);
		rail = skate_world_nearest_rail(skate_globals.world, here, 60.f, point, tangent);
		if (rail >= 0)
		{
			/* the middle of the rail's longest segment */
			int count;
			int i;
			float best = 0.f;
			float const *p = skate_world_rail_points(skate_globals.world, rail, &count);
			for (i = 0; i + 1 < count; ++i)
			{
				float dx = p[i * 3 + 3] - p[i * 3], dy = p[i * 3 + 4] - p[i * 3 + 1], dz = p[i * 3 + 5] - p[i * 3 + 2];
				float l = sqrtf(dx * dx + dy * dy + dz * dz);
				if (l > best)
				{
					best = l;
					point[0] = p[i * 3] + 0.5f * dx;
					point[1] = p[i * 3 + 1] + 0.5f * dy;
					point[2] = p[i * 3 + 2] + 0.5f * dz;
					tangent[0] = dx / l;
					tangent[1] = dy / l;
					tangent[2] = dz / l;
				}
			}
		}
		if (rail >= 0)
		{
			/* drop onto it from 0.6 m above, rolling along it */
			float start[3];
			float velocity[3];
			start[0] = point[0];
			start[1] = point[1] + 0.6f;
			start[2] = point[2];
			velocity[0] = tangent[0] * 4.f;
			velocity[1] = 0.f;
			velocity[2] = tangent[2] * 4.f;
			skate_skater_enter(skate_globals.skater, start, velocity, (float)atan2(tangent[0], tangent[2]));
			platform_log("skate: rail test: rail %d at %.1f %.1f %.1f", rail, point[0], point[1], point[2]);
		}
	}

	{
		skate_input in;
		skate_pose pose;
		skate_event event;
		real_point3d position;
		real_vector3d velocity;
		short step;

		skate_halo_derive_rails();
		if (skate_globals.reenter)
		{
			float position[3];
			float velocity[3];
			real_vector3d forward;
			skate_globals.reenter = FALSE;
			unit_get_facing_vector(biped_index, &forward);
			halo_to_skate(&biped->object.position, position);
			position[1] += 0.05f;
			halo_to_skate((real_point3d *)&biped->object.translational_velocity, velocity);
			velocity[0] *= HALO_TICKS_PER_SECOND;
			velocity[1] *= HALO_TICKS_PER_SECOND;
			velocity[2] *= HALO_TICKS_PER_SECOND;
			skate_skater_enter(skate_globals.skater, position, velocity, (float)atan2(forward.j, forward.i));
		}
		skate_platform_skating = TRUE;
		skate_halo_read_input(gamepad_index, &in);
		/* two 60 Hz steps per Halo tick; the first sees the midpoint of the stick's travel so a
		quick flick still leaves the recognizer two samples */
		for (step = 0; step < 2; ++step)
		{
			skate_input sample = in;
			if (step == 0)
			{
				sample.right_stick[0] = 0.5f * (in.right_stick[0] + skate_globals.stick_previous[0]);
				sample.right_stick[1] = 0.5f * (in.right_stick[1] + skate_globals.stick_previous[1]);
			}
			skate_skater_step(skate_globals.skater, skate_globals.world, &sample);
		}
		skate_globals.stick_previous[0] = in.right_stick[0];
		skate_globals.stick_previous[1] = in.right_stick[1];

		while (skate_skater_poll_event(skate_globals.skater, &event))
		{
			switch (event.type)
			{
			case SKATE_EVENT_TRICK: platform_log("skate: trick %s %u", event.name, event.points); break;
			case SKATE_EVENT_LANDED: platform_log("skate: landed %s", event.value ? "clean" : "sketchy"); break;
			case SKATE_EVENT_BAIL: platform_log("skate: bail (%s)", event.name); break;
			case SKATE_EVENT_GRIND: platform_log("skate: grind %s", event.name); break;
			case SKATE_EVENT_BANKED: platform_log("skate: banked %u", event.points); break;
			case SKATE_EVENT_POP: platform_log("skate: pop %s (strength %d)", event.name, event.value); break;
			}
			skate_halo_hud_event(&event);
		}

		skate_skater_get_pose(skate_globals.skater, &pose);
		skate_to_halo(pose.position, &position);
		position.z += DECK_HEIGHT_METRES / METRES_PER_WORLD_UNIT + skate_globals.feet_height;
		skate_to_halo(pose.velocity, (real_point3d *)&velocity);
		velocity.i /= HALO_TICKS_PER_SECOND;
		velocity.j /= HALO_TICKS_PER_SECOND;
		velocity.k /= HALO_TICKS_PER_SECOND;

		{
			/* the body faces along the board */
			real_vector3d forward;
			real_vector3d up;
			forward.i = (real)cos(pose.heading);
			forward.j = (real)sin(pose.heading);
			forward.k = 0.f;
			up.i = 0.f;
			up.j = 0.f;
			up.k = 1.f;
			object_set_position(biped_index, &position, &forward, &up);
			biped->unit.desired_facing_vector = forward;
		}
		biped->object.translational_velocity = velocity;
		biped->biped.pathfinding_point = position;
		SET_FLAG(biped->biped.flags, _biped_airborne_bit, FALSE);
		biped->biped.landing = NONE;

		animation->crouching = pose.crouch > 0.45f || pose.state == SKATE_STATE_BAIL;
		animation->state_desired = _unit_state_idle;

		skate_halo_board_pose(&pose);
		if ((skate_globals.ticks % 3) == 0)
			skate_halo_hud_status(&pose);
		if ((skate_globals.ticks % 60) == 0)
		{
			platform_log("skate: state %d at %.2f %.2f %.2f speed %.1f m/s combo %u x%u banked %llu",
				pose.state, position.x, position.y, position.z,
				sqrtf(pose.velocity[0] * pose.velocity[0] + pose.velocity[2] * pose.velocity[2]),
				pose.combo_value, pose.combo_multiplier, pose.banked);
		}
	}
	return TRUE;
}

void skate_halo_unit_killed_enemy(boolean headshot)
{
	if (!skate_globals.skating || !skate_globals.skater)
		return;
	{
		skate_pose pose;
		char const *trick = skate_skater_current_trick(skate_globals.skater);

		skate_skater_get_pose(skate_globals.skater, &pose);
		if (trick && trick[0])
		{
			char name[64];
			snprintf(name, sizeof(name), "%s Kill", trick);
			skate_skater_add_bonus(skate_globals.skater, name, headshot ? 400 : 250);
		}
		else if (pose.state == SKATE_STATE_AIR)
			skate_skater_add_bonus(skate_globals.skater, headshot ? "Airborne Headshot" : "Airborne Kill", headshot ? 350 : 250);
		else
			skate_skater_add_bonus(skate_globals.skater, headshot ? "Rolling Headshot" : "Rolling Kill", headshot ? 200 : 100);
	}
}

/* ---------- the board's render

Drawn with the rasterizer's debug triangles (queued in world space, depth-tested, drawn by
rasterizer_debug_draw after the scene). The board hangs under the biped's rendered position,
so it moves with the interpolated body. Board space: metres, +X left, +Y up, +Z nose. */

void rasterizer_debug_triangle_shaded(real_point3d const *p0, real_point3d const *p1, real_point3d const *p2,
	real_argb_color const *color0, real_argb_color const *color1, real_argb_color const *color2);

static struct
{
	float q[4];
	real_point3d origin;
} board_frame;

static void board_point(float x, float y, float z, real_point3d *out)
{
	/* rotate (x, y, z) by the board quaternion, then skate -> halo */
	float const *q = board_frame.q;
	float tx = 2.f * (q[1] * z - q[2] * y);
	float ty = 2.f * (q[2] * x - q[0] * z);
	float tz = 2.f * (q[0] * y - q[1] * x);
	float r[3];
	r[0] = x + q[3] * tx + (q[1] * tz - q[2] * ty);
	r[1] = y + q[3] * ty + (q[2] * tx - q[0] * tz);
	r[2] = z + q[3] * tz + (q[0] * ty - q[1] * tx);
	out->x = board_frame.origin.x + r[2] / METRES_PER_WORLD_UNIT;
	out->y = board_frame.origin.y + r[0] / METRES_PER_WORLD_UNIT;
	out->z = board_frame.origin.z + r[1] / METRES_PER_WORLD_UNIT;
}

static void board_quad(float const a[3], float const b[3], float const c[3], float const d[3],
	real_argb_color const *color)
{
	real_point3d p[4];
	board_point(a[0], a[1], a[2], &p[0]);
	board_point(b[0], b[1], b[2], &p[1]);
	board_point(c[0], c[1], c[2], &p[2]);
	board_point(d[0], d[1], d[2], &p[3]);
	/* both windings: the debug pass's culling is not ours to rely on */
	rasterizer_debug_triangle_shaded(&p[0], &p[1], &p[2], color, color, color);
	rasterizer_debug_triangle_shaded(&p[0], &p[2], &p[3], color, color, color);
	rasterizer_debug_triangle_shaded(&p[0], &p[2], &p[1], color, color, color);
	rasterizer_debug_triangle_shaded(&p[0], &p[3], &p[2], color, color, color);
}

static void board_shade(real_argb_color const *base, float light, real_argb_color *out)
{
	out->alpha = 1.f;
	out->red = base->red * light;
	out->green = base->green * light;
	out->blue = base->blue * light;
}

/* a box between two corners, lit from above */
static void board_box(float x0, float y0, float z0, float x1, float y1, float z1, real_argb_color const *color)
{
	float v[8][3];
	real_argb_color top, side, bottom, end;
	short i;
	for (i = 0; i < 8; ++i)
	{
		v[i][0] = (i & 1) ? x1 : x0;
		v[i][1] = (i & 2) ? y1 : y0;
		v[i][2] = (i & 4) ? z1 : z0;
	}
	board_shade(color, 1.f, &top);
	board_shade(color, 0.7f, &side);
	board_shade(color, 0.45f, &bottom);
	board_shade(color, 0.6f, &end);
	board_quad(v[2], v[3], v[7], v[6], &top);
	board_quad(v[0], v[4], v[5], v[1], &bottom);
	board_quad(v[0], v[2], v[6], v[4], &side);
	board_quad(v[1], v[5], v[7], v[3], &side);
	board_quad(v[4], v[6], v[7], v[5], &end);
	board_quad(v[0], v[1], v[3], v[2], &end);
}

/* the grindable edges near the rider, as faint strips just above them */
static void skate_halo_render_rails(void)
{
	static real_argb_color const glow = {0.45f, 0.35f, 0.85f, 1.f};
	int rail_count = skate_world_rail_count(skate_globals.world);
	int rail;
	float const reach = 14.f;
	float const *here = skate_globals.pose.position;

	for (rail = 0; rail < rail_count; ++rail)
	{
		int count;
		int i;
		float const *p = skate_world_rail_points(skate_globals.world, rail, &count);
		for (i = 0; i + 1 < count; ++i)
		{
			float const *a = &p[i * 3];
			float const *b = &p[(i + 1) * 3];
			float dx = b[0] - a[0], dz = b[2] - a[2];
			float length = sqrtf(dx * dx + dz * dz);
			float sx, sz;
			float mid[3];
			float c[4][3];
			real_point3d h[4];
			short k;
			mid[0] = 0.5f * (a[0] + b[0]) - here[0];
			mid[1] = 0.5f * (a[1] + b[1]) - here[1];
			mid[2] = 0.5f * (a[2] + b[2]) - here[2];
			if (mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2] > (reach + length) * (reach + length) || length < 1e-3f)
				continue;
			sx = -dz / length * 0.025f;
			sz = dx / length * 0.025f;
			c[0][0] = a[0] - sx; c[0][1] = a[1] + 0.01f; c[0][2] = a[2] - sz;
			c[1][0] = a[0] + sx; c[1][1] = a[1] + 0.01f; c[1][2] = a[2] + sz;
			c[2][0] = b[0] + sx; c[2][1] = b[1] + 0.01f; c[2][2] = b[2] + sz;
			c[3][0] = b[0] - sx; c[3][1] = b[1] + 0.01f; c[3][2] = b[2] - sz;
			for (k = 0; k < 4; ++k)
				skate_to_halo(c[k], &h[k]);
			rasterizer_debug_triangle_shaded(&h[0], &h[1], &h[2], &glow, &glow, &glow);
			rasterizer_debug_triangle_shaded(&h[0], &h[2], &h[3], &glow, &glow, &glow);
			rasterizer_debug_triangle_shaded(&h[0], &h[2], &h[1], &glow, &glow, &glow);
			rasterizer_debug_triangle_shaded(&h[0], &h[3], &h[2], &glow, &glow, &glow);
		}
	}
}

void skate_halo_render(void)
{
	static real_argb_color const grip = {1.f, 0.10f, 0.10f, 0.11f};
	static real_argb_color const ply = {1.f, 0.36f, 0.48f, 0.20f};   /* Spartan green */
	static real_argb_color const metal = {1.f, 0.62f, 0.64f, 0.66f};
	static real_argb_color const urethane = {1.f, 0.92f, 0.86f, 0.70f};
	struct object_datum *object;
	float const deck_y = 0.095f, deck_t = 0.014f, half_w = 0.105f, flat = 0.27f, tip = 0.40f, kick = 0.055f;

	if (!skate_globals.skating || !skate_globals.have_pose || skate_globals.biped_index == NONE)
		return;
	object = object_get(skate_globals.biped_index);
	memcpy(board_frame.q, skate_globals.pose.board_rotation, sizeof(board_frame.q));
	board_frame.origin = object->object.position;
	board_frame.origin.z -= DECK_HEIGHT_METRES / METRES_PER_WORLD_UNIT + skate_globals.feet_height;

	skate_halo_render_rails();

	/* deck: flat middle, nose and tail kicked up */
	board_box(-half_w, deck_y, -flat, half_w, deck_y + deck_t, flat, &ply);
	{
		float a[3], b[3], c[3], d[3];
		real_argb_color top;
		board_shade(&grip, 1.f, &top);
		/* grip tape on top of the middle */
		a[0] = -half_w; a[1] = deck_y + deck_t + 0.001f; a[2] = -flat;
		b[0] = half_w; b[1] = a[1]; b[2] = -flat;
		c[0] = half_w; c[1] = a[1]; c[2] = flat;
		d[0] = -half_w; d[1] = a[1]; d[2] = flat;
		board_quad(a, b, c, d, &top);
		/* nose and tail ramps, grip on top, ply under */
		{
			short end;
			for (end = -1; end <= 1; end += 2)
			{
				float z0 = flat * end, z1 = tip * end;
				real_argb_color under;
				board_shade(&ply, 0.55f, &under);
				a[0] = -half_w * 0.95f; a[1] = deck_y + deck_t + 0.001f; a[2] = z0;
				b[0] = half_w * 0.95f; b[1] = a[1]; b[2] = z0;
				c[0] = half_w * 0.8f; c[1] = deck_y + deck_t + kick; c[2] = z1;
				d[0] = -half_w * 0.8f; d[1] = c[1]; d[2] = z1;
				board_quad(a, b, c, d, &top);
				a[1] = b[1] = deck_y;
				c[1] = d[1] = deck_y + kick;
				board_quad(a, b, c, d, &under);
			}
		}
	}
	/* trucks and wheels */
	{
		short end;
		for (end = -1; end <= 1; end += 2)
		{
			float z = 0.21f * end;
			board_box(-0.075f, 0.045f, z - 0.018f, 0.075f, deck_y, z + 0.018f, &metal);
			board_box(-0.11f, 0.0f, z - 0.027f, -0.075f, 0.054f, z + 0.027f, &urethane);
			board_box(0.075f, 0.0f, z - 0.027f, 0.11f, 0.054f, z + 0.027f, &urethane);
		}
	}
}

static void skate_halo_board_pose(skate_pose const *pose)
{
	skate_globals.pose = *pose;
	skate_globals.have_pose = TRUE;
}

/* ---------- the page's HUD (port/web/site/skate-hud.js) */

static void skate_halo_hud_post(char const *json)
{
#ifdef HALO_WEB
	web_note_skate(json);
#else
	(void)json;
#endif
}

static void skate_halo_hud_event(skate_event const *event)
{
	char json[128];

	switch (event->type)
	{
	case SKATE_EVENT_TRICK: snprintf(json, sizeof(json), "{\"e\":\"trick\",\"n\":\"%s\",\"p\":%u}", event->name, event->points); break;
	case SKATE_EVENT_LANDED: snprintf(json, sizeof(json), "{\"e\":\"land\",\"c\":%d}", event->value); break;
	case SKATE_EVENT_BAIL: snprintf(json, sizeof(json), "{\"e\":\"bail\",\"n\":\"%s\"}", event->name); break;
	case SKATE_EVENT_GRIND: snprintf(json, sizeof(json), "{\"e\":\"grind\",\"n\":\"%s\"}", event->name); break;
	case SKATE_EVENT_BANKED: snprintf(json, sizeof(json), "{\"e\":\"bank\",\"p\":%u}", event->points); break;
	default: return;
	}
	skate_halo_hud_post(json);
}

static void skate_halo_hud_status(skate_pose const *pose)
{
	char json[192];
	char const *trick = skate_skater_current_trick(skate_globals.skater);
	float speed = sqrtf(pose->velocity[0] * pose->velocity[0] + pose->velocity[2] * pose->velocity[2]);

	snprintf(json, sizeof(json), "{\"e\":\"s\",\"st\":%d,\"k\":\"%s\",\"c\":%u,\"m\":%u,\"b\":%llu,\"v\":%.1f}",
		pose->state, trick ? trick : "", pose->combo_value, pose->combo_multiplier, pose->banked, speed);
	skate_halo_hud_post(json);
}
