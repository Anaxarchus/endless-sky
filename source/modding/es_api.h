/* es_api.h
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

/* The game's native modding API: the contract between Endless Sky and the mod
 * loaders it finds at start (see docs/modding-api.md). Pure C, usable from C and
 * C++. This file is the single source of truth: a loader copies it unchanged.
 *
 * A mod loader is a shared library (.dylib on macOS, .so on Linux, .dll on
 * Windows) in a "modloaders" folder of the game's resources or of its config
 * folder. It exports one function, es_loader_init (ES_LOADER_INIT_SYMBOL):
 *
 *     #include "es_api.h"
 *     static const EsHost *game;
 *     ES_LOADER_EXPORT int32_t ES_CALL es_loader_init(const EsHost *host, EsLoader *loader)
 *     {
 *         if(host->version < ES_API_VERSION || !ES_HOST_HAS(host, ship_in_stock))
 *             return 1;
 *         game = host;
 *         // Fill only what the game reserved (an older game reserves less).
 *         if(loader->size > sizeof(EsLoader))
 *             loader->size = sizeof(EsLoader);
 *         loader->version = ES_API_VERSION;
 *         loader->name = "Example loader 1.0";
 *         loader->load_mods = my_load_mods;
 *         loader->step = my_step;
 *         ...
 *         return 0;
 *     }
 *
 * The game gives it the host table (EsHost: what the game offers) and a zeroed
 * EsLoader with its `size` set to the bytes the game reserved; the loader fills
 * in what it implements and answers 0, or nonzero to decline. The game then
 * calls load_mods once with the enabled resource and plugin folders, and calls
 * the other entries as the game runs. Any entry may be left null.
 *
 * On Windows, link the loader's own dependencies statically (or ship them
 * where the game's executable is): DLLs next to a loader in "modloaders" are
 * not searched. File names match ".dll" in any case there.
 *
 * Versions. ES_API_VERSION is the mod API number. Both tables grow only by
 * appending fields at the end, so a table from an older version is a prefix of
 * a newer one, and `size` says how much of it there is: check a field with
 * ES_HOST_HAS / ES_LOADER_HAS before using a field newer than the version you
 * need.
 *  - The game accepts any loader whose version is ES_API_MIN_VERSION or later,
 *    newer than the game's included. It uses min(loader->size,
 *    sizeof(EsLoader)) bytes of the loader's table and ignores fields it does
 *    not know.
 *  - The loader checks host->version and ES_HOST_HAS for what it needs, and
 *    answers nonzero from es_loader_init when the game is too old for it. It
 *    writes no more of EsLoader than the `size` the game passed.
 *  - The game rejects (and logs) a loader whose version is older than
 *    ES_API_MIN_VERSION, or whose `size` does not cover the API 9 table.
 *
 * Threads and timing. Everything runs on the game's main thread. The game calls
 * the loader's entries only from inside its own hooks (an engine step, the
 * planet screen, the map, the outfitter, a mod panel), and the loader calls the
 * host table only from inside those calls (and from es_loader_init, load_mods
 * and shutdown, for the calls that need no world). Outside a hook there is no
 * world: queries answer 0 or "", and writes do nothing.
 *
 * Ownership. Nothing is freed across the boundary.
 *  - A string the host returns stays valid until the next call into the host;
 *    copy it to keep it.
 *  - A string the game passes to the loader is valid for the duration of that
 *    call only.
 *  - The strings the loader puts in EsLoader (name) must stay valid until its
 *    shutdown returns.
 *
 * Mod identity. The game, not the loader, decides which mod a call is for: the
 * loader brackets every hook it runs for a mod with hook_timing(mod, hook, 1)
 * and hook_timing(mod, hook, 0), and the calls marked "Mod-scoped" act for the
 * mod named by the last begin. The game remembers which loader runs each mod
 * name (the first loader to run a name keeps it), and sends the per-mod
 * callbacks (planet_button, panel, ...) to that loader with the mod's name.
 * A bracket lasts at most until the game's call into the loader returns: one a
 * loader leaves open (its mod failed before the end) is closed then, and an end
 * without a begin is ignored. During es_loader_init a loader runs no mod, so
 * mod-scoped calls do nothing there.
 *
 * Pilots. When the player makes or loads another pilot (or another save), the
 * game drops everything mods put up for the last one (map overlays, buttons,
 * panels, tagged ships, kills, losses) and does not run the days in between: the
 * loaders see a fresh world, starting with an enter. Mods are loaded once per
 * run of the game (load_mods, and so a mod's on_load), not once per pilot; a
 * mod keeps per-pilot state in its saved data (data / set_data).
 *
 * Ships are named by their index into this step's snapshot of the ships in the
 * current system, 0 <= i < ship_count(). An index is only meaningful within one
 * hook call. Booleans are ints (0 or 1) to keep the ABI plain. */
#ifndef ES_API_H_
#define ES_API_H_

#include <stddef.h>
#include <stdint.h>

/* The mod API version this header describes, and the oldest the game accepts. */
#define ES_API_VERSION 9
#define ES_API_MIN_VERSION 9

/* The symbol a loader exports, and its type. */
#define ES_LOADER_INIT_SYMBOL "es_loader_init"

/* The calling convention of every function in both tables and of
 * es_loader_init: __cdecl on 32-bit Windows, the platform's C default elsewhere. */
#if defined(_WIN32) && !defined(_WIN64)
#define ES_CALL __cdecl
#else
#define ES_CALL
#endif

/* Put before the definition of es_loader_init to export it. */
#if defined(_WIN32) || defined(__CYGWIN__)
#define ES_LOADER_VISIBILITY __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define ES_LOADER_VISIBILITY __attribute__((visibility("default")))
#else
#define ES_LOADER_VISIBILITY
#endif
#ifdef __cplusplus
#define ES_LOADER_EXPORT extern "C" ES_LOADER_VISIBILITY
#else
#define ES_LOADER_EXPORT ES_LOADER_VISIBILITY
#endif

/* Whether a table's `size` covers a field: use before a field newer than the
 * version you checked. */
#define ES_HOST_HAS(host, field) ((host)->size >= offsetof(EsHost, field) + sizeof((host)->field))
#define ES_LOADER_HAS(loader, field) ((loader)->size >= offsetof(EsLoader, field) + sizeof((loader)->field))

#ifdef __cplusplus
extern "C" {
#endif

/* What the game offers a loader. Filled once by the game; read-only for the
 * loader. Fields are append-only: new fields go at the end, existing fields
 * never change, move or go away. */
typedef struct EsHost {
	/* sizeof(EsHost) as the game was built. */
	uint32_t size;
	/* The game's ES_API_VERSION. */
	uint32_t version;

	/* Write a line to the game's log (and stderr). */
	void (ES_CALL *log)(const char *text);
	/* Show a message to the player in the in-flight message area. */
	void (ES_CALL *message)(const char *text);

	/* The engine's step counter (60 steps per second of game time). */
	int32_t (ES_CALL *step)(void);
	/* The display name of the system the player is in. */
	const char *(ES_CALL *system_name)(void);

	int32_t (ES_CALL *ship_count)(void);
	/* The index of the player's flagship, or -1. */
	int32_t (ES_CALL *flagship)(void);
	const char *(ES_CALL *ship_name)(int32_t i);
	const char *(ES_CALL *ship_model)(int32_t i);
	int32_t (ES_CALL *ship_is_player)(int32_t i);
	int32_t (ES_CALL *ship_is_disabled)(int32_t i);
	double (ES_CALL *ship_hull)(int32_t i);
	double (ES_CALL *ship_max_hull)(int32_t i);
	double (ES_CALL *ship_shields)(int32_t i);
	double (ES_CALL *ship_max_shields)(int32_t i);
	double (ES_CALL *ship_energy)(int32_t i);
	double (ES_CALL *ship_fuel)(int32_t i);
	/* Set a level, clamped to [0, max]. */
	void (ES_CALL *ship_set_hull)(int32_t i, double value);
	void (ES_CALL *ship_set_shields)(int32_t i, double value);

	/* The player's conditions: the same store missions and events read and write. */
	int64_t (ES_CALL *condition_get)(const char *name);
	void (ES_CALL *condition_set)(const char *name, int64_t value);

	/* ---- Mod API 2 ----
	 * Unless marked "in flight", these work in any hook, flying or landed. Systems
	 * and governments are named by their true (data) names. */

	/* The date, as days since the game's epoch. */
	int32_t (ES_CALL *day)(void);
	/* The true name of the system the player is in, or "". */
	const char *(ES_CALL *here)(void);

	/* The galaxy: every valid, accessible system, sorted by true name. */
	int32_t (ES_CALL *system_count)(void);
	const char *(ES_CALL *system_at)(int32_t i);
	const char *(ES_CALL *system_government)(const char *system);
	int32_t (ES_CALL *system_link_count)(const char *system);
	const char *(ES_CALL *system_link)(const char *system, int32_t k);
	int32_t (ES_CALL *system_inhabited)(const char *system);
	/* The defense fleets of the system's planets, the ones a player fights to dominate them. */
	int32_t (ES_CALL *system_defense_count)(const char *system);
	const char *(ES_CALL *system_defense_fleet)(const char *system, int32_t k);

	/* Apply data changes as an event, as a plugin's event would, and save them
	 * with the pilot. `text` is the body of an event, for example
	 *   system "Alpha Centauri"\n\tgovernment "Free Worlds"
	 * Answers 1 when it applied something. */
	int32_t (ES_CALL *change)(const char *text);

	/* Governments. */
	double (ES_CALL *attitude)(const char *government, const char *toward);
	double (ES_CALL *reputation)(const char *government);
	void (ES_CALL *set_reputation)(const char *government, double value);
	/* Whether a plugin with this name is installed and enabled (loaded by the game). */
	int32_t (ES_CALL *plugin_enabled)(const char *name);
	/* A fleet definition's average strength (the sum of its ships' costs). */
	double (ES_CALL *fleet_strength)(const char *fleet);
	/* Mod API 3. A fleet's expected stats, averaged over its variants by weight:
	 * "ships" (how many), "hull", "shields", "dps" (the shield plus hull damage its
	 * weapons deal per second) and "cost" are totals for the fleet; "speed" is the
	 * average top speed of its ships. 0 for an unknown fleet or stat. */
	double (ES_CALL *fleet_stat)(const char *fleet, const char *stat);
	/* Mod API 3. The fleets that arrive in a system on their own (its `fleet` lines),
	 * in the order its data lists them. */
	int32_t (ES_CALL *system_fleet_count)(const char *system);
	const char *(ES_CALL *system_fleet)(const char *system, int32_t k);

	/* In flight: put a fleet into the player's system under `government`, tagging
	 * its ships with `tag`. `arrive` 0 places them in the system already; 1 has
	 * them jump in. Answers how many ships were added. */
	int32_t (ES_CALL *spawn)(const char *fleet, const char *government, const char *tag, int32_t arrive);
	const char *(ES_CALL *ship_tag)(int32_t i);
	const char *(ES_CALL *ship_government)(int32_t i);
	double (ES_CALL *ship_strength)(int32_t i);
	/* The strength of the ships in the system with this tag that can still fight. */
	double (ES_CALL *tag_strength)(const char *tag);
	/* How many ships whose tag starts with `prefix` the player has disabled or
	 * destroyed since the last call with that prefix. */
	int32_t (ES_CALL *player_kills)(const char *prefix);

	/* The map: drawn on every map view, kept until the mod clears them. Each mod
	 * has its own set. Colours are 0..1. */
	void (ES_CALL *map_clear)(void); /* Mod-scoped. */
	void (ES_CALL *map_line)(const char *from, const char *to, double r, double g, double b); /* Mod-scoped. */
	void (ES_CALL *map_marker)(const char *system, double r, double g, double b, const char *label); /* Mod-scoped. */
	/* Mod API 4. A section of the map's side panel, shown while `system` is the
	 * selected system: a heading in the given colour, then `text` (lines split on
	 * "\n"). Cleared with the mod's other overlays by map_clear. */
	void (ES_CALL *map_detail)(const char *system, const char *title, const char *text, double r, double g, double b); /* Mod-scoped. */

	/* The planet screen: a button with this label, or none for "". Clicking it
	 * runs the mod's on_planet_button hook. */
	void (ES_CALL *planet_button)(const char *label); /* Mod-scoped. */
	/* A panel: title, text, then buttons. Clicking a button runs the mod's
	 * on_panel hook with its index. Opening one while it is open replaces its
	 * contents. Panels open only from a hook run by the player's own action (a
	 * planet button or a panel button). */
	void (ES_CALL *panel_open)(const char *title, const char *text); /* Mod-scoped. */
	void (ES_CALL *panel_button)(const char *label); /* Mod-scoped. */
	void (ES_CALL *panel_close)(void); /* Mod-scoped. */

	/* The loader, around every call it makes into one of its mods' hooks:
	 * `begin` 1 just before the call, with the mod's name and the hook's,
	 * and 0 just after. Required: the game times hooks with it, and it is how
	 * the game knows which mod is running. Every call marked "Mod-scoped"
	 * acts for the mod named by the last begin (the game keeps its own record
	 * of it), and does nothing outside a bracket. Brackets do not nest. */
	void (ES_CALL *hook_timing)(const char *mod, const char *hook, int32_t begin);

	/* ---- Mod API 5 ----
	 * The player's own ships, wherever they are, are named by their UUIDs (the
	 * "uuid" in the save file), which last across saving and loading. Outfits are
	 * named by their true names. */

	/* The outfitter: a button with this label, or none for "". It is enabled while
	 * the mod's can_outfitter hook says so, and clicking it runs on_outfitter. */
	void (ES_CALL *outfitter_button)(const char *label); /* Mod-scoped. */
	/* In the outfitter's hooks: its selected ships, the one in focus first. */
	int32_t (ES_CALL *outfitter_ship_count)(void);
	const char *(ES_CALL *outfitter_ship)(int32_t k);
	/* The player's ships that are not destroyed, in the order of the fleet. */
	int32_t (ES_CALL *fleet_count)(void);
	const char *(ES_CALL *fleet_ship)(int32_t k);
	const char *(ES_CALL *fleet_ship_name)(const char *ship);
	const char *(ES_CALL *fleet_ship_model)(const char *ship);
	/* How many of an outfit a ship has installed, and how many of those are integrated. */
	int32_t (ES_CALL *ship_outfit_count)(const char *ship, const char *outfit);
	int32_t (ES_CALL *ship_integrated_count)(const char *ship, const char *outfit);
	/* Integrate one installed outfit that is not yet integrated: it can no longer
	 * be uninstalled, sold, moved or plundered, and its mass and the outfit space
	 * it takes up are scaled by `scale` (0..1), each saving rounded to a whole
	 * number. Capacities (weapon, engine, cargo) are left as they are. Ammunition cannot
	 * be integrated. Answers 1 when it was. */
	int32_t (ES_CALL *ship_integrate)(const char *ship, const char *outfit, double scale);
	/* An outfit's display name, category, an attribute ("mass" and "cost" too), and whether it is a weapon. */
	const char *(ES_CALL *outfit_name)(const char *outfit);
	const char *(ES_CALL *outfit_category)(const char *outfit);
	double (ES_CALL *outfit_attribute)(const char *outfit, const char *attribute);
	int32_t (ES_CALL *outfit_is_weapon)(const char *outfit);
	/* Ask the player to confirm: a dialog with OK and Cancel (the default). OK
	 * runs the mod's on_confirm hook with `token`. Only from a hook the player
	 * started (a planet, panel or outfitter button, or on_confirm). */
	void (ES_CALL *confirm)(const char *text, int32_t token); /* Mod-scoped. */

	/* ---- Mod API 6 ---- */

	/* Text a mod keeps with the pilot, by key (its own keys only); "" for none.
	 * Setting "" removes the key. Saved and loaded with the pilot. */
	const char *(ES_CALL *data)(const char *key); /* Mod-scoped. */
	void (ES_CALL *set_data)(const char *key, const char *text); /* Mod-scoped. */
	/* Planets, by true name. planet: the planet the player is landed on, or "". */
	const char *(ES_CALL *planet)(void);
	const char *(ES_CALL *planet_name)(const char *planet);
	const char *(ES_CALL *planet_system)(const char *planet);
	const char *(ES_CALL *planet_government)(const char *planet);
	int32_t (ES_CALL *planet_count)(const char *system);
	const char *(ES_CALL *planet_at)(const char *system, int32_t k);
	/* Whether the player may use a service there now: "bank", "trading", "job board",
	 * "hire crew", "outfitter" or "shipyard" (the port has it, and the player has access). */
	int32_t (ES_CALL *planet_service)(const char *planet, const char *service);
	/* A table in the open panel, under its text: headings, then rows, each as
	 * tab-separated cells. Clicking a row runs the mod's on_panel_row with its index. */
	void (ES_CALL *panel_columns)(const char *headers); /* Mod-scoped. */
	void (ES_CALL *panel_row)(const char *cells); /* Mod-scoped. */
	/* Ask the player for a whole number from 1 to `most`; OK runs the mod's
	 * on_number with `token` and the number. Only from a hook the player started. */
	void (ES_CALL *ask_number)(const char *text, int32_t token, int32_t most); /* Mod-scoped. */

	/* ---- Mod API 7 ---- */

	/* Open the mod's panel on a full screen of its own over the planet screen, as
	 * the outfitter is: the title and the table on the left, the text and the
	 * buttons in a sidebar on the right; a click on a column heading runs the mod's
	 * on_panel_header. Only from a hook the player started, or to refresh the mod's
	 * own open screen. panel_close (or Escape) closes it. */
	void (ES_CALL *screen_open)(const char *title, const char *text); /* Mod-scoped. */
	/* Show a row of the open panel's table as chosen (-1 for none). */
	void (ES_CALL *panel_select)(int32_t row); /* Mod-scoped. */

	/* ---- Mod API 8 ---- */

	/* A line chart at the top of the screen's sidebar: `values` are numbers
	 * separated by commas. Up to three series share the chart's scale. */
	void (ES_CALL *panel_chart)(const char *label, const char *values); /* Mod-scoped. */
	/* Put a job on the job board of the planet the player is landed on: `body` is
	 * the body of a mission as the game's data writes it (name, description,
	 * cargo, destination, deadline, on complete ...), and the game names it
	 * "<mod>: <name>". Answers 1 when it is on the board; 0 when the planet has no
	 * job board, the text makes no job, or one of that name is already offered or taken. */
	int32_t (ES_CALL *offer_job)(const char *name, const char *body); /* Mod-scoped. */

	/* ---- Mod API 9 ---- */

	/* Shipyard stock (PlayerInfo's ship stock). Planets and ship models are named
	 * by their true names (a model is a key of the game's ships: a stock model or a
	 * named variant). Switch tracking on or off for this session; the switch is not
	 * saved, and loading a pilot turns it off. */
	void (ES_CALL *ship_stock_on)(int32_t on);
	/* A model's count at a planet, or -1: untracked (unlimited), or an unknown name. */
	int32_t (ES_CALL *ship_stock)(const char *planet, const char *model);
	/* Set the count; a negative count untracks the model there. Unknown names do nothing. */
	void (ES_CALL *set_ship_stock)(const char *planet, const char *model, int32_t count);
	/* The text the shipyard shows in place of "New hulls are on order; check back
	 * in a few days." when the model is out of stock there ("" removes it; not saved). */
	void (ES_CALL *ship_order_note)(const char *planet, const char *model, const char *text);
	/* The ship models a planet's shipyards sell, sorted by true name. */
	int32_t (ES_CALL *shipyard_count)(const char *planet);
	const char *(ES_CALL *shipyard_model)(const char *planet, int32_t k);
	/* A model's display name, full new price (hull and outfits, what a shipyard
	 * charges for a new one and what buy_ship charges), category, and the
	 * licenses it needs, separated by commas ("" for none). */
	const char *(ES_CALL *model_name)(const char *model);
	int64_t (ES_CALL *model_cost)(const char *model);
	const char *(ES_CALL *model_category)(const char *model);
	const char *(ES_CALL *model_licenses)(const char *model);
	/* Only in a hook the player started: deliver a new ship of this model at the
	 * planet the player is landed on, for its full new price (model_cost),
	 * named `name` ("" for a random name). No shop is touched: not the shelf
	 * stock, the used hulls or outfits in stock, nor their depreciation. Answers 1
	 * when it was bought; 0 when not landed, the model is unknown, the player
	 * lacks a license it needs, or cannot pay. */
	int32_t (ES_CALL *buy_ship)(const char *model, const char *name); /* Mod-scoped. */
	/* Ships destroyed in the player's system since the last engine step whose hooks
	 * ran (each ship once, up to 4096), in the hooks an engine step runs: the
	 * count; then each one's model (true name of the stock model), government,
	 * and whether the player's ships destroyed it. */
	int32_t (ES_CALL *losses)(void);
	const char *(ES_CALL *loss_model)(int32_t k);
	const char *(ES_CALL *loss_government)(int32_t k);
	int32_t (ES_CALL *loss_by_player)(int32_t k);
	/* A label under a system on the map, in the stack with the markers' labels, but
	 * with no ring. Drawn and cleared with the mod's other overlays. */
	void (ES_CALL *map_label)(const char *system, const char *text, double r, double g, double b); /* Mod-scoped. */
	/* A ring around a system on the map, `scale` times a marker's (clamped to
	 * 0.5 .. 3), with no label. Drawn and cleared with the mod's other overlays. */
	void (ES_CALL *map_ring)(const char *system, double scale, double r, double g, double b); /* Mod-scoped. */
	/* How many used hulls of this model (counted by its base model) the player
	 * sold to the landed planet's shipyard and can buy back until takeoff; 0 when
	 * not landed or for an unknown model. Always 0 unless the game includes
	 * used-hull support (the fork's patches/used-hulls.patch). */
	int32_t (ES_CALL *ship_in_stock)(const char *model);
} EsHost;

/* What a loader offers the game, filled in by es_loader_init. The game passes
 * it zeroed, with `size` set to the bytes it reserved; the loader writes no
 * more than that, sets `size` to the bytes it filled and `version` to the
 * ES_API_VERSION it was built with. The game reads min(size, sizeof(EsLoader))
 * bytes. Any entry may be null. Fields are append-only, as in EsHost.
 *
 * Every entry that runs mod code must bracket each mod's hook with
 * host->hook_timing (see EsHost). The game makes the mod-specific callbacks
 * (planet_button through number) only to the loader that ran that mod's hooks,
 * with the mod's name as `mod`. */
typedef struct EsLoader {
	/* The bytes of this struct the loader filled. */
	uint32_t size;
	/* The ES_API_VERSION the loader was built with. */
	uint32_t version;
	/* The loader's name and version, for the game's log ("Some Loader 1.2").
	 * Must stay valid until shutdown returns. */
	const char *name;

	/* Load the mods. Called once, inside a hook (a world exists), with the
	 * enabled sources in the game's load order: the game's resources folder
	 * first (name ""), then every enabled plugin (its folder, and its name).
	 * A later source's data overrides an earlier one's, and loaders should
	 * treat their mods the same way. The loader decides what to load from each
	 * folder; the game does not look for scripts itself. Paths are UTF-8. A
	 * zipped plugin is passed as-is, as the path of its .zip file; a loader may
	 * skip such sources. The arrays and their strings are valid for this call
	 * only. */
	void (ES_CALL *load_mods)(int32_t count, const char *const *paths, const char *const *names);
	/* How many mods are currently loaded and running: not counting mods that
	 * failed to load, or that the loader has since stopped or disabled. The
	 * game asks again every engine step ("mods loaded"). */
	int32_t (ES_CALL *count)(void);

	/* An engine step's hooks, in this order: day for each new day that passed
	 * in play (days since the game's epoch), enter when the player is in a new
	 * system (its true name; also on landing), then step (the engine's step
	 * counter). */
	void (ES_CALL *step)(int32_t step);
	void (ES_CALL *day)(int32_t day);
	void (ES_CALL *enter)(const char *system);

	/* Per-mod callbacks, for the mod named `mod`. */
	/* The player clicked the planet button that mod `mod` put up. */
	void (ES_CALL *planet_button)(const char *mod);
	/* The player clicked button `button` of the panel mod `mod` opened. */
	void (ES_CALL *panel)(const char *mod, int32_t button);
	/* Whether mod `mod`'s outfitter button is enabled for `outfit` ("" for none): 1 or 0. */
	int32_t (ES_CALL *can_outfitter)(const char *mod, const char *outfit);
	/* The player clicked mod `mod`'s outfitter button with `outfit` selected. */
	void (ES_CALL *outfitter)(const char *mod, const char *outfit);
	/* The player confirmed what mod `mod` asked, with its token. */
	void (ES_CALL *confirm)(const char *mod, int32_t token);
	/* The player clicked row `row` of mod `mod`'s panel table. */
	void (ES_CALL *panel_row)(const char *mod, int32_t row);
	/* The player clicked heading `column` of mod `mod`'s screen table. */
	void (ES_CALL *panel_header)(const char *mod, int32_t column);
	/* The player answered mod `mod`'s number dialog. */
	void (ES_CALL *number)(const char *mod, int32_t token, int32_t value);

	/* End every mod. Called once, when the game ends the mods; the library is
	 * unloaded right after, so nothing of it may keep running. */
	void (ES_CALL *shutdown)(void);
} EsLoader;

/* The type of es_loader_init. Answers 0 when the loader is ready. */
typedef int32_t (ES_CALL *EsLoaderInitFn)(const EsHost *host, EsLoader *loader);

#ifdef __cplusplus
}
#endif

#endif
