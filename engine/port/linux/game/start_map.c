/* game.start_map and game.test_script (web): a campaign level in place of the
main menu, under a player profile so checkpoints and level progress are saved,
and script commands at set times for the automated checks. */
#ifdef HALO_WEB

#include "cseries.h"
#include "main/main.h"
#include "interface/player_ui.h"
#include "game/game.h"
#include "hs/hs.h"
#include "saved games/player_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *config_string(const char *name);
void web_js_post(int kind, const char *text);
boolean game_time_get_paused(void);
real game_time_get_speed(void);
real main_get_seconds_elapsed(void);

enum
{
	MAXIMUM_START_PROFILES = 64,
	MAXIMUM_TEST_COMMANDS = 16,
};

/* the first profile of this browser, or a new one named Player */
static boolean start_map_choose_profile(void)
{
	word count = MAXIMUM_START_PROFILES;      /* in: room in the list; out: profiles found */
	long indices[MAXIMUM_START_PROFILES];
	long profile_index = NONE;
	struct player_profile profile;
	static wchar_t name[] = L"Player";

	player_ui_set_single_player_local_player_controller(0, 0);
	player_profiles_enumerate_available_to_local_player_index(0, &count, indices, FALSE);
	if (count > 0)
	{
		profile_index = indices[0];
	}
	else
	{
		profile_index = player_profile_new(0, name);
	}
	if (profile_index == NONE || !player_profile_get(profile_index, &profile))
	{
		return FALSE;
	}
	player_ui_set_active_player_profile(0, profile_index, &profile);

	return TRUE;
}

void start_map_begin(const char *level)
{
	static char scenario[128];

	if (!start_map_choose_profile())
	{
		web_js_post(0, "start_map: no player profile; progress is not saved");
	}
	snprintf(scenario, sizeof(scenario), "levels\\%s\\%s", level, level);
	main_set_difficulty(1);
	main_set_map_name(scenario);
	game_connection_set(0);
	main_menu_switch_to_single_player();
}

/* game.test_script "5:cheat_deathless_player 1;60:game_won": each command runs
once, that many seconds of game time into the first level played */
void start_map_update(boolean main_menu_loaded)
{
	static boolean parsed;
	static char text[512];
	static struct { long tick; char *command; boolean done; } commands[MAXIMUM_TEST_COMMANDS];
	static short command_count;
	short index;

	if (!parsed)
	{
		char *cursor;

		parsed = TRUE;
		strncpy(text, config_string("game.test_script"), sizeof(text) - 1);
		for (cursor = strtok(text, ";"); cursor && command_count < MAXIMUM_TEST_COMMANDS; cursor = strtok(NULL, ";"))
		{
			char *colon = strchr(cursor, ':');

			if (colon)
			{
				*colon = 0;
				commands[command_count].tick = (long)(atof(cursor) * 30.0);
				commands[command_count].command = colon + 1;
				command_count++;
			}
		}
	}
	if (!command_count || main_menu_loaded || !game_in_progress())
	{
		return;
	}
	{
		/* the game clock against the wall clock, every 5 seconds, for the checks' logs */
		static unsigned long reported;
		static long frames;
		static double seconds;
		unsigned long now = system_milliseconds();

		frames++;
		seconds += (double)main_get_seconds_elapsed();
		if (now - reported >= 5000)
		{
			char note[160];

			reported = now;
			snprintf(note, sizeof(note), "game time %ld ticks, paused %d, speed %.2f; %ld frames, %.2f s counted",
				game_time_get(), (int)game_time_get_paused(), (double)game_time_get_speed(), frames, seconds);
			web_js_post(0, note);
			frames = 0;
			seconds = 0.0;
		}
	}
	for (index = 0; index < command_count; index++)
	{
		if (!commands[index].done && game_time_get() >= commands[index].tick)
		{
			char note[256];

			commands[index].done = TRUE;
			snprintf(note, sizeof(note), "test_script at tick %ld: %s", game_time_get(), commands[index].command);
			web_js_post(0, note);
			hs_compile_and_evaluate(commands[index].command);
			web_js_post(0, "test_script: done");
		}
	}
}

#endif
