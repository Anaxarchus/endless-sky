/* api11_loader.c
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

/* A mod loader that tests mod API 11 inside the real game: run.sh builds it,
 * puts it in a scratch config folder with the "API 11" integration test
 * (api11_tests.txt), and runs the game on that test. It runs one mod,
 * "api11_test", whose hooks are this file's checks. Each check that passes adds
 * 1 to the condition "api11 pass"; each that fails adds 1 to "api11 fail" and
 * logs "API11 FAIL: <what>". The test asserts on those conditions and on the
 * ones the checks set when a stage is done:
 *   pilot_loaded  definitions (define, variants, defined), and moving outfits
 *                 in and out of cargo and storage while landed ("api11 loads");
 *   planet_button ship_replace_outfit, which needs a hook the player started
 *                 ("api11 replaced");
 *   step          in flight: moving outfits, the ship queries, and waking a
 *                 placed derelict up, which must then move and stay awake
 *                 ("api11 woke"). */
#include "../../../source/modding/es_api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOD "api11_test"

static const EsHost *game;

static void Check(int ok, const char *what)
{
	char line[256];
	if(ok)
		game->condition_set("api11 pass", game->condition_get("api11 pass") + 1);
	else
	{
		game->condition_set("api11 fail", game->condition_get("api11 fail") + 1);
		snprintf(line, sizeof(line), "API11 FAIL: %s", what);
		game->log(line);
	}
}

static void Begin(const char *hook)
{
	game->hook_timing(MOD, hook, 1);
}

static void End(const char *hook)
{
	game->hook_timing(MOD, hook, 0);
}

/* The definition the hand-made save holds, exactly (as data-file text). */
static const char *CORE =
	"outfit \"API11 Core\"\n"
	"\tcategory \"Systems\"\n"
	"\tcost 1000\n"
	"\tmass 10\n"
	"\t\"outfit space\" -10\n"
	"\t\"shield generation\" 2\n";

static int loads = 0;

static void ES_CALL load_mods(int32_t count, const char *const *paths, const char *const *names)
{
	(void)count; (void)paths; (void)names;
	Begin("on_load");
	game->planet_button("API 11");
	End("on_load");
}

static int32_t ES_CALL mod_count(void)
{
	return 1;
}

static void ES_CALL pilot_loaded(void)
{
	Begin("on_pilot_loaded");
	game->planet_button("API 11");
	/* The game may start the mods with a pilot of its own (none loaded yet):
	 * test only the test's pilot. */
	if(game->fleet_count() == 0 || strcmp(game->planet(), "Ruin"))
	{
		End("on_pilot_loaded");
		return;
	}
	++loads;

	/* What the save defined, before any call. */
	Check(game->defined("outfit", "API11 Core") == 2, "save's outfit defined");
	Check(game->defined("outfit", "API11 Battery") == 2, "save's outfit variant defined");
	Check(game->defined("ship", "API11 Hull") == 2, "save's ship defined");
	Check(game->defined("ship", "API11 Hull Mk2") == 2, "save's ship variant defined");
	Check(game->defined("outfit", "Hyperdrive") == 1, "game data outfit");
	Check(game->defined("ship", "Star Barge") == 1, "game data ship");
	Check(game->defined("outfit", "API11 No Such Thing") == 0, "unknown outfit");
	Check(game->defined("planet", "Earth") == 0, "unknown kind");
	Check(game->outfit_attribute("API11 Battery", "energy capacity") == 12345., "variant attribute from the save");
	Check(game->outfit_attribute("API11 Battery", "mass") > 0., "variant keeps the base's mass");
	Check(!strcmp(game->model_name("API11 Hull Mk2"), "API11 Hull Mk2"), "ship variant's display name");
	Check(!strcmp(game->model_category("API11 Hull Mk2"), "Light Freighter"), "ship variant inherits");
	/* What this mod defines below was saved after the first load. */
	if(loads >= 2)
	{
		Check(game->defined("outfit", "API11 Core Plus") == 2, "round trip: outfit variant");
		Check(game->defined("outfit", "API11 Laser+") == 2, "round trip: weapon variant");
		Check(game->defined("ship", "API11 Barge X") == 2, "round trip: ship variant");
		Check(game->defined("outfit", "API11 Plate") == 2, "round trip: outfit");
		Check(game->defined("ship", "API11 Plated") == 2, "round trip: ship");
	}

	/* define: the same text again is a no-op that counts; other text, a data
	 * name or another node is refused. */
	Check(game->define(CORE) == 1, "same text is a no-op");
	Check(game->define("outfit \"API11 Core\"\n\t\"shield generation\" 3\n") == 0, "other text refused");
	Check(game->define("outfit \"Hyperdrive\"\n\tmass 1\n") == 0, "game data name refused");
	Check(game->define("fleet \"API11 Fleet\"\n") == 0, "other node refused");
	/* A ship first in the text may use an outfit defined after it. */
	Check(game->define(
		"ship \"API11 Plated\"\n"
		"\tsprite \"ship/star barge\"\n"
		"\tattributes\n"
		"\t\tcategory \"Light Freighter\"\n"
		"\t\tcost 1000\n"
		"\t\tmass 50\n"
		"\t\tdrag 2\n"
		"\t\thull 500\n"
		"\t\t\"outfit space\" 100\n"
		"\toutfits\n"
		"\t\t\"API11 Plate\"\n"
		"outfit \"API11 Plate\"\n"
		"\tcategory \"Systems\"\n"
		"\tmass 5\n"
		"\thull 100\n"
		"\t\"outfit space\" -5\n") == 2, "define an outfit and a ship using it");
	Check(game->define("ship \"API11 Bad\"\n\toutfits\n\t\t\"API11 No Such Thing\"\n") == 0,
		"a ship with an undefined outfit is refused");

	/* Variants. */
	Check(game->define_outfit_variant("API11 Core Plus", "API11 Core",
		"\"shield generation\" 5\n\"display name\" \"API11 Core+\"\n") == 1, "outfit variant of a runtime outfit");
	Check(game->outfit_attribute("API11 Core Plus", "shield generation") == 5., "variant's change");
	Check(game->outfit_attribute("API11 Core Plus", "mass") == 10., "variant keeps the rest");
	Check(!strcmp(game->outfit_name("API11 Core Plus"), "API11 Core+"), "variant's display name");
	Check(game->define_outfit_variant("API11 Laser+", "Heavy Laser",
		"\"display name\" \"Heavy Laser+\"\nweapon\n\t\"shield damage\" 100\n") == 1, "weapon variant");
	Check(game->outfit_is_weapon("API11 Laser+") == 1, "weapon variant is a weapon");
	Check(game->define_outfit_variant("API11 Nothing+", "API11 No Such Thing", "mass 1\n") == 0,
		"variant of an unknown outfit refused");
	Check(game->define_ship_variant("API11 Barge X", "Star Barge", "\"display name\" \"Barge X\"\n") == 1,
		"ship variant of a game ship");
	Check(!strcmp(game->model_name("API11 Barge X"), "Barge X"), "ship variant's display name");
	Check(game->model_cost("API11 Barge X") == game->model_cost("Star Barge"), "ship variant's cost");

	/* Moving outfits while landed. */
	Check(game->outfit_give("API11 Core", 2, "storage") == 2, "give to storage");
	Check(game->outfit_take("API11 Core", 5, "storage") == 2, "take from storage");
	Check(game->outfit_give("API11 Plate", 3, "cargo") == 3, "give to cargo");
	Check(game->outfit_take("API11 Plate", 1, "any") == 1, "take from any");
	Check(game->outfit_take("API11 Plate", 9, "cargo") == 2, "take what there is");
	Check(game->outfit_take("API11 Plate", 1, "elsewhere") == 0, "unknown place");
	Check(game->outfit_give("API11 No Such Thing", 1, "cargo") == 0, "unknown outfit");
	/* Only a hook the player started may replace outfits. */
	Check(game->ship_replace_outfit(game->fleet_ship(0), "API11 Core", "API11 Core Plus", 1) == 0,
		"replace refused outside a player's hook");

	game->condition_set("api11 loads", loads);
	End("on_pilot_loaded");
}

static void ES_CALL planet_button(const char *mod)
{
	(void)mod;
	char ship[64];
	Begin("on_planet_button");
	snprintf(ship, sizeof(ship), "%s", game->fleet_ship(0));
	Check(game->ship_outfit_count(ship, "API11 Core") == 1, "the save's outfit is installed");
	Check(game->ship_replace_outfit(ship, "API11 Core", "API11 Core Plus", 5) == 1, "replace one");
	Check(game->ship_outfit_count(ship, "API11 Core") == 0, "old one gone");
	Check(game->ship_outfit_count(ship, "API11 Core Plus") == 1, "new one installed");
	Check(game->ship_replace_outfit(ship, "API11 Core", "API11 Core Plus", 1) == 0, "nothing left to replace");
	Check(game->ship_replace_outfit(ship, "API11 Core Plus", "API11 Laser+", 1) == 0, "no gun port: refused");
	Check(game->ship_replace_outfit(ship, "API11 Battery", "API11 Plate", 1) == 1, "replace a variant");
	game->condition_set("api11 replaced", 1);
	End("on_planet_button");
}

/* The flight part: the wreck to wake up, found by its model. */
static int flightSteps = 0;
static int woke = 0;
static int wokeStep = 0;
static double wokeX = 0., wokeY = 0.;

/* A disabled wreck to wake up, or once one woke, that one: the only one that
 * is the Merchants' (a fleet's derelicts have no names). */
static int FindWreck(void)
{
	int32_t n = game->ship_count();
	for(int32_t i = 0; i < n; ++i)
		if(!strcmp(game->ship_model(i), "API11 Drifter")
				&& (woke ? !strcmp(game->ship_government(i), "Merchant") : game->ship_is_disabled(i)))
			return i;
	return -1;
}

static void ES_CALL step(int32_t s)
{
	(void)s;
	Begin("on_step");
	++flightSteps;
	int32_t flagship = game->flagship();
	if(flightSteps == 1 && flagship >= 0)
	{
		Check(game->ship_distance(flagship) == 0., "flagship's own distance");
		Check(game->ship_distance(-1) == -1., "distance of no ship");
		Check(game->ship_reactivate(flagship, "", "") == 0, "the player's ship is not reactivated");
		Check(game->outfit_give("API11 Plate", 2, "cargo") == 2, "give into the flagship's hold");
		Check(game->outfit_take("API11 Plate", 3, "cargo") == 2, "take from the ships' holds");
		Check(game->outfit_take("API11 Plate", 1, "storage") == 0, "no storage in flight");
	}
	int i = FindWreck();
	if(!woke && i >= 0 && flightSteps > 10)
	{
		double dx = game->ship_x(i) - game->ship_x(flagship);
		double dy = game->ship_y(i) - game->ship_y(flagship);
		Check(fabs(sqrt(dx * dx + dy * dy) - game->ship_distance(i)) < 1e-6, "distance matches positions");
		Check(game->ship_is_disabled(i) == 1, "the derelict is disabled");
		Check(game->ship_reactivate(i, "No Such Government", "") == 0, "unknown government refused");
		Check(game->ship_is_disabled(i) == 1, "a refused call changes nothing");
		Check(game->ship_reactivate(i, "Merchant", "heroic") == 1, "reactivate");
		Check(game->ship_is_disabled(i) == 0, "no longer disabled");
		Check(!strcmp(game->ship_government(i), "Merchant"), "government changed");
		Check(game->ship_reactivate(i, "", "") == 0, "an active ship is not reactivated");
		woke = 1;
		wokeStep = flightSteps;
		wokeX = game->ship_x(i);
		wokeY = game->ship_y(i);
	}
	else if(woke == 1 && flightSteps == wokeStep + 90)
	{
		Check(i >= 0, "the woken ship is still here, and still the Merchants'");
		if(i >= 0)
		{
			double dx = game->ship_x(i) - wokeX;
			double dy = game->ship_y(i) - wokeY;
			char what[128];
			snprintf(what, sizeof(what), "the woken ship moves (%.1f)", sqrt(dx * dx + dy * dy));
			Check(sqrt(dx * dx + dy * dy) > 50., what);
			Check(game->ship_is_disabled(i) == 0, "it stays awake");
		}
		woke = 2;
		game->condition_set("api11 woke", 1);
	}
	End("on_step");
}

ES_LOADER_EXPORT int32_t ES_CALL es_loader_init(const EsHost *host, EsLoader *loader)
{
	if(host->version < 11 || !ES_HOST_HAS(host, ship_reactivate))
		return 1;
	game = host;
	if(loader->size > sizeof(EsLoader))
		loader->size = sizeof(EsLoader);
	loader->version = ES_API_VERSION;
	loader->name = "API 11 test loader";
	loader->load_mods = load_mods;
	loader->count = mod_count;
	loader->step = step;
	loader->planet_button = planet_button;
	loader->pilot_loaded = pilot_loaded;
	return 0;
}
