/* ModState.cpp
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

#include "ModState.h"

#include "RuntimeDefinitions.h"

#include "../DataNode.h"
#include "../DataWriter.h"
#include "../GameData.h"
#include "../Outfit.h"
#include "../Planet.h"
#include "../Ship.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

using namespace std;

namespace {
	// A mod's text may hold anything, but a save file's token cannot hold both
	// kinds of quote, or a line break. Those, the percent sign and the other
	// control characters are written as %XX.
	string EncodeModData(const string &text)
	{
		static const char *HEX = "0123456789ABCDEF";
		string out;
		out.reserve(text.size());
		for(unsigned char c : text)
		{
			if(c == '%' || c == '"' || c == '`' || c < 0x20 || c == 0x7F)
			{
				out += '%';
				out += HEX[c >> 4];
				out += HEX[c & 15];
			}
			else
				out += static_cast<char>(c);
		}
		return out;
	}

	string DecodeModData(const string &text)
	{
		auto hex = [](char c) -> int {
			if(c >= '0' && c <= '9')
				return c - '0';
			if(c >= 'A' && c <= 'F')
				return c - 'A' + 10;
			if(c >= 'a' && c <= 'f')
				return c - 'a' + 10;
			return -1;
		};
		string out;
		out.reserve(text.size());
		for(size_t i = 0; i < text.size(); ++i)
		{
			if(text[i] == '%' && i + 2 < text.size() && hex(text[i + 1]) >= 0 && hex(text[i + 2]) >= 0)
			{
				out += static_cast<char>(hex(text[i + 1]) * 16 + hex(text[i + 2]));
				i += 2;
			}
			else
				out += text[i];
		}
		return out;
	}

	// Mods' text, by mod, then key.
	map<string, map<string, string>> modData;
	// Shipyard stock by planet, then model (see ShipStock()). Only the counts are saved.
	bool shipStockEnabled = false;
	map<const Planet *, map<const Ship *, int>> shipStock;
	map<const Planet *, map<const Ship *, string>> shipOrderNotes;
	// Outfitter stock by planet, then outfit (mod API 10). Only the counts are saved.
	bool outfitStockEnabled = false;
	map<const Planet *, map<const Outfit *, int>> outfitStock;

	// A count from a save file, clamped to an int; negative counts are not kept.
	int SavedCount(double value)
	{
		return static_cast<int>(min(value, static_cast<double>(numeric_limits<int>::max())));
	}

	// Stock by planet, then item, written as a block of the save file sorted by
	// name, if it holds anything.
	template <class Item, class NameOf>
	void SaveStock(DataWriter &out, const string &key, const map<const Planet *, map<const Item *, int>> &stock,
		NameOf nameOf)
	{
		if(stock.empty())
			return;
		map<string, map<string, int>> sorted;
		for(const auto &[stockPlanet, items] : stock)
			for(const auto &[item, count] : items)
				sorted[stockPlanet->TrueName()][nameOf(item)] = count;

		out.Write(key);
		out.BeginChild();
		{
			for(const auto &[planetName, items] : sorted)
			{
				out.Write(planetName);
				out.BeginChild();
				{
					for(const auto &[itemName, count] : items)
						out.Write(itemName, count);
				}
				out.EndChild();
			}
		}
		out.EndChild();
	}
	// Bumped by Clear(): which pilot the state belongs to.
	uint64_t generation = 0;
}



void ModState::Clear()
{
	modData.clear();
	shipStockEnabled = false;
	shipStock.clear();
	shipOrderNotes.clear();
	outfitStockEnabled = false;
	outfitStock.clear();
	RuntimeDefinitions::NewPilot();
	++generation;
}



uint64_t ModState::Generation()
{
	return generation;
}



bool ModState::Load(const DataNode &child)
{
	const string &key = child.Token(0);
	if(key == "mod data")
	{
		for(const DataNode &grand : child)
			if(grand.Size() >= 3)
				modData[grand.Token(0)][grand.Token(1)] = DecodeModData(grand.Token(2));
		return true;
	}
	if(key == "ship stock")
	{
		// Names that no longer exist are dropped here, and ones that are no
		// longer defined in Validate().
		for(const DataNode &grand : child)
		{
			const Planet *stockPlanet = GameData::Planets().Find(grand.Token(0));
			if(!stockPlanet)
				continue;
			for(const DataNode &great : grand)
			{
				const Ship *model = GameData::Ships().Find(great.Token(0));
				if(model && great.Size() >= 2 && great.Value(1) >= 0.)
					shipStock[stockPlanet][model] = SavedCount(great.Value(1));
			}
		}
		return true;
	}
	if(key == "outfit stock")
	{
		// As ship stock: unknown names are dropped here, undefined ones in Validate().
		for(const DataNode &grand : child)
		{
			const Planet *stockPlanet = GameData::Planets().Find(grand.Token(0));
			if(!stockPlanet)
				continue;
			for(const DataNode &great : grand)
			{
				const Outfit *outfit = GameData::Outfits().Find(great.Token(0));
				if(outfit && OutfitTrackable(outfit) && great.Size() >= 2 && great.Value(1) >= 0.)
					outfitStock[stockPlanet][outfit] = SavedCount(great.Value(1));
			}
		}
		return true;
	}
	return false;
}



void ModState::Save(DataWriter &out)
{
	// Text that mods keep with this pilot.
	if(!modData.empty())
	{
		out.Write("mod data");
		out.BeginChild();
		{
			for(const auto &[mod, values] : modData)
				for(const auto &[name, value] : values)
					out.Write(mod, name, EncodeModData(value));
		}
		out.EndChild();
	}

	// Shipyard and outfitter stock that mods keep with this pilot, sorted by name.
	SaveStock(out, "ship stock", shipStock, [](const Ship *model) { return model->VariantName(); });
	SaveStock(out, "outfit stock", outfitStock, [](const Outfit *outfit) { return outfit->TrueName(); });
}



void ModState::LoadDefinitions(const DataNode &child, const ConditionsStore *conditions)
{
	RuntimeDefinitions::Load(child, conditions);
}



void ModState::SaveDefinitions(DataWriter &out)
{
	RuntimeDefinitions::Save(out);
}



void ModState::Validate()
{
	// Drop shipyard stock for planets or ship models that are not valid (e.g. a plugin was removed).
	// Planet::IsValid() also needs the planet to be in a system, so stock for a planet an event has
	// taken out of its system is dropped too. There is no cheaper "is defined" check for planets,
	// and the mod that tracks stock seeds it again on load, so that is acceptable.
	for(auto it = shipStock.begin(); it != shipStock.end(); )
	{
		if(it->first->IsValid())
			erase_if(it->second, [](const pair<const Ship *const, int> &entry) { return !entry.first->IsValid(); });
		if(!it->first->IsValid() || it->second.empty())
			it = shipStock.erase(it);
		else
			++it;
	}
	// The same for outfitter stock, and outfits that are no longer defined.
	for(auto it = outfitStock.begin(); it != outfitStock.end(); )
	{
		if(it->first->IsValid())
			erase_if(it->second, [](const pair<const Outfit *const, int> &entry) { return !entry.first->IsDefined(); });
		if(!it->first->IsValid() || it->second.empty())
			it = outfitStock.erase(it);
		else
			++it;
	}
}



const string &ModState::Data(const string &mod, const string &key)
{
	static const string none;
	auto it = modData.find(mod);
	if(it == modData.end())
		return none;
	auto vit = it->second.find(key);
	return vit == it->second.end() ? none : vit->second;
}



void ModState::SetData(const string &mod, const string &key, const string &value)
{
	if(value.empty())
	{
		auto it = modData.find(mod);
		if(it != modData.end())
		{
			it->second.erase(key);
			if(it->second.empty())
				modData.erase(it);
		}
		return;
	}
	modData[mod][key] = value;
}



bool ModState::ShipStockEnabled()
{
	return shipStockEnabled;
}



void ModState::SetShipStockEnabled(bool enabled)
{
	shipStockEnabled = enabled;
}



int ModState::ShipStock(const Planet *planet, const Ship *model)
{
	auto it = shipStock.find(planet);
	if(it == shipStock.end())
		return -1;
	auto sit = it->second.find(model);
	return sit == it->second.end() ? -1 : sit->second;
}



void ModState::SetShipStock(const Planet *planet, const Ship *model, int count)
{
	// Never track placeholder models, which would be saved without a name.
	if(!planet || !model || !model->IsValid())
		return;
	if(count < 0)
	{
		auto it = shipStock.find(planet);
		if(it != shipStock.end())
		{
			it->second.erase(model);
			if(it->second.empty())
				shipStock.erase(it);
		}
		return;
	}
	shipStock[planet][model] = count;
}



int ModState::AddShipStock(const Planet *planet, const Ship *model, int delta)
{
	if(!planet || !model || !model->IsValid())
		return -1;
	auto it = shipStock.find(planet);
	if(it == shipStock.end())
		return -1;
	auto sit = it->second.find(model);
	if(sit == it->second.end())
		return -1;
	// Clamp to [0, INT_MAX] without overflowing.
	int64_t count = static_cast<int64_t>(sit->second) + delta;
	sit->second = static_cast<int>(clamp<int64_t>(count, 0, numeric_limits<int>::max()));
	return sit->second;
}



const string &ModState::ShipOrderNote(const Planet *planet, const Ship *model)
{
	static const string none;
	auto it = shipOrderNotes.find(planet);
	if(it == shipOrderNotes.end())
		return none;
	auto nit = it->second.find(model);
	return nit == it->second.end() ? none : nit->second;
}



void ModState::SetShipOrderNote(const Planet *planet, const Ship *model, const string &note)
{
	if(!planet || !model)
		return;
	if(note.empty())
	{
		auto it = shipOrderNotes.find(planet);
		if(it != shipOrderNotes.end())
		{
			it->second.erase(model);
			if(it->second.empty())
				shipOrderNotes.erase(it);
		}
		return;
	}
	shipOrderNotes[planet][model] = note;
}



bool ModState::OutfitStockEnabled()
{
	return outfitStockEnabled;
}



void ModState::SetOutfitStockEnabled(bool enabled)
{
	outfitStockEnabled = enabled;
}



bool ModState::OutfitTrackable(const Outfit *outfit)
{
	return outfit && outfit->IsDefined() && !outfit->Get("map") && !outfit->TrueName().ends_with(" License");
}



int ModState::OutfitStock(const Planet *planet, const Outfit *outfit)
{
	auto it = outfitStock.find(planet);
	if(it == outfitStock.end())
		return -1;
	auto oit = it->second.find(outfit);
	return oit == it->second.end() ? -1 : oit->second;
}



void ModState::SetOutfitStock(const Planet *planet, const Outfit *outfit, int count)
{
	// Never track outfits that are not defined, which would be saved without a
	// name, nor maps and licenses.
	if(!planet || !outfit || !OutfitTrackable(outfit))
		return;
	if(count < 0)
	{
		auto it = outfitStock.find(planet);
		if(it != outfitStock.end())
		{
			it->second.erase(outfit);
			if(it->second.empty())
				outfitStock.erase(it);
		}
		return;
	}
	outfitStock[planet][outfit] = count;
}



int ModState::OutfitsAvailable(const Planet *planet, const Outfit *outfit, int count, int soldBack)
{
	const int tracked = outfitStockEnabled ? OutfitStock(planet, outfit) : -1;
	if(tracked < 0 || count <= 0)
		return count;
	const int64_t most = static_cast<int64_t>(max(0, soldBack)) + tracked;
	return static_cast<int>(min<int64_t>(count, most));
}



void ModState::OutfitsBought(const Planet *planet, const Outfit *outfit, int count, int soldBack)
{
	if(!outfitStockEnabled || count <= 0)
		return;
	auto it = outfitStock.find(planet);
	if(it == outfitStock.end())
		return;
	auto oit = it->second.find(outfit);
	if(oit == it->second.end())
		return;
	const int fromShelf = count - min(count, max(0, soldBack));
	oit->second = max(0, oit->second - fromShelf);
}
