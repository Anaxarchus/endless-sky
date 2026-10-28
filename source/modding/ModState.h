/* ModState.h
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
#include <string>

class DataNode;
class DataWriter;
class Outfit;
class Planet;
class Ship;



// What the mods keep with the pilot, kept here rather than in PlayerInfo: each
// mod's saved text (mod API 6), the shipyard stock mods track (mod API 9) and
// the notes they show for models out of stock, and the outfitter stock they
// track (mod API 10). PlayerInfo clears, loads, saves and validates it with the
// pilot, in the same blocks and order as always ("mod data", "ship stock", then
// "outfit stock"). Without a mod, all of it stays empty and the shipyards and
// outfitters behave as they always have. Main thread only.
class ModState {
public:
	// Forget everything: a pilot is about to be made or loaded. Also moves
	// Generation() on, so the mod harness knows the pilot changed.
	static void Clear();
	// A number that changes with every Clear().
	static uint64_t Generation();
	// Load a top-level node of the save file, if it is one of ours ("mod data",
	// "ship stock" or "outfit stock"); answers whether it was.
	static bool Load(const DataNode &child);
	// Write our blocks, if there is anything to write.
	static void Save(DataWriter &out);
	// Drop stock for planets, models and outfits that are not valid; called once
	// a loaded pilot's events have been applied.
	static void Validate();

	// Text that mods keep with this pilot, by mod and key. An empty value
	// removes the key. Saved and loaded with the pilot.
	static const std::string &Data(const std::string &mod, const std::string &key);
	static void SetData(const std::string &mod, const std::string &key, const std::string &value);

	// Per-planet shipyard stock. Tracking is off unless a mod turns it on; that
	// switch is not saved, so without the mod the shipyards behave as they
	// always have. A count of -1 means the model is untracked at that planet,
	// which means unlimited. The counts are saved with the pilot.
	static bool ShipStockEnabled();
	static void SetShipStockEnabled(bool enabled);
	static int ShipStock(const Planet *planet, const Ship *model);
	// A negative count untracks the model at that planet.
	static void SetShipStock(const Planet *planet, const Ship *model, int count);
	// Change a tracked count, never going below zero. An untracked model stays
	// untracked. Returns the new count, or -1 if untracked.
	static int AddShipStock(const Planet *planet, const Ship *model, int delta);
	// Text the shipyard adds when this model is out of stock at this planet, e.g.
	// to say where it can be ordered. Not saved; an empty note removes it.
	static const std::string &ShipOrderNote(const Planet *planet, const Ship *model);
	static void SetShipOrderNote(const Planet *planet, const Ship *model, const std::string &note);

	// Per-planet outfitter stock (mod API 10), as the shipyard stock above: off
	// unless a mod turns it on (not saved), -1 for untracked (unlimited), and
	// the counts saved with the pilot.
	static bool OutfitStockEnabled();
	static void SetOutfitStockEnabled(bool enabled);
	static int OutfitStock(const Planet *planet, const Outfit *outfit);
	// Whether an outfit can be tracked: a defined one that is not a map or a
	// license (which the outfitter sells as the knowledge they give, not as
	// things on a shelf).
	static bool OutfitTrackable(const Outfit *outfit);
	// A negative count untracks the outfit at that planet; maps and licenses are not tracked.
	static void SetOutfitStock(const Planet *planet, const Outfit *outfit, int count);
	// For the outfitter, which sells the outfits the player sold there (its
	// "in stock", `soldBack` of them) before new ones. How many of `count` can
	// be bought there now: all of them unless the stock is on and the outfit is
	// tracked there, and then no more than soldBack plus the tracked count.
	static int OutfitsAvailable(const Planet *planet, const Outfit *outfit, int count, int soldBack);
	// The outfitter sold `count` of the outfit, `soldBack` of which it had from
	// the player: take the rest off the tracked count, if it is tracked.
	static void OutfitsBought(const Planet *planet, const Outfit *outfit, int count, int soldBack);
};
