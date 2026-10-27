/* ModHarness.h
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

#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

class Outfit;
class Panel;
class System;
class PlayerInfo;
class Point;
class Ship;
class ShipEvent;
class UI;



// Runs mods written in DadoScript (see modding/README.md). The harness and its
// script engine are compiled in only when the game is built with ES_DADO_MODS;
// otherwise every function here does nothing.
//
// Everything here runs on the main thread: in flight while the calculation
// thread is paused, and from the planet screen, map and mod panels, which only
// run while it is stopped. The script engine belongs to the thread that made it.
class ModHarness {
public:
	// Run the mods' hooks for this engine step: on_day for each new day, on_enter
	// on arriving in a system, then on_step. The first call (or the first UI
	// call) starts the harness and loads the mods found in each resource or
	// plugin folder's "dado" folder. Ships the mods spawn go into newShips;
	// events are this step's ship events, used to credit the player's kills.
	static void Step(PlayerInfo &player, const std::list<std::shared_ptr<Ship>> &ships,
		std::list<std::shared_ptr<Ship>> &newShips, const std::list<ShipEvent> &events, int step);
	// End the script engine and every mod in it.
	static void Shutdown();
	// The player landed: start the harness if need be and run on_enter for the
	// system, so mods can put up planet buttons even for a pilot loaded on a planet.
	static void Landed(PlayerInfo &player);
	// The engine rebuilt its ship list (taking off, loading): ships the mods
	// spawned are gone, so the next step runs on_enter again.
	static void ShipsReplaced();

	// Draw the mods' map overlays. center and zoom are the map's own transform:
	// a system at galaxy position p is drawn at zoom * (p + center).
	static void DrawMap(const PlayerInfo &player, const Point &center, double zoom);
	// Draw the mods' side-panel sections for the selected system on the map,
	// below the box that holds its orbits and the map key (which is `top` tall).
	static void DrawMapDetails(const System *system, double top);
	// The label of the planet screen's mod button, or "" for none: one mod's own
	// label, or "Mods" when several mods put one up.
	static std::string PlanetButtonLabel();
	// The player clicked the planet screen's mod button. A panel the mod opens
	// is shown by the planet screen's mod panel (see NewPlanetPanel).
	static void PlanetButton(PlayerInfo &player, UI &ui);
	// The panel mods show on the planet screen. Like the bank and trading
	// panels, the planet screen swaps it into its content area and lets the keys
	// it does not use through. Null when the harness is not built in.
	static std::shared_ptr<Panel> NewPlanetPanel(PlayerInfo &player);
	// Whether a mod has a panel open, and closing it (the player left it).
	static bool PanelOpen();
	static void ClosePanel();

	// The outfitter's mod buttons (mod API 5): one label per mod that put one
	// up, in the order of the mods' names. `ships` are the outfitter's selected
	// ships, the one in focus first, and `outfit` its selected outfit (or null).
	static std::vector<std::string> OutfitterButtons();
	// Whether button i is enabled: the mod's can_outfitter hook decides.
	static bool OutfitterButtonActive(size_t i, PlayerInfo &player, const std::vector<Ship *> &ships,
		const Outfit *outfit);
	// The player clicked button i: run the mod's on_outfitter hook.
	static void OutfitterButton(size_t i, PlayerInfo &player, UI &ui, const std::vector<Ship *> &ships,
		const Outfit *outfit);

	// Whether the harness is compiled into this build, and how many mods it has
	// loaded so far (0 until it starts). Safe from any thread; the player's
	// "dado harness" and "dado mods loaded" conditions read these.
	static bool IsBuiltIn();
	static int64_t LoadedCount();

	// Set a ship's levels, clamped to [0, maximum]. Used by the C surface the
	// harness calls (modding/include/es_host.h); a friend of Entity.
	static void SetHull(Ship &ship, double value);
	static void SetShields(Ship &ship, double value);
};
