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
class Planet;
class Ship;



// What the mods keep with the pilot, kept here rather than in PlayerInfo: each
// mod's saved text (mod API 6), the shipyard stock mods track (mod API 9) and
// the notes they show for models out of stock. PlayerInfo clears, loads, saves
// and validates it with the pilot, in the same blocks and order as always
// ("mod data", then "ship stock"). Without a mod, all of it stays empty and
// the shipyards behave as they always have. Main thread only.
class ModState {
public:
	// Forget everything: a pilot is about to be made or loaded. Also moves
	// Generation() on, so the mod harness knows the pilot changed.
	static void Clear();
	// A number that changes with every Clear().
	static uint64_t Generation();
	// Load a top-level node of the save file, if it is one of ours ("mod data"
	// or "ship stock"); answers whether it was.
	static bool Load(const DataNode &child);
	// Write our blocks, if there is anything to write.
	static void Save(DataWriter &out);
	// Drop stock for planets and models that are not valid; called once a
	// loaded pilot's events have been applied.
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
};
