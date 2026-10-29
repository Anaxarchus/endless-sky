/* RuntimeDefinitions.h
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

#include <string>

class ConditionsStore;
class DataNode;
class DataWriter;
class Outfit;
class Ship;



// Outfits and ship models that mods define while the game runs (mod API 11),
// kept for the pilot: an ordered list of records (the mod that made it, the
// kind, the name, and either the node it loaded or the base and the changes
// applied to it). ModState clears, loads and saves them with the pilot, in a
// "runtime definitions" block written before the pilot's ships, and the records
// are applied as soon as that block is read, so every name the rest of the save
// mentions is defined before it is used.
//
// The objects live in the game's own sets (GameData::Outfits() and Ships()),
// which hand out pointers that must stay valid, so a definition is always
// loaded into the set's object of that name (a placeholder made by an earlier
// lookup becomes the real thing), and nothing is ever erased. Definitions made
// for an earlier pilot stay in the sets but are "stale": not defined for this
// pilot, invisible to the mods, and replaced if this pilot defines the name.
// Main thread only.
class RuntimeDefinitions {
public:
	// Another pilot is about to be made or loaded: every record becomes stale.
	static void NewPilot();
	// Load and apply a save file's "runtime definitions" block.
	static void Load(const DataNode &node, const ConditionsStore *conditions);
	// Write the "runtime definitions" block, if there are any.
	static void Save(DataWriter &out);

	// Define what `text` holds for `mod`: top-level outfit and ship nodes, in
	// data-file syntax. Answers how many it defined (counting the ones it had
	// already defined with the same text). Sets `jumpRange` when an outfit
	// added a jump range, so the caller must update the systems' neighbors.
	static int Define(const std::string &mod, const std::string &text, const ConditionsStore *conditions,
		bool &jumpRange);
	// A copy of an outfit or a ship model with `changes` (child nodes in
	// data-file syntax) applied on top. Answers whether it is defined.
	static bool DefineOutfitVariant(const std::string &mod, const std::string &name, const std::string &base,
		const std::string &changes, const ConditionsStore *conditions, bool &jumpRange);
	static bool DefineShipVariant(const std::string &mod, const std::string &name, const std::string &base,
		const std::string &changes, const ConditionsStore *conditions);

	// 0: unknown (or stale); 1: defined by the game data or a plugin; 2: defined
	// at runtime for this pilot. `kind` is "outfit" or "ship".
	static int Defined(const std::string &kind, const std::string &name);
	// Whether an object was defined at runtime, but not for this pilot.
	static bool IsStale(const Outfit *outfit);
	static bool IsStale(const Ship *model);
};
