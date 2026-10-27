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

#include "../DataNode.h"
#include "../DataWriter.h"
#include "../GameData.h"
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
	// Bumped by Clear(): which pilot the state belongs to.
	uint64_t generation = 0;
}



void ModState::Clear()
{
	modData.clear();
	shipStockEnabled = false;
	shipStock.clear();
	shipOrderNotes.clear();
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
					shipStock[stockPlanet][model] = static_cast<int>(
						min(great.Value(1), static_cast<double>(numeric_limits<int>::max())));
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

	// Shipyard stock that mods keep with this pilot, sorted by name.
	if(!shipStock.empty())
	{
		map<string, map<string, int>> sorted;
		for(const auto &[stockPlanet, models] : shipStock)
			for(const auto &[model, count] : models)
				sorted[stockPlanet->TrueName()][model->VariantName()] = count;

		out.Write("ship stock");
		out.BeginChild();
		{
			for(const auto &[planetName, models] : sorted)
			{
				out.Write(planetName);
				out.BeginChild();
				{
					for(const auto &[modelName, count] : models)
						out.Write(modelName, count);
				}
				out.EndChild();
			}
		}
		out.EndChild();
	}
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
