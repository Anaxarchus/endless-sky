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
class PlayerInfo;
class Point;
class Ship;
class ShipEvent;
class System;
class UI;



// The game's side of the native modding API (source/modding/es_api.h, and
// docs/modding-api.md): it finds the mod loaders (shared libraries in a
// "modloaders" folder of the resources or the config folder), hands them the
// host table, runs their hooks and shows what their mods ask for. Without a
// mod loader, every function here does nothing and the game plays as usual.
//
// Everything here runs on the main thread: in flight while the calculation
// thread is paused, and from the planet screen, map and mod panels, which only
// run while it is stopped. The loaders are called only from that thread.
class ModHarness {
public:
	// Open the mod loaders, once the game's data has loaded and before a pilot
	// is loaded or a menu shown, so "mod loaders" is right from the start. The
	// loaders load their mods later, at the first hook, when there is a world.
	static void Init();
	// Run the mods' hooks for this engine step: on_day for each new day, on_enter
	// on arriving in a system, then on_step. The first call (or the first UI
	// call) starts the harness: it has the mod loaders load their mods from the
	// enabled resource and plugin folders (opening the loaders first, if Init
	// has not). Ships the mods spawn go
	// into newShips.
	static void Step(PlayerInfo &player, const std::list<std::shared_ptr<Ship>> &ships,
		std::list<std::shared_ptr<Ship>> &newShips, int step);
	// Keep what the mods need from this engine step's ship events: the
	// player's kills and the ships destroyed (mod API 9). Called every step,
	// also when Step does not run (paused, or a panel over the flight view),
	// so nothing is lost; the next Step's hooks see it.
	static void CollectEvents(const std::list<ShipEvent> &events);
	// End every mod, and close the mod loaders.
	static void Shutdown();
	// The player landed: start the harness if need be and run on_enter for the
	// system, so mods can put up planet buttons even for a pilot loaded on a planet.
	static void Landed(PlayerInfo &player);
	// The engine rebuilt its ship list (taking off, loading): ships the mods
	// spawned are gone, so the next step runs on_enter again.
	static void ShipsReplaced();
	// The game is about to write the pilot (mod API 10): the loaders' save
	// entry runs, so what the mods write now is saved with it. Called first
	// thing in PlayerInfo::Save(DataWriter &), so it covers every save; does
	// nothing from inside a hook, before the mods start, or for a pilot no hook
	// has run for yet.
	static void Saving(const PlayerInfo &player);

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
	// it does not use through. Null when there is no mod loader.
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

	// How many mod loaders are open (set by Init, once the game's data has
	// loaded) and how many mods they have loaded so far (0 until the first hook
	// has them load their mods; updated every engine step). Safe from any thread; the player's
	// "mod loaders" and "mods loaded" conditions read these.
	static int64_t LoaderCount();
	static int64_t LoadedCount();

	// Set a ship's levels, clamped to [0, maximum]. Used by the host table the
	// mod loaders call (source/modding/es_api.h); a friend of Entity.
	static void SetHull(Ship &ship, double value);
	static void SetShields(Ship &ship, double value);
	// Mod API 11: wake a disabled ship (not the player's) up: crew up to what it
	// needs (at least one), hull a little above the level at which it is disabled
	// (shields as they are), and no longer disabled. Answers whether it was.
	static bool Reactivate(Ship &ship);
	// What a shipyard charges for a brand-new ship of this model, when no used
	// hull or outfits are in stock: its chassis and every outfit at full price
	// (what stock depreciation answers when it holds no records). Mod API 9's
	// model_cost, and the price of PlayerInfo::DeliverShip.
	static int64_t NewShipCost(const Ship *model);
};
