/*
SKATE_HALO.H

HaloSkate3: the player's biped on a skateboard (skate_halo.c).
*/

#ifndef __SKATE_HALO_H
#define __SKATE_HALO_H
#pragma once

/* keys the platform layer reports beside the emulated gamepad (port/linux/src/xinput_sdl.c) */
enum
{
	SKATE_KEY_BOARD = 1 << 0, /* B */
	SKATE_KEY_SHIFT = 1 << 1,
	SKATE_KEY_SPACE = 1 << 2,
	SKATE_KEY_UP = 1 << 4,
	SKATE_KEY_DOWN = 1 << 5,
	SKATE_KEY_LEFT = 1 << 6,
	SKATE_KEY_RIGHT = 1 << 7,
	SKATE_KEY_W = 1 << 8,
	SKATE_KEY_A = 1 << 9,
	SKATE_KEY_S = 1 << 10,
	SKATE_KEY_D = 1 << 11,
	SKATE_KEY_Q = 1 << 12,
	SKATE_KEY_X = 1 << 13,
	SKATE_KEY_C = 1 << 14,
	SKATE_KEY_E = 1 << 15,
};

struct unit_animation_update_data;

void skate_halo_initialize(void);
boolean skate_halo_unit_is_skating(long unit_index);
boolean skate_halo_update_biped(long biped_index, struct unit_animation_update_data *animation);
void skate_halo_unit_killed_enemy(boolean headshot);
void skate_halo_render(void);

#endif
