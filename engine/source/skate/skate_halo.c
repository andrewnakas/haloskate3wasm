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
	Q, X / white, black         grabs         E / X                 powerslide (with a carve)
*/

#include "cseries.h"
#include "game/players.h"
#include "input/input.h"
#include "objects/objects.h"
#include "physics/collisions.h"
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
extern unsigned skate_platform_keys;

/* ---------- constants */

#define METRES_PER_WORLD_UNIT 3.048f
#define HALO_TICKS_PER_SECOND 30.f
#define DECK_HEIGHT_METRES 0.11f

/* ---------- prototypes */

static void skate_halo_board_pose(skate_pose const *pose);
static void skate_halo_hud_event(skate_event const *event);

/* ---------- globals */

static struct
{
	boolean initialized;
	boolean skating;
	boolean auto_mount;
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
	skate_pose pose;  /* the latest, for the board's render */
	boolean have_pose;
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
	long ignore = (long)(intptr_t)user;
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
		rx = stick_value(pad->sticks[_gamepad_stick_right].x);
		ry = stick_value(pad->sticks[_gamepad_stick_right].y);
		if (pad->analog_buttons[_gamepad_analog_button_a] > 64 || in->left_stick[1] > 0.5f)
			in->buttons |= SKATE_BUTTON_PUSH;
		if (in->left_stick[1] < -0.5f)
			in->buttons |= SKATE_BUTTON_BRAKE;
		if (pad->analog_buttons[_gamepad_analog_button_white] > 64)
			in->buttons |= SKATE_BUTTON_GRAB_LEFT;
		if (pad->analog_buttons[_gamepad_analog_button_black] > 64)
			in->buttons |= SKATE_BUTTON_GRAB_RIGHT;
		if (pad->analog_buttons[_gamepad_analog_button_x] > 64)
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
		in->buttons |= SKATE_BUTTON_GRAB_RIGHT;
	if (keys & SKATE_KEY_E)
		in->buttons |= SKATE_BUTTON_POWERSLIDE;

	in->right_stick[0] = rx;
	in->right_stick[1] = ry;
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
	/* a world per mount: its raycasts ignore this biped */
	if (skate_globals.world)
		skate_world_delete(skate_globals.world);
	skate_globals.world = skate_world_new(skate_halo_raycast, (void *)(intptr_t)biped_index);

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
	platform_log("skate: off the board (%s)", why);
}

/* ---------- public code */

void skate_halo_initialize(void)
{
	char const *automatic = getenv("HALO_SKATE_AUTO");

	memset(&skate_globals, 0, sizeof(skate_globals));
	skate_globals.biped_index = NONE;
	skate_globals.auto_mount = automatic && automatic[0] == '1';
	skate_globals.initialized = TRUE;
	platform_log("skate: ready (HaloSkate3, auto mount %s)", skate_globals.auto_mount ? "on" : "off");
}

boolean skate_halo_unit_is_skating(long unit_index)
{
	return skate_globals.skating && unit_index != NONE && unit_index == skate_globals.biped_index;
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
	if (skate_globals.auto_mount && !skate_globals.skating && skate_globals.ticks > 60)
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

	{
		skate_input in;
		skate_pose pose;
		skate_event event;
		real_point3d position;
		real_vector3d velocity;
		short step;

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

static void skate_halo_hud_event(skate_event const *event)
{
	(void)event;
}
