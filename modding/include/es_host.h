/* es_host.h
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

/* The C surface the game exposes to the Dado mod harness (modding/harness).
 * The game implements these in source/ModHarness.cpp; the test host in
 * modding/test implements them over fake data.
 *
 * Everything here is called on the main thread, from inside a hook, while the
 * calculation thread is paused. Outside a hook there is no world: queries
 * answer 0 or "", and writes do nothing.
 *
 * Ships are named by their index into this step's snapshot of the ships in
 * the current system, 0 <= i < es_ship_count(). An index is only meaningful
 * within one hook call. Booleans are ints (0 or 1) to keep the ABI plain. */
#ifndef ES_HOST_H_
#define ES_HOST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write a line to the game's log (and stderr). */
void es_log(const char *text);
/* Show a message to the player in the in-flight message area. */
void es_message(const char *text);

/* The engine's step counter (60 steps per second of game time). */
int32_t es_step(void);
/* The display name of the system the player is in. */
const char *es_system_name(void);

int32_t es_ship_count(void);
/* The index of the player's flagship, or -1. */
int32_t es_flagship(void);
const char *es_ship_name(int32_t i);
const char *es_ship_model(int32_t i);
int32_t es_ship_is_player(int32_t i);
int32_t es_ship_is_disabled(int32_t i);
double es_ship_hull(int32_t i);
double es_ship_max_hull(int32_t i);
double es_ship_shields(int32_t i);
double es_ship_max_shields(int32_t i);
double es_ship_energy(int32_t i);
double es_ship_fuel(int32_t i);
/* Set a level, clamped to [0, max]. */
void es_ship_set_hull(int32_t i, double value);
void es_ship_set_shields(int32_t i, double value);

/* The player's conditions: the same store missions and events read and write. */
int64_t es_condition_get(const char *name);
void es_condition_set(const char *name, int64_t value);

/* ---- Mod API 2 ----
 * Unless marked "in flight", these work in any hook, flying or landed. Systems
 * and governments are named by their true (data) names. */

/* The date, as days since the game's epoch. */
int32_t es_day(void);
/* The true name of the system the player is in, or "". */
const char *es_here(void);

/* The galaxy: every valid, accessible system, sorted by true name. */
int32_t es_system_count(void);
const char *es_system_at(int32_t i);
const char *es_system_government(const char *system);
int32_t es_system_link_count(const char *system);
const char *es_system_link(const char *system, int32_t k);
int32_t es_system_inhabited(const char *system);
/* The defense fleets of the system's planets, the ones a player fights to dominate them. */
int32_t es_system_defense_count(const char *system);
const char *es_system_defense_fleet(const char *system, int32_t k);

/* Apply data changes as an event, as a plugin's event would, and save them
 * with the pilot. `text` is the body of an event, for example
 *   system "Alpha Centauri"\n\tgovernment "Free Worlds"
 * Answers 1 when it applied something. */
int32_t es_change(const char *text);

/* Governments. */
double es_attitude(const char *government, const char *toward);
double es_reputation(const char *government);
void es_set_reputation(const char *government, double value);
/* Whether a plugin with this name is installed and enabled (loaded by the game). */
int32_t es_plugin_enabled(const char *name);
/* A fleet definition's average strength (the sum of its ships' costs). */
double es_fleet_strength(const char *fleet);
/* Mod API 3. A fleet's expected stats, averaged over its variants by weight:
 * "ships" (how many), "hull", "shields", "dps" (the shield plus hull damage its
 * weapons deal per second) and "cost" are totals for the fleet; "speed" is the
 * average top speed of its ships. 0 for an unknown fleet or stat. */
double es_fleet_stat(const char *fleet, const char *stat);
/* Mod API 3. The fleets that arrive in a system on their own (its `fleet` lines),
 * in the order its data lists them. */
int32_t es_system_fleet_count(const char *system);
const char *es_system_fleet(const char *system, int32_t k);

/* In flight: put a fleet into the player's system under `government`, tagging
 * its ships with `tag`. `arrive` 0 places them in the system already; 1 has
 * them jump in. Answers how many ships were added. */
int32_t es_spawn(const char *fleet, const char *government, const char *tag, int32_t arrive);
const char *es_ship_tag(int32_t i);
const char *es_ship_government(int32_t i);
double es_ship_strength(int32_t i);
/* The strength of the ships in the system with this tag that can still fight. */
double es_tag_strength(const char *tag);
/* How many ships whose tag starts with `prefix` the player has disabled or
 * destroyed since the last call with that prefix. */
int32_t es_player_kills(const char *prefix);

/* The map: drawn on every map view, kept until the mod clears them. Each mod
 * has its own set. Colours are 0..1. */
void es_map_clear(const char *mod);
void es_map_line(const char *mod, const char *from, const char *to, double r, double g, double b);
void es_map_marker(const char *mod, const char *system, double r, double g, double b, const char *label);
/* Mod API 4. A section of the map's side panel, shown while `system` is the
 * selected system: a heading in the given colour, then `text` (lines split on
 * "\n"). Cleared with the mod's other overlays by es_map_clear. */
void es_map_detail(const char *mod, const char *system, const char *title, const char *text, double r, double g, double b);

/* The planet screen: a button with this label, or none for "". Clicking it
 * runs the mod's on_planet_button hook. */
void es_planet_button(const char *mod, const char *label);
/* A panel: title, text, then buttons. Clicking a button runs the mod's
 * on_panel hook with its index. Opening one while it is open replaces its
 * contents. Panels open only from a hook run by the player's own action (a
 * planet button or a panel button). */
void es_panel_open(const char *mod, const char *title, const char *text);
void es_panel_button(const char *mod, const char *label);
void es_panel_close(const char *mod);

/* The harness, around each hook it runs for a mod: `begin` 1 just before the
 * call, 0 just after. The game times hooks with it. Not part of the mod API. */
void es_hook_timing(const char *mod, const char *hook, int32_t begin);

/* ---- Mod API 5 ----
 * The player's own ships, wherever they are, are named by their UUIDs (the
 * "uuid" in the save file), which last across saving and loading. Outfits are
 * named by their true names. */

/* The outfitter: a button with this label, or none for "". It is enabled while
 * the mod's can_outfitter hook says so, and clicking it runs on_outfitter. */
void es_outfitter_button(const char *mod, const char *label);
/* In the outfitter's hooks: its selected ships, the one in focus first. */
int32_t es_outfitter_ship_count(void);
const char *es_outfitter_ship(int32_t k);
/* The player's ships that are not destroyed, in the order of the fleet. */
int32_t es_fleet_count(void);
const char *es_fleet_ship(int32_t k);
const char *es_fleet_ship_name(const char *ship);
const char *es_fleet_ship_model(const char *ship);
/* How many of an outfit a ship has installed, and how many of those are integrated. */
int32_t es_ship_outfit_count(const char *ship, const char *outfit);
int32_t es_ship_integrated_count(const char *ship, const char *outfit);
/* Integrate one installed outfit that is not yet integrated: it can no longer
 * be uninstalled, sold, moved or plundered, and its mass and the outfit space
 * it takes up are scaled by `scale` (0..1), each saving rounded to a whole
 * number. Capacities (weapon, engine, cargo) are left as they are. Ammunition cannot
 * be integrated. Answers 1 when it was. */
int32_t es_ship_integrate(const char *ship, const char *outfit, double scale);
/* An outfit's display name, category, an attribute ("mass" and "cost" too), and whether it is a weapon. */
const char *es_outfit_name(const char *outfit);
const char *es_outfit_category(const char *outfit);
double es_outfit_attribute(const char *outfit, const char *attribute);
int32_t es_outfit_is_weapon(const char *outfit);
/* Ask the player to confirm: a dialog with OK and Cancel (the default). OK
 * runs the mod's on_confirm hook with `token`. Only from a hook the player
 * started (a planet, panel or outfitter button, or on_confirm). */
void es_confirm(const char *mod, const char *text, int32_t token);

/* ---- Mod API 6 ---- */

/* Text a mod keeps with the pilot, by key (its own keys only); "" for none.
 * Setting "" removes the key. Saved and loaded with the pilot. */
const char *es_data(const char *mod, const char *key);
void es_set_data(const char *mod, const char *key, const char *text);
/* Planets, by true name. es_planet: the planet the player is landed on, or "". */
const char *es_planet(void);
const char *es_planet_name(const char *planet);
const char *es_planet_system(const char *planet);
const char *es_planet_government(const char *planet);
int32_t es_planet_count(const char *system);
const char *es_planet_at(const char *system, int32_t k);
/* Whether the player may use a service there now: "bank", "trading", "job board",
 * "hire crew", "outfitter" or "shipyard" (the port has it, and the player has access). */
int32_t es_planet_service(const char *planet, const char *service);
/* A table in the open panel, under its text: headings, then rows, each as
 * tab-separated cells. Clicking a row runs the mod's on_panel_row with its index. */
void es_panel_columns(const char *mod, const char *headers);
void es_panel_row(const char *mod, const char *cells);
/* Ask the player for a whole number from 1 to `most`; OK runs the mod's
 * on_number with `token` and the number. Only from a hook the player started. */
void es_ask_number(const char *mod, const char *text, int32_t token, int32_t most);

/* ---- Mod API 7 ---- */

/* Open the mod's panel on a full screen of its own over the planet screen, as
 * the outfitter is: the title and the table on the left, the text and the
 * buttons in a sidebar on the right; a click on a column heading runs the mod's
 * on_panel_header. Only from a hook the player started, or to refresh the mod's
 * own open screen. panel_close (or Escape) closes it. */
void es_screen_open(const char *mod, const char *title, const char *text);
/* Show a row of the open panel's table as chosen (-1 for none). */
void es_panel_select(const char *mod, int32_t row);

#ifdef __cplusplus
}
#endif

#endif
