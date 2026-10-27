/* test_loader.c
Copyright (c) 2026 by Anaxarchus

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

/* A minimal mod loader for test_host.cpp, in plain C. Each build of it is one
 * variant, chosen with these macros:
 *   TL_NAME      the loader's name ("test loader")
 *   TL_MOD       the one mod it runs ("test_mod")
 *   TL_VERSION   the version it reports (ES_API_VERSION)
 *   TL_SIZE      the size it reports (sizeof(EsLoader))
 *   TL_RESULT    what es_loader_init answers (0)
 *   TL_NO_INIT   defined: no es_loader_init at all
 *   TL_MISBEHAVE defined: misuse the brackets, as a loader whose mod failed
 *                might. es_loader_init brackets a mod and calls for it; step
 *                leaves its bracket open (the mod "threw"); enter first makes
 *                an unbracketed call, then ends a bracket it never began.
 * Every hook brackets the mod with hook_timing and makes one mod-scoped call
 * (planet_button, with the hook's name as the label), so the host can check
 * which mod it was for. */
#include "../../source/modding/es_api.h"

#include <string.h>

#ifndef TL_NAME
#define TL_NAME "test loader"
#endif
#ifndef TL_MOD
#define TL_MOD "test_mod"
#endif
#ifndef TL_VERSION
#define TL_VERSION ES_API_VERSION
#endif
#ifndef TL_SIZE
#define TL_SIZE sizeof(EsLoader)
#endif
#ifndef TL_RESULT
#define TL_RESULT 0
#endif

/* What the host test reads back through tl_stats. */
typedef struct TlStats {
	int init_ok;
	int load_calls;
	int sources;
	char last_source_name[64];
	int steps;
	int days;
	int enters;
	int planet_buttons;
	int panels;
	char last_target[64];
} TlStats;

static TlStats stats;

ES_LOADER_EXPORT TlStats *ES_CALL tl_stats(void)
{
	return &stats;
}

#ifndef TL_NO_INIT
static const EsHost *game;

static void hook(const char *name)
{
	game->hook_timing(TL_MOD, name, 1);
	game->planet_button(name);
	game->hook_timing(TL_MOD, name, 0);
}

static void ES_CALL load_mods(int32_t count, const char *const *paths, const char *const *names)
{
	++stats.load_calls;
	stats.sources = count;
	if(count > 0 && paths[count - 1] && names[count - 1])
	{
		strncpy(stats.last_source_name, names[count - 1], sizeof(stats.last_source_name) - 1);
		hook("on_load");
	}
}

static int32_t ES_CALL count(void)
{
	return stats.load_calls ? 1 : 0;
}

static void ES_CALL step(int32_t step)
{
	(void)step;
	++stats.steps;
#ifdef TL_MISBEHAVE
	game->hook_timing(TL_MOD, "on_step", 1);
	game->planet_button("leak");
	/* The mod failed here: no end bracket. */
#else
	hook("on_step");
#endif
}

static void ES_CALL day(int32_t day)
{
	(void)day;
	++stats.days;
	/* Both loaders run a mod of this name: the game must refuse the second. */
	game->hook_timing("shared", "on_day", 1);
	game->planet_button("on_day");
	game->hook_timing("shared", "on_day", 0);
}

static void ES_CALL enter(const char *system)
{
	(void)system;
	++stats.enters;
#ifdef TL_MISBEHAVE
	game->planet_button("stray");
	game->hook_timing(TL_MOD, "on_enter", 0);
#endif
	hook("on_enter");
}

static void ES_CALL planet_button(const char *mod)
{
	++stats.planet_buttons;
	strncpy(stats.last_target, mod, sizeof(stats.last_target) - 1);
}

static void ES_CALL panel(const char *mod, int32_t button)
{
	(void)button;
	++stats.panels;
	strncpy(stats.last_target, mod, sizeof(stats.last_target) - 1);
}

static void ES_CALL shutdown(void)
{
	game->log("shutdown " TL_NAME);
}

ES_LOADER_EXPORT int32_t ES_CALL es_loader_init(const EsHost *host, EsLoader *loader)
{
	/* A valid table: the game's version, every API 9 call, a zeroed loader table. */
	if(!host || host->version != ES_API_VERSION || !ES_HOST_HAS(host, ship_in_stock)
			|| !host->log || !host->hook_timing || !host->planet_button)
		return 10;
	if(!loader || loader->size < sizeof(EsLoader) || loader->load_mods || loader->shutdown)
		return 11;
	game = host;
	stats.init_ok = 1;
	host->log("init " TL_NAME);
#ifdef TL_MISBEHAVE
	host->hook_timing("init_mod", "init", 1);
	host->planet_button("init");
	host->hook_timing("init_mod", "init", 0);
#endif

	loader->size = (uint32_t)(TL_SIZE);
	loader->version = TL_VERSION;
	loader->name = TL_NAME;
	loader->load_mods = load_mods;
	loader->count = count;
	loader->step = step;
	loader->day = day;
	loader->enter = enter;
	loader->planet_button = planet_button;
	loader->panel = panel;
	loader->shutdown = shutdown;
	return TL_RESULT;
}
#endif
