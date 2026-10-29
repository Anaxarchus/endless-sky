/* RuntimeDefinitions.cpp
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

#include "RuntimeDefinitions.h"

#include "../DataFile.h"
#include "../DataNode.h"
#include "../DataWriter.h"
#include "../GameData.h"
#include "../Logger.h"
#include "../Outfit.h"
#include "../Ship.h"
#include "../Weapon.h"

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

using namespace std;

namespace {
	// One definition a mod made for this pilot.
	struct Record {
		// The mod that made it.
		string mod;
		bool isShip = false;
		// Made by define_outfit_variant or define_ship_variant: saved as (name,
		// base, changes), and derived from the base again on load.
		bool isVariant = false;
		string name;
		// The base of a variant, or of a ship node with three tokens ("" for none).
		string base;
		// What is loaded: the node a mod defined, or for a variant, an outfit or
		// ship node with the changes as its children.
		DataNode node;
		// The record in a canonical form, to tell the same definition from another.
		string text;
		// Whether its object holds it now (a record a save holds may fail to load,
		// e.g. when the plugin its base came from was removed; it is kept, and saved).
		bool applied = false;
	};
	using Key = pair<bool, string>;

	// This pilot's records, in the order they were made (and are applied).
	vector<Record> records;
	map<Key, size_t> recordIndex;
	// What every object defined at runtime in this run of the game holds now, by
	// kind and name: the text of the record it was loaded from, or "" when it was
	// emptied. It outlives the pilot, as the objects do.
	map<Key, string> content;
	// The objects defined at runtime, and their names.
	map<const Outfit *, string> runtimeOutfits;
	map<const Ship *, string> runtimeShips;

	// The sets only hand out const objects; the objects themselves are not const
	// (they live in GameData's sets), and this is where mods define them.
	Outfit *MutableOutfit(const string &name)
	{
		return const_cast<Outfit *>(GameData::Outfits().Get(name));
	}

	Ship *MutableShip(const string &name)
	{
		return const_cast<Ship *>(GameData::Ships().Get(name));
	}

	string Canonical(const DataNode &node, bool childrenOnly)
	{
		DataWriter out;
		if(childrenOnly)
			for(const DataNode &child : node)
				out.Write(child);
		else
			out.Write(node);
		return out.SaveToString();
	}

	void Log(const string &mod, const string &message)
	{
		Logger::Log("Mod \"" + mod + "\": " + message, Logger::Level::WARNING);
	}

	bool IsApplied(const Key &key)
	{
		auto it = recordIndex.find(key);
		return it != recordIndex.end() && records[it->second].applied;
	}

	bool StaleOutfit(const Outfit *outfit)
	{
		auto it = runtimeOutfits.find(outfit);
		return it != runtimeOutfits.end() && !IsApplied(Key(false, it->second));
	}

	bool StaleShip(const Ship *ship)
	{
		auto it = runtimeShips.find(ship);
		return it != runtimeShips.end() && !IsApplied(Key(true, it->second));
	}
}



// Kept in a class, so that it can be a friend of Outfit, Ship and Weapon (which
// name it).
class RuntimeDefinitionsDetail {
public:
	// Whether a name belongs to the game data or a plugin: defined, and never
	// defined at runtime.
	static bool IsData(const Key &key)
	{
		if(content.contains(key))
			return false;
		if(key.first)
		{
			const Ship *model = GameData::Ships().Find(key.second);
			return model && model->isDefined;
		}
		const Outfit *outfit = GameData::Outfits().Find(key.second);
		return outfit && outfit->IsDefined();
	}

	static bool UsableOutfit(const string &name)
	{
		const Outfit *outfit = GameData::Outfits().Find(name);
		return outfit && outfit->IsDefined() && !StaleOutfit(outfit);
	}

	static bool UsableShip(const string &name)
	{
		const Ship *model = GameData::Ships().Find(name);
		return model && model->isDefined && model->IsValid() && !StaleShip(model);
	}

	// A copy of `base` with `node`'s children applied, as Outfit::Load applies
	// them. Load adds to some lists, so a changed description replaces the base's,
	// and a changed display name gets its own plural. A weapon block loads on top
	// of the base's weapon (Weapon::Load supports that), except that its live
	// effects are kept as they are: Load converts them from counts per lifetime
	// into chances per frame, which must happen once.
	static Outfit OutfitVariant(const Outfit &base, const DataNode &node, const ConditionsStore *conditions)
	{
		Outfit result = base;
		bool hasDescription = false;
		bool hasDisplayName = false;
		bool hasPlural = false;
		bool hasWeapon = false;
		for(const DataNode &child : node)
		{
			const string &key = child.Token(0);
			hasDescription |= (key == "description");
			hasDisplayName |= (key == "display name");
			hasPlural |= (key == "plural");
			hasWeapon |= (key == "weapon");
		}
		if(hasDescription)
			result.description.Clear();
		if(hasDisplayName && !hasPlural)
			result.pluralName.clear();
		map<const Effect *, int> baseLiveEffects;
		if(hasWeapon && result.weapon)
		{
			Weapon weapon = *result.weapon;
			baseLiveEffects.swap(weapon.liveEffects);
			weapon.totalLifetime = -1.;
			result.weapon = make_shared<Weapon>(std::move(weapon));
		}
		result.Load(node, conditions);
		if(hasWeapon && result.weapon)
		{
			Weapon weapon = *result.weapon;
			if(weapon.lifetime > 0)
				for(const auto &[effect, chance] : baseLiveEffects)
					weapon.liveEffects.emplace(effect, chance);
			weapon.totalLifetime = -1.;
			result.weapon = make_shared<Weapon>(std::move(weapon));
		}
		return result;
	}

	// Whether a ship node names only outfits that are defined for this pilot (a
	// ship model with an undefined outfit is not valid), and a usable base.
	static bool ShipNodeUsable(const Record &record, string &why)
	{
		if(!record.base.empty())
		{
			if(record.base == record.name)
			{
				why = "a ship variant cannot be its own base";
				return false;
			}
			if(!UsableShip(record.base))
			{
				why = "the base ship \"" + record.base + "\" is not defined";
				return false;
			}
		}
		for(const DataNode &child : record.node)
		{
			const string &key = child.Token(0);
			if(key == "outfits")
			{
				for(const DataNode &grand : child)
					if(!UsableOutfit(grand.Token(0)))
					{
						why = "the outfit \"" + grand.Token(0) + "\" is not defined";
						return false;
					}
			}
			else if(key == "gun" || key == "turret")
			{
				string outfit = child.Size() >= 4 ? child.Token(3) : child.Size() == 2 ? child.Token(1) : "";
				if(!outfit.empty() && !UsableOutfit(outfit))
				{
					why = "the outfit \"" + outfit + "\" is not defined";
					return false;
				}
			}
		}
		return true;
	}

	// Load a record into its object, if it can be: nothing changes when it
	// cannot. A ship still needs FinishLoading. Sets `jumpRange` when an outfit
	// has a jump range (which Outfit::Load adds to the systems' neighbor distances).
	static bool LoadRecord(const Record &record, const ConditionsStore *conditions, bool &jumpRange, string &why)
	{
		if(!record.isShip)
		{
			Outfit result;
			if(record.isVariant)
			{
				if(record.base == record.name)
				{
					why = "an outfit variant cannot be its own base";
					return false;
				}
				if(!UsableOutfit(record.base))
				{
					why = "the base outfit \"" + record.base + "\" is not defined";
					return false;
				}
				result = OutfitVariant(*GameData::Outfits().Find(record.base), record.node, conditions);
			}
			else
				result.Load(record.node, conditions);
			Outfit *target = MutableOutfit(record.name);
			*target = std::move(result);
			runtimeOutfits[target] = record.name;
			if(target->Get("jump drive") && target->Get("jump range") > 0.)
				jumpRange = true;
			return true;
		}

		if(!ShipNodeUsable(record, why))
			return false;
		Ship *target = MutableShip(record.name);
		*target = Ship();
		target->Load(record.node, conditions);
		runtimeShips[target] = record.name;
		return true;
	}

	static void FinishShip(const Record &record)
	{
		MutableShip(record.name)->FinishLoading(true);
	}

	// Empty an object that holds a runtime definition no record of this pilot
	// can load: it is then undefined, as a removed plugin's would be.
	static void Empty(const Key &key)
	{
		auto it = content.find(key);
		if(it == content.end() || it->second.empty())
			return;
		if(key.first)
			*MutableShip(key.second) = Ship();
		else
			*MutableOutfit(key.second) = Outfit();
		it->second.clear();
	}
};



void RuntimeDefinitions::NewPilot()
{
	records.clear();
	recordIndex.clear();
}



namespace {
	// A record made from a node: `define` node, or a variant's parts.
	bool MakeDefineRecord(const string &mod, const DataNode &node, Record &record, string &why)
	{
		const string &key = node.Token(0);
		record.mod = mod;
		record.isVariant = false;
		if(key == "outfit" && node.Size() == 2)
			record.isShip = false;
		else if(key == "ship" && (node.Size() == 2 || node.Size() == 3))
		{
			record.isShip = true;
			if(node.Size() == 3)
				record.base = node.Token(1);
		}
		else
		{
			why = "a definition must be `outfit \"<name>\"`, `ship \"<name>\"` or `ship \"<base>\" \"<name>\"`";
			return false;
		}
		record.name = node.Token(node.Size() - 1);
		if(record.name.empty())
		{
			why = "a definition needs a name";
			return false;
		}
		record.node = node;
		record.text = "define\n" + Canonical(node, false);
		return true;
	}

	bool MakeVariantRecord(const string &mod, bool isShip, const string &name, const string &base,
		const DataNode &changes, Record &record, string &why)
	{
		if(name.empty() || base.empty())
		{
			why = "a variant needs a name and a base";
			return false;
		}
		record.mod = mod;
		record.isShip = isShip;
		record.isVariant = true;
		record.name = name;
		record.base = base;
		DataNode node;
		node.AddToken(isShip ? "ship" : "outfit");
		if(isShip)
			node.AddToken(base);
		node.AddToken(name);
		for(const DataNode &child : changes)
			node.AddChild(child);
		record.node = std::move(node);
		record.text = "variant\n" + base + "\n" + Canonical(record.node, true);
		return true;
	}

	// The children of a node holding `text` (child nodes in data-file syntax).
	DataNode ParseChanges(const string &text)
	{
		DataNode changes;
		istringstream in(text);
		DataFile file(in);
		for(const DataNode &node : file)
			changes.AddChild(node);
		return changes;
	}

	// Define one record for a mod, as a host call does. Answers whether it is
	// defined (also when it already was, with the same text).
	bool Define(Record &&record, const ConditionsStore *conditions, bool &jumpRange)
	{
		const Key key(record.isShip, record.name);
		const string kind = record.isShip ? "ship" : "outfit";
		auto it = recordIndex.find(key);
		if(it != recordIndex.end())
		{
			const Record &old = records[it->second];
			if(old.mod != record.mod)
				Log(record.mod, "cannot define " + kind + " \"" + record.name + "\": mod \"" + old.mod
					+ "\" defined it.");
			else if(old.text != record.text)
				Log(record.mod, "cannot define " + kind + " \"" + record.name
					+ "\" again with other text: definitions cannot change once made; give a new version a new name.");
			else
				return old.applied;
			return false;
		}
		if(RuntimeDefinitionsDetail::IsData(key))
		{
			Log(record.mod, "cannot define " + kind + " \"" + record.name
				+ "\": the game data or a plugin defines it.");
			return false;
		}
		string why;
		if(!RuntimeDefinitionsDetail::LoadRecord(record, conditions, jumpRange, why))
		{
			Log(record.mod, "cannot define " + kind + " \"" + record.name + "\": " + why + ".");
			return false;
		}
		record.applied = true;
		recordIndex[key] = records.size();
		records.push_back(std::move(record));
		const Record &added = records.back();
		if(added.isShip)
			RuntimeDefinitionsDetail::FinishShip(added);
		content[key] = added.text;
		Logger::Log("Mod \"" + added.mod + "\" defined " + kind + " \"" + added.name + "\".", Logger::Level::INFO);
		return true;
	}
}



void RuntimeDefinitions::Load(const DataNode &node, const ConditionsStore *conditions)
{
	const size_t first = records.size();
	for(const DataNode &child : node)
	{
		const string &key = child.Token(0);
		Record record;
		string why;
		bool made = false;
		if(key == "define" && child.Size() >= 2)
		{
			for(const DataNode &grand : child)
			{
				made = MakeDefineRecord(child.Token(1), grand, record, why);
				break;
			}
			if(!made && why.empty())
				why = "an empty definition";
		}
		else if((key == "outfit variant" || key == "ship variant") && child.Size() >= 4)
			made = MakeVariantRecord(child.Token(1), key == "ship variant", child.Token(2), child.Token(3), child,
				record, why);
		else
			why = "unrecognized";
		if(!made)
		{
			child.PrintTrace("Skipping runtime definition: " + why + ":");
			continue;
		}
		const Key recordKey(record.isShip, record.name);
		if(recordIndex.contains(recordKey))
		{
			child.PrintTrace("Skipping runtime definition of a name defined before:");
			continue;
		}
		if(RuntimeDefinitionsDetail::IsData(recordKey))
		{
			child.PrintTrace("Skipping runtime definition of a name the game data or a plugin defines:");
			continue;
		}
		recordIndex[recordKey] = records.size();
		records.push_back(std::move(record));
	}

	// Load what is not already loaded as it is recorded, in order: first every
	// outfit and ship, then finish the ships, as the game data loads. A record
	// whose object already holds it (the same pilot loaded again, or another
	// pilot defined the same) is left alone, unless its base was loaded again.
	set<Key> changed;
	bool jumpRange = false;
	for(size_t i = first; i < records.size(); ++i)
	{
		Record &record = records[i];
		const Key key(record.isShip, record.name);
		const bool baseChanged = !record.base.empty() && changed.contains(Key(record.isShip, record.base));
		auto it = content.find(key);
		if(it != content.end() && it->second == record.text && !baseChanged)
		{
			record.applied = true;
			continue;
		}
		string why;
		if(RuntimeDefinitionsDetail::LoadRecord(record, conditions, jumpRange, why))
		{
			record.applied = true;
			content[key] = record.text;
		}
		else
		{
			record.applied = false;
			RuntimeDefinitionsDetail::Empty(key);
			Logger::Log("Cannot load the runtime definition of " + string(record.isShip ? "ship" : "outfit") + " \""
				+ record.name + "\" (by mod \"" + record.mod + "\"): " + why + ". It is kept with the pilot.",
				Logger::Level::WARNING);
		}
		changed.insert(key);
	}
	// A ship model sums its outfits when it finishes, so finish every ship model
	// again if anything changed.
	if(!changed.empty())
		for(const Record &record : records)
			if(record.isShip && record.applied)
				RuntimeDefinitionsDetail::FinishShip(record);
	// A jump range an outfit added needs nothing here: loading a pilot updates
	// the systems' neighbors after this (PlayerInfo::ApplyChanges).
}



void RuntimeDefinitions::Save(DataWriter &out)
{
	if(records.empty())
		return;
	out.Write("runtime definitions");
	out.BeginChild();
	{
		for(const Record &record : records)
		{
			if(record.isVariant)
			{
				out.Write(record.isShip ? "ship variant" : "outfit variant", record.mod, record.name, record.base);
				out.BeginChild();
				{
					for(const DataNode &child : record.node)
						out.Write(child);
				}
				out.EndChild();
			}
			else
			{
				out.Write("define", record.mod);
				out.BeginChild();
				{
					out.Write(record.node);
				}
				out.EndChild();
			}
		}
	}
	out.EndChild();
}



int RuntimeDefinitions::Define(const string &mod, const string &text, const ConditionsStore *conditions,
	bool &jumpRange)
{
	istringstream in(text);
	DataFile file(in);
	// As the game data loads: the outfits first, so a ship may use an outfit
	// defined after it in the same text.
	int defined = 0;
	for(int pass = 0; pass < 2; ++pass)
		for(const DataNode &node : file)
		{
			const bool isShip = (node.Token(0) == "ship");
			if(isShip != (pass == 1))
				continue;
			Record record;
			string why;
			if(!MakeDefineRecord(mod, node, record, why))
			{
				Log(mod, "cannot define `" + node.Token(0) + (node.Size() >= 2 ? " " + node.Token(1) : string())
						+ "`: " + why + ".");
				continue;
			}
			defined += ::Define(std::move(record), conditions, jumpRange);
		}
	return defined;
}



bool RuntimeDefinitions::DefineOutfitVariant(const string &mod, const string &name, const string &base,
	const string &changes, const ConditionsStore *conditions, bool &jumpRange)
{
	Record record;
	string why;
	if(!MakeVariantRecord(mod, false, name, base, ParseChanges(changes), record, why))
	{
		Log(mod, "cannot define outfit variant \"" + name + "\": " + why + ".");
		return false;
	}
	return ::Define(std::move(record), conditions, jumpRange);
}



bool RuntimeDefinitions::DefineShipVariant(const string &mod, const string &name, const string &base,
	const string &changes, const ConditionsStore *conditions)
{
	Record record;
	string why;
	if(!MakeVariantRecord(mod, true, name, base, ParseChanges(changes), record, why))
	{
		Log(mod, "cannot define ship variant \"" + name + "\": " + why + ".");
		return false;
	}
	bool jumpRange = false;
	return ::Define(std::move(record), conditions, jumpRange);
}



int RuntimeDefinitions::Defined(const string &kind, const string &name)
{
	if(kind != "outfit" && kind != "ship")
		return 0;
	const Key key(kind == "ship", name);
	if(IsApplied(key))
		return 2;
	return RuntimeDefinitionsDetail::IsData(key) ? 1 : 0;
}



bool RuntimeDefinitions::IsStale(const Outfit *outfit)
{
	return StaleOutfit(outfit);
}



bool RuntimeDefinitions::IsStale(const Ship *model)
{
	return StaleShip(model);
}


