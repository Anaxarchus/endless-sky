/* fake_game.cpp
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

// A stand-in for the game: implements es_host.h over a few fake ships and a
// small fake galaxy, loads every mod in a directory through the real harness,
// and drives its hooks as the game would: steps in flight, new days, jumps, and
// the planet-screen button and panel clicks of a landed player, and the
// outfitter's mod buttons. It checks what the example mod, the adversarial test
// mods and the API 2 and API 5 probes (test/mods/api2_probe, api5_probe) did.
// Run by `dado-mods.sh test`.

#include "es_host.h"
// dadoc writes esmod.h without a C++ guard.
extern "C" {
#include "esmod.h"
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

using std::string;
using std::to_string;
using std::vector;

namespace {
	struct FakeShip {
		string name, model;
		bool player, disabled;
		double hull, maxHull, shields, maxShields;
		// API 2: set on ships es_spawn added.
		string tag = "", government = "";
		double strength = 0.;
		bool arrived = false;
	};

	struct FakeSystem {
		string name, display, government;
		bool inhabited;
		vector<string> links, defense;
		// The fleets that arrive on their own (mod API 3).
		vector<string> fleets = {};
	};

	// A fleet definition: its ships' models and strengths.
	using FleetShips = vector<std::pair<string, double>>;

	struct MapLine { string from, to; double r, g, b; };
	struct MapMarker { string system; double r, g, b; string label; };
	struct MapDetail { string system, title, text; double r, g, b; };
	struct MapOverlay { vector<MapLine> lines; vector<MapMarker> markers; vector<MapDetail> details; };

	struct Panel {
		bool open = false;
		string title, text;
		vector<string> buttons;
		// API 6: a table's headings and rows, as the mod sent them (tab-separated).
		string columns;
		vector<string> rows;
		// API 7: on a full screen, and the chosen row.
		bool full = false;
		int selected = -1;
	};

	struct Change { string text; bool applied; };

	std::vector<FakeShip> ships = {
		{"Buggy Barge", "Star Barge", true, false, 500, 1000, 0, 600},
		{"Full Barge", "Star Barge", true, false, 1000, 1000, 600, 600},
		{"Dead Barge", "Star Barge", true, true, 50, 1000, 0, 600},
		{"Pirate", "Bounder", false, false, 100, 800, 0, 400},
	};
	// Ships es_spawn added this step; they join the snapshot at the next step.
	std::vector<FakeShip> pending;
	std::map<string, int64_t> conditions;
	std::vector<string> messages;
	bool inHook = false;
	int step = 0;
	int failures = 0;

	// ---- API 2 state ----
	// Sorted by true name, as es_system_at promises.
	std::vector<FakeSystem> galaxy = {
		{"Alpha", "Alpha Prime", "Republic", true, {"Beta", "Gamma"}, {"Republic Defense"}},
		{"Beta", "Beta Reach", "Republic", true, {"Alpha", "Delta"}, {}},
		{"Delta", "Delta Hold", "Pirate", true, {"Beta", "Epsilon"}, {"Pirate Defense", "Pirate Raid"}, {"Pirate Raid", "Merchant"}},
		{"Epsilon", "Epsilon Drift", "Independent", false, {"Delta", "Zeta"}, {}},
		{"Gamma", "Gamma Gate", "Syndicate", true, {"Alpha", "Zeta"}, {"Syndicate Defense"}},
		{"Zeta", "Zeta Void", "Pirate", false, {"Epsilon", "Gamma"}, {}},
	};
	std::map<string, FleetShips> fleets = {
		{"Republic Defense", {{"Frigate", 300}, {"Frigate", 300}, {"Frigate", 300}}},
		{"Pirate Defense", {{"Bounder", 120}, {"Bounder", 120}}},
		{"Pirate Raid", {{"Bounder", 120}, {"Hawk", 90}, {"Hawk", 90}}},
		{"Syndicate Defense", {{"Cruiser", 500}, {"Cruiser", 500}}},
		{"Merchant", {{"Freighter", 100}}},
	};
	std::map<string, double> reputations = {
		{"Republic", 5.}, {"Pirate", -20.}, {"Syndicate", 0.}, {"Independent", 0.}, {"Merchant", 1.},
	};
	std::map<std::pair<string, string>, double> attitudes = {
		{{"Pirate", "Republic"}, -.5}, {{"Republic", "Pirate"}, -1.}, {{"Merchant", "Republic"}, .25},
	};
	int32_t day = 10000;
	string here = "Alpha";
	// Landed, the planet screen is up: no ship snapshot, and no spawning.
	bool flying = true;
	// Inside a hook the player's own click ran: panels may open.
	bool playerAction = false;
	std::vector<Change> changes;
	// The tag of every ship the player disabled or destroyed, in order, and how
	// far each es_player_kills prefix has read.
	std::vector<string> killLog;
	std::map<string, size_t> killCursor;
	std::map<string, MapOverlay> overlays;
	std::map<string, string> planetButtons;
	std::map<string, Panel> panels;

	bool Valid(int32_t i) { return inHook && flying && i >= 0 && i < static_cast<int32_t>(ships.size()); }

	// ---- API 5 state ----
	struct FakeOutfit { string display, category; double mass, cost; bool weapon; std::map<string, double> attributes; };
	std::map<string, FakeOutfit> outfitDefs = {
		{"Laser", {"Laser Cannon", "Guns", 10., 1000., true, {}}},
		{"Battery", {"Small Battery", "Power", 15., 400., false, {{"energy capacity", 1000.}}}},
		{"Gat Ammo", {"Gat Ammo", "Ammunition", .1, 5., false, {}}},
	};
	struct FleetShip {
		string uuid, name, model;
		std::map<string, int> outfits, integrated;
		double massSaved = 0.;
	};
	std::vector<FleetShip> fleet = {
		{"uuid-a", "Buggy Barge", "Star Barge", {{"Laser", 2}, {"Gat Ammo", 50}}, {}},
		{"uuid-b", "Full Barge", "Star Barge", {{"Battery", 1}}, {}},
	};
	std::map<string, string> outfitterButtons;
	// es_hook_timing: hooks begun and not yet ended, and finished calls by "mod hook".
	int hooksOpen = 0;
	bool hookTimingBalanced = true;
	std::map<string, int> hookCalls;
	// Set in the outfitter's hooks: the selected ships' UUIDs.
	const std::vector<string> *outfitterShips = nullptr;
	struct Confirm { string mod, text; int32_t token; };
	std::vector<Confirm> confirms;
	string returned;

	// ---- API 6 state ----
	std::map<string, std::map<string, string>> modData;
	struct FakePlanet { string name, display, system, government; bool bank; };
	std::vector<FakePlanet> planets = {
		{"Alpha I", "Alpha Prime I", "Alpha", "Republic", true},
		{"Alpha Station", "Alpha Station", "Alpha", "Republic", false},
		{"Delta Haven", "Delta Haven", "Delta", "Pirate", true},
	};
	// The planet the player is landed on, while landed.
	string landedPlanet = "Alpha I";
	struct NumberAsk { string mod, text; int32_t token, most; };
	std::vector<NumberAsk> numberAsks;

	const FakePlanet *FindPlanet(const char *name)
	{
		for(const FakePlanet &planet : planets)
			if(planet.name == name)
				return &planet;
		return nullptr;
	}
	vector<const FakePlanet *> PlanetsIn(const char *system)
	{
		vector<const FakePlanet *> out;
		for(const FakePlanet &planet : planets)
			if(planet.system == system)
				out.push_back(&planet);
		return out;
	}

	FleetShip *FindFleetShip(const char *uuid)
	{
		for(FleetShip &ship : fleet)
			if(inHook && ship.uuid == uuid)
				return &ship;
		return nullptr;
	}
	const FakeOutfit *FindOutfit(const char *name)
	{
		auto it = outfitDefs.find(name);
		return it == outfitDefs.end() ? nullptr : &it->second;
	}
	int Count(const std::map<string, int> &counts, const string &name)
	{
		auto it = counts.find(name);
		return it == counts.end() ? 0 : it->second;
	}

	FakeSystem *FindSystem(const string &name)
	{
		for(FakeSystem &system : galaxy)
			if(system.name == name)
				return &system;
		return nullptr;
	}

	bool IsGovernment(const string &name) { return reputations.count(name) != 0; }

	// Words of one data-file line: bare tokens, or "quoted" ones.
	vector<string> Tokens(const string &line)
	{
		vector<string> out;
		size_t i = 0;
		while(i < line.size())
		{
			if(line[i] == ' ' || line[i] == '\t')
				++i;
			else if(line[i] == '"')
			{
				size_t end = line.find('"', i + 1);
				if(end == string::npos)
					end = line.size();
				out.push_back(line.substr(i + 1, end - i - 1));
				i = end + 1;
			}
			else
			{
				size_t end = line.find_first_of(" \t", i);
				if(end == string::npos)
					end = line.size();
				out.push_back(line.substr(i, end - i));
				i = end;
			}
		}
		return out;
	}

	// Apply an event body: `system "X"` nodes with `government`, `link` and `unlink` children.
	bool ApplyChange(const string &text)
	{
		bool applied = false;
		FakeSystem *system = nullptr;
		size_t start = 0;
		while(start <= text.size())
		{
			size_t end = text.find('\n', start);
			if(end == string::npos)
				end = text.size();
			string line = text.substr(start, end - start);
			start = end + 1;
			vector<string> tokens = Tokens(line);
			if(tokens.empty())
				continue;
			bool child = line[0] == '\t' || line[0] == ' ';
			if(!child)
			{
				system = (tokens[0] == "system" && tokens.size() == 2) ? FindSystem(tokens[1]) : nullptr;
				continue;
			}
			if(!system || tokens.size() != 2)
				continue;
			if(tokens[0] == "government" && IsGovernment(tokens[1]))
			{
				system->government = tokens[1];
				applied = true;
			}
			else if((tokens[0] == "link" || tokens[0] == "unlink") && FindSystem(tokens[1]))
			{
				FakeSystem *other = FindSystem(tokens[1]);
				for(auto [a, b] : {std::make_pair(system, other), std::make_pair(other, system)})
				{
					auto it = std::find(a->links.begin(), a->links.end(), b->name);
					if(tokens[0] == "link" && it == a->links.end())
						a->links.push_back(b->name);
					else if(tokens[0] == "unlink" && it != a->links.end())
						a->links.erase(it);
				}
				applied = true;
			}
		}
		return applied;
	}

	void Check(bool ok, const string &what)
	{
		std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.c_str());
		if(!ok)
			++failures;
	}
}

extern "C" {
void es_log(const char *text) { std::printf("[log] %s\n", text); }
void es_message(const char *text) { if(inHook) messages.emplace_back(text); }
int32_t es_step(void) { return inHook ? step : 0; }
const char *es_system_name(void)
{
	const FakeSystem *system = inHook ? FindSystem(here) : nullptr;
	return system ? system->display.c_str() : "";
}
int32_t es_ship_count(void) { return inHook && flying ? static_cast<int32_t>(ships.size()) : 0; }
int32_t es_flagship(void) { return inHook && flying ? 0 : -1; }
const char *es_ship_name(int32_t i) { return Valid(i) ? ships[i].name.c_str() : ""; }
const char *es_ship_model(int32_t i) { return Valid(i) ? ships[i].model.c_str() : ""; }
int32_t es_ship_is_player(int32_t i) { return Valid(i) && ships[i].player; }
int32_t es_ship_is_disabled(int32_t i) { return Valid(i) && ships[i].disabled; }
double es_ship_hull(int32_t i) { return Valid(i) ? ships[i].hull : 0.; }
double es_ship_max_hull(int32_t i) { return Valid(i) ? ships[i].maxHull : 0.; }
double es_ship_shields(int32_t i) { return Valid(i) ? ships[i].shields : 0.; }
double es_ship_max_shields(int32_t i) { return Valid(i) ? ships[i].maxShields : 0.; }
double es_ship_energy(int32_t i) { return Valid(i) ? 100. : 0.; }
double es_ship_fuel(int32_t i) { return Valid(i) ? 300. : 0.; }
void es_ship_set_hull(int32_t i, double v) { if(Valid(i)) ships[i].hull = std::clamp(v, 0., ships[i].maxHull); }
void es_ship_set_shields(int32_t i, double v) { if(Valid(i)) ships[i].shields = std::clamp(v, 0., ships[i].maxShields); }
int64_t es_condition_get(const char *name) { return conditions[name]; }
void es_condition_set(const char *name, int64_t v) { if(inHook) conditions[name] = v; }

// ---- Mod API 2 ----

int32_t es_day(void) { return inHook ? day : 0; }
const char *es_here(void) { return inHook ? here.c_str() : ""; }

int32_t es_system_count(void) { return inHook ? static_cast<int32_t>(galaxy.size()) : 0; }
const char *es_system_at(int32_t i)
{
	return inHook && i >= 0 && i < static_cast<int32_t>(galaxy.size()) ? galaxy[i].name.c_str() : "";
}
const char *es_system_government(const char *name)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system ? system->government.c_str() : "";
}
int32_t es_system_link_count(const char *name)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system ? static_cast<int32_t>(system->links.size()) : 0;
}
const char *es_system_link(const char *name, int32_t k)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system && k >= 0 && k < static_cast<int32_t>(system->links.size()) ? system->links[k].c_str() : "";
}
int32_t es_system_inhabited(const char *name)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system && system->inhabited;
}
int32_t es_system_defense_count(const char *name)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system ? static_cast<int32_t>(system->defense.size()) : 0;
}
const char *es_system_defense_fleet(const char *name, int32_t k)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system && k >= 0 && k < static_cast<int32_t>(system->defense.size()) ? system->defense[k].c_str() : "";
}

int32_t es_change(const char *text)
{
	if(!inHook)
		return 0;
	bool applied = ApplyChange(text);
	changes.push_back({text, applied});
	return applied;
}

double es_attitude(const char *government, const char *toward)
{
	auto it = attitudes.find({government, toward});
	return inHook && it != attitudes.end() ? it->second : 0.;
}
double es_reputation(const char *government)
{
	auto it = reputations.find(government);
	return inHook && it != reputations.end() ? it->second : 0.;
}
void es_set_reputation(const char *government, double value)
{
	if(inHook && IsGovernment(government))
		reputations[government] = value;
}
// The fake game has one plugin installed and enabled.
int32_t es_plugin_enabled(const char *name)
{
	return name && std::string(name) == "Fake Plugin";
}

double es_fleet_strength(const char *fleet)
{
	auto it = fleets.find(fleet);
	double sum = 0.;
	if(inHook && it != fleets.end())
		for(const auto &ship : it->second)
			sum += ship.second;
	return sum;
}

// Each fake ship has 1000 hull, 500 shields, 50 damage per second and speed 6.
double es_fleet_stat(const char *fleet, const char *stat)
{
	auto it = inHook && fleet && stat ? fleets.find(fleet) : fleets.end();
	if(it == fleets.end())
		return 0.;
	const string which = stat;
	double n = static_cast<double>(it->second.size());
	if(which == "ships")
		return n;
	if(which == "hull")
		return 1000. * n;
	if(which == "shields")
		return 500. * n;
	if(which == "dps")
		return 50. * n;
	if(which == "speed")
		return 6.;
	if(which == "cost")
		return es_fleet_strength(fleet);
	return 0.;
}
int32_t es_system_fleet_count(const char *name)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system ? static_cast<int32_t>(system->fleets.size()) : 0;
}
const char *es_system_fleet(const char *name, int32_t k)
{
	const FakeSystem *system = inHook ? FindSystem(name) : nullptr;
	return system && k >= 0 && k < static_cast<int32_t>(system->fleets.size()) ? system->fleets[k].c_str() : "";
}

int32_t es_spawn(const char *fleet, const char *government, const char *tag, int32_t arrive)
{
	auto it = fleets.find(fleet);
	if(!inHook || !flying || it == fleets.end() || !IsGovernment(government))
		return 0;
	int32_t n = 0;
	for(const auto &[model, strength] : it->second)
	{
		FakeShip ship{string(fleet) + " " + to_string(++n), model, false, false, 400, 400, 200, 200};
		ship.tag = tag;
		ship.government = government;
		ship.strength = strength;
		ship.arrived = arrive != 0;
		pending.push_back(ship);
	}
	return n;
}
const char *es_ship_tag(int32_t i) { return Valid(i) ? ships[i].tag.c_str() : ""; }
const char *es_ship_government(int32_t i) { return Valid(i) ? ships[i].government.c_str() : ""; }
double es_ship_strength(int32_t i) { return Valid(i) ? ships[i].strength : 0.; }
double es_tag_strength(const char *tag)
{
	double sum = 0.;
	if(inHook && *tag)
		for(const auto *list : {&ships, &pending})
			for(const FakeShip &ship : *list)
				if(ship.tag == tag && !ship.disabled)
					sum += ship.strength;
	return sum;
}
int32_t es_player_kills(const char *prefix)
{
	if(!inHook)
		return 0;
	string p = prefix;
	size_t &cursor = killCursor[p];
	int32_t n = 0;
	for(; cursor < killLog.size(); ++cursor)
		if(killLog[cursor].compare(0, p.size(), p) == 0)
			++n;
	return n;
}

void es_map_clear(const char *mod) { if(inHook) overlays[mod] = MapOverlay(); }
void es_map_line(const char *mod, const char *from, const char *to, double r, double g, double b)
{
	if(inHook && FindSystem(from) && FindSystem(to))
		overlays[mod].lines.push_back({from, to, r, g, b});
}
void es_map_marker(const char *mod, const char *system, double r, double g, double b, const char *label)
{
	if(inHook && FindSystem(system))
		overlays[mod].markers.push_back({system, r, g, b, label});
}

void es_map_detail(const char *mod, const char *system, const char *title, const char *text, double r, double g, double b)
{
	if(inHook && FindSystem(system))
		overlays[mod].details.push_back({system, title, text, r, g, b});
}

void es_planet_button(const char *mod, const char *label)
{
	if(!inHook)
		return;
	if(*label)
		planetButtons[mod] = label;
	else
		planetButtons.erase(mod);
}
void es_panel_open(const char *mod, const char *title, const char *text)
{
	if(!inHook || !playerAction)
		return;
	Panel &panel = panels[mod];
	panel = Panel();
	panel.open = true;
	panel.title = title;
	panel.text = text;
}
void es_panel_button(const char *mod, const char *label)
{
	auto it = panels.find(mod);
	if(inHook && it != panels.end() && it->second.open)
		it->second.buttons.emplace_back(label);
}
void es_panel_close(const char *mod)
{
	auto it = panels.find(mod);
	if(inHook && it != panels.end())
		it->second = Panel();
}

void es_hook_timing(const char *mod, const char *hook, int32_t begin)
{
	if(begin)
	{
		hookTimingBalanced = hookTimingBalanced && hooksOpen == 0;
		++hooksOpen;
		return;
	}
	hookTimingBalanced = hookTimingBalanced && hooksOpen == 1;
	--hooksOpen;
	++hookCalls[string(mod) + " " + hook];
}

// ---- Mod API 5 ----
void es_outfitter_button(const char *mod, const char *label)
{
	if(!inHook)
		return;
	if(*label)
		outfitterButtons[mod] = label;
	else
		outfitterButtons.erase(mod);
}
int32_t es_outfitter_ship_count(void) { return inHook && outfitterShips ? static_cast<int32_t>(outfitterShips->size()) : 0; }
const char *es_outfitter_ship(int32_t k)
{
	if(!inHook || !outfitterShips || k < 0 || k >= static_cast<int32_t>(outfitterShips->size()))
		return "";
	return (*outfitterShips)[k].c_str();
}
int32_t es_fleet_count(void) { return inHook ? static_cast<int32_t>(fleet.size()) : 0; }
const char *es_fleet_ship(int32_t k)
{
	return inHook && k >= 0 && k < static_cast<int32_t>(fleet.size()) ? fleet[k].uuid.c_str() : "";
}
const char *es_fleet_ship_name(const char *ship) { FleetShip *s = FindFleetShip(ship); return s ? s->name.c_str() : ""; }
const char *es_fleet_ship_model(const char *ship) { FleetShip *s = FindFleetShip(ship); return s ? s->model.c_str() : ""; }
int32_t es_ship_outfit_count(const char *ship, const char *outfit)
{
	FleetShip *s = FindFleetShip(ship);
	return s ? Count(s->outfits, outfit) : 0;
}
int32_t es_ship_integrated_count(const char *ship, const char *outfit)
{
	FleetShip *s = FindFleetShip(ship);
	return s ? Count(s->integrated, outfit) : 0;
}
int32_t es_ship_integrate(const char *ship, const char *outfit, double scale)
{
	FleetShip *s = FindFleetShip(ship);
	const FakeOutfit *o = FindOutfit(outfit);
	if(!s || !o || o->category == "Ammunition" || !(scale >= 0. && scale <= 1.)
			|| Count(s->outfits, outfit) <= Count(s->integrated, outfit))
		return 0;
	++s->integrated[outfit];
	// The game saves whole tons per outfit.
	s->massSaved += std::round(o->mass * (1. - scale));
	return 1;
}
const char *es_outfit_name(const char *outfit) { const FakeOutfit *o = FindOutfit(outfit); return o ? o->display.c_str() : ""; }
const char *es_outfit_category(const char *outfit) { const FakeOutfit *o = FindOutfit(outfit); return o ? o->category.c_str() : ""; }
double es_outfit_attribute(const char *outfit, const char *attribute)
{
	const FakeOutfit *o = FindOutfit(outfit);
	if(!o)
		return 0.;
	string name = attribute;
	if(name == "mass")
		return o->mass;
	if(name == "cost")
		return o->cost;
	auto it = o->attributes.find(name);
	return it == o->attributes.end() ? 0. : it->second;
}
int32_t es_outfit_is_weapon(const char *outfit) { const FakeOutfit *o = FindOutfit(outfit); return o && o->weapon; }
void es_confirm(const char *mod, const char *text, int32_t token)
{
	if(inHook && playerAction)
		confirms.push_back({mod, text, token});
}

// ---- Mod API 6 ----
const char *es_data(const char *mod, const char *key)
{
	if(!inHook)
		return "";
	auto it = modData[mod].find(key);
	return it == modData[mod].end() ? "" : it->second.c_str();
}
void es_set_data(const char *mod, const char *key, const char *text)
{
	if(!inHook || !*key)
		return;
	if(*text)
		modData[mod][key] = text;
	else
		modData[mod].erase(key);
}
const char *es_planet(void) { return inHook && !flying ? landedPlanet.c_str() : ""; }
const char *es_planet_name(const char *name) { const FakePlanet *p = FindPlanet(name); return p ? p->display.c_str() : ""; }
const char *es_planet_system(const char *name) { const FakePlanet *p = FindPlanet(name); return p ? p->system.c_str() : ""; }
const char *es_planet_government(const char *name) { const FakePlanet *p = FindPlanet(name); return p ? p->government.c_str() : ""; }
int32_t es_planet_count(const char *system) { return static_cast<int32_t>(PlanetsIn(system).size()); }
const char *es_planet_at(const char *system, int32_t k)
{
	vector<const FakePlanet *> in = PlanetsIn(system);
	return k >= 0 && k < static_cast<int32_t>(in.size()) ? in[k]->name.c_str() : "";
}
int32_t es_planet_service(const char *name, const char *service)
{
	const FakePlanet *p = FindPlanet(name);
	return p && string(service) == "bank" && p->bank;
}
void es_panel_columns(const char *mod, const char *headers)
{
	auto it = panels.find(mod);
	if(inHook && it != panels.end() && it->second.open)
	{
		it->second.columns = headers;
		it->second.rows.clear();
	}
}
void es_panel_row(const char *mod, const char *cells)
{
	auto it = panels.find(mod);
	if(inHook && it != panels.end() && it->second.open && !it->second.columns.empty())
		it->second.rows.emplace_back(cells);
}
void es_ask_number(const char *mod, const char *text, int32_t token, int32_t most)
{
	if(inHook && playerAction && most >= 1)
		numberAsks.push_back({mod, text, token, most});
}

// ---- Mod API 7 ----
void es_screen_open(const char *mod, const char *title, const char *text)
{
	if(!inHook || !playerAction)
		return;
	Panel &panel = panels[mod];
	panel = Panel();
	panel.open = true;
	panel.full = true;
	panel.title = title;
	panel.text = text;
}
void es_panel_select(const char *mod, int32_t row)
{
	auto it = panels.find(mod);
	if(inHook && it != panels.end() && it->second.open)
		it->second.selected = row;
}
}

namespace {
	const char *const PROBE = "api2_probe";

	// What the API 2 probe recorded under "test: api2 <key>" (0 when absent).
	int64_t Probe(const string &key)
	{
		auto it = conditions.find("test: api2 " + key);
		return it == conditions.end() ? 0 : it->second;
	}

	// One step in flight: last step's spawns join the snapshot, then the mods run.
	void Step()
	{
		ships.insert(ships.end(), pending.begin(), pending.end());
		pending.clear();
		inHook = true;
		esmod_step(step++);
		inHook = false;
	}

	void NewDay()
	{
		++day;
		inHook = true;
		esmod_day(day);
		inHook = false;
	}

	// Jump: the system's other ships stay behind, then the mods see the arrival.
	void Enter(const string &system)
	{
		if(system != here)
			ships.erase(std::remove_if(ships.begin(), ships.end(),
				[](const FakeShip &ship) { return !ship.player; }), ships.end());
		here = system;
		inHook = true;
		esmod_enter(system.c_str());
		inHook = false;
	}

	// The landed player clicks a mod's planet button, or one of its panel's buttons.
	void ClickPlanetButton(const char *mod)
	{
		inHook = playerAction = true;
		esmod_planet_button(mod);
		inHook = playerAction = false;
	}
	void ClickPanel(const char *mod, int32_t button)
	{
		inHook = playerAction = true;
		esmod_panel(mod, button);
		inHook = playerAction = false;
	}

	// The probe's one-line summary of a system, as it builds it from the API.
	string Describe(const FakeSystem &system)
	{
		string out = system.name + ":" + system.government + ":" + (system.inhabited ? "1" : "0") + ":";
		for(size_t k = 0; k < system.links.size(); ++k)
			out += (k ? "," : "") + system.links[k];
		out += ":";
		for(size_t k = 0; k < system.defense.size(); ++k)
			out += (k ? "," : "") + system.defense[k];
		return out;
	}

	int CountTag(const vector<FakeShip> &list, const string &tag, const string &government, bool arrived)
	{
		return static_cast<int>(std::count_if(list.begin(), list.end(), [&](const FakeShip &ship)
			{ return ship.tag == tag && ship.government == government && ship.arrived == arrived; }));
	}

	// Whether only the probe has map overlays, a planet button and panels.
	template <class Map>
	bool OnlyProbe(const Map &perMod) { return perMod.size() == 1 && perMod.count(PROBE); }

	bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

	// Mod API 2, through test/mods/api2_probe.
	void TestApi2()
	{
		// Outside a hook there is no world.
		Check(es_day() == 0 && es_here()[0] == 0 && es_system_count() == 0 && es_change("system \"Zeta\"\n\tgovernment \"Republic\"") == 0
			&& es_spawn("Pirate Raid", "Pirate", "x", 0) == 0 && es_tag_strength("x") == 0. && es_player_kills("") == 0,
			"API 2 calls outside a hook answer zeros and change nothing");

		// Idle until enabled: every new hook runs, and the probe touches nothing.
		const auto reputationsBefore = reputations;
		conditions["test: api2 phase"] = 1;
		NewDay();
		Enter("Alpha");
		ClickPlanetButton(PROBE);
		ClickPanel(PROBE, 0);
		Step();
		Step();
		bool quiet = std::none_of(conditions.begin(), conditions.end(), [](const auto &c)
			{ return c.first.rfind("test: api2 ", 0) == 0 && c.first != "test: api2 phase" && c.second; });
		Check(quiet && overlays.empty() && planetButtons.empty() && panels.empty() && changes.empty() && pending.empty()
			&& reputations == reputationsBefore, "the API 2 probe idles until \"test: api2\" is set");

		conditions["test: api2"] = 1;
		// The mods start in flight: on_enter for where the player is.
		Enter("Alpha");
		Check(Probe("entered Alpha") == 1 && Probe("here matches") == 1, "on_enter ran with the system's true name, which here() answers");
		Check(Probe("ambush Alpha") == 3 && CountTag(pending, "api2 ambush", "Pirate", true) == 3,
			"on_enter spawned a jumping-in fleet in flight (3 ships)");

		// Phase 1: queries, changes, spawning and overlays from on_step.
		string galaxyBefore;
		for(const FakeSystem &system : galaxy)
			galaxyBefore += Describe(system) + ";";
		ships.insert(ships.end(), pending.begin(), pending.end());
		pending.clear();
		const size_t shipsBefore = ships.size();
		Step();
		Check(Probe("phase done") == 1, "the probe ran phase 1");
		Check(Probe("day") == day, "day() answers the date");
		Check(Probe("here Alpha") == 1 && Probe("display Alpha Prime") == 1, "here() is the true name, system() the display name");
		Check(Probe("system count") == 6, "system_count() counts the galaxy");
		Check(Probe("galaxy " + galaxyBefore) == 1, "system_at, system_government, inhabited, link(_count), defense_(count|fleet) round-trip: "
			+ galaxyBefore);
		Check(Probe("out of range") == 1, "galaxy queries out of range or for unknown systems answer zeros");
		Check(Probe("attitude pirate republic") == -50, "attitude() answers -0.5");
		Check(Probe("reputation republic") == 500, "reputation() answers 5");
		Check(Near(reputations["Pirate"], 12.5) && Probe("reputation pirate") == 1250, "set_reputation() set 12.5 and reads back");
		Check(Probe("strength raid") == 300 && Probe("strength none") == 0, "fleet_strength() sums the fleet's ships; unknown fleets are 0");
		Check(Probe("stat raid ships") == 3 && Probe("stat raid hull") == 3000 && Probe("stat raid shields") == 1500
			&& Probe("stat raid dps") == 150 && Probe("stat raid speed") == 6 && Probe("stat raid cost") == 300
			&& Probe("stat none") == 0 && Probe("stat raid bogus") == 0, "fleet_stat() answers each stat; unknown fleets and stats are 0");
		Check(Probe("system fleets Delta: Pirate Raid,Merchant,") == 1 && Probe("system fleets Alpha: ") == 1
			&& Probe("system fleet out of range") == 1, "system_fleet_count() and system_fleet() list a system's own fleets");
		Check(Probe("change zeta") == 1 && FindSystem("Zeta")->government == "Republic" && Probe("zeta gov Republic") == 1,
			"change() applied a system's government, and system_government() sees it");
		Check(changes.size() == 2 && changes[0].text == "system \"Zeta\"\n\tgovernment \"Republic\"" && changes[0].applied
			&& !changes[1].applied && Probe("change nowhere") == 0, "change() recorded its raw text; an unknown system applies nothing");
		Check(Probe("spawn raid") == 3 && Probe("spawn trader") == 1 && Probe("spawn none") == 0,
			"spawn() answers the ships added (3, 1, and 0 for an unknown fleet)");
		Check(CountTag(pending, "api2 raid", "Pirate", false) == 3 && CountTag(pending, "api2 trader", "Merchant", true) == 1
			&& pending.size() == 4, "spawned ships carry their tag, government and arrival");
		Check(Probe("spawn ships now") == static_cast<int64_t>(shipsBefore), "spawned ships join ship_count() only at the next step");
		Check(Probe("tag strength spawned") == 300, "tag_strength() counts ships just spawned");
		const MapOverlay &map = overlays[PROBE];
		Check(map.lines.size() == 2 && map.lines[0].from == "Alpha" && map.lines[0].to == "Beta" && Near(map.lines[0].r, 1.)
			&& map.lines[1].to == "Delta" && Near(map.lines[1].g, .5), "map_line() recorded two lines after map_clear() dropped one");
		Check(map.markers.size() == 1 && map.markers[0].system == "Delta" && map.markers[0].label == "pirates"
			&& Near(map.markers[0].g, 1.), "map_marker() recorded a labelled marker");
		Check(map.details.size() == 1 && map.details[0].system == "Delta" && map.details[0].title == "Raiders"
			&& map.details[0].text == "3 ships\nbound for Alpha" && Near(map.details[0].r, .5),
			"map_detail() recorded a side-panel section after map_clear() dropped one; an unknown system adds none");
		Check(OnlyProbe(overlays), "map calls land under the calling mod's name");
		Check(planetButtons.size() == 1 && planetButtons[PROBE] == "Raid board", "planet_button() set the mod's button, under its name");
		Check(panels.empty(), "a panel cannot open from on_step");

		// Phase 2: the spawned ships in the snapshot.
		conditions["test: api2 phase"] = 2;
		Step();
		Check(Probe("seen raid") == 3 && Probe("seen raid pirate") == 3 && Probe("seen trader") == 1,
			"ship_tag() and ship_government() see the spawned ships");
		Check(Probe("seen raid strength") == 300, "ship_strength() of the raid sums to 300");
		Check(Probe("untagged") == 1, "ships the game made, and bad indices, have no tag or government");

		// The player disables the raid's flagship and the trader; something else disables a Hawk.
		vector<FakeShip *> raid, trader;
		for(FakeShip &ship : ships)
			if(ship.tag == "api2 raid")
				raid.push_back(&ship);
			else if(ship.tag == "api2 trader")
				trader.push_back(&ship);
		if(raid.size() == 3 && trader.size() == 1)
		{
			// The raid is a Bounder (120) and two Hawks (90 each).
			for(FakeShip *ship : {raid[0], trader[0]})
			{
				ship->disabled = true;
				killLog.push_back(ship->tag);
			}
			raid[1]->disabled = true;
		}
		conditions["test: api2 phase"] = 3;
		Step();
		Check(Probe("tag strength after") == 90, "tag_strength() leaves out disabled ships (" + to_string(Probe("tag strength after"))
			+ ", expected 90)");
		Check(Probe("kills api2") == 2 && Probe("kills api2 again") == 0, "player_kills() counts by prefix and resets each call");
		Check(Probe("kills raid") == 1, "player_kills() keeps a separate count per prefix");

		// A jump: a new day, then the arrival.
		NewDay();
		Check(Probe("days seen") == 1 && Probe("last day") == day && Probe("day matches") == 1, "on_day ran with the new day");
		Enter("Beta");
		Check(Probe("entered Beta") == 1 && Probe("ambush Beta") == 3 && CountTag(pending, "api2 ambush", "Pirate", true) == 3,
			"on_enter ran for the jump and spawned there");

		// Landed: the planet button and the panel.
		flying = false;
		pending.clear();
		ClickPlanetButton("hull_regen");
		Check(Probe("planet clicks") == 0 && panels.empty(), "another mod's planet button does not reach the probe");
		ClickPlanetButton(PROBE);
		Check(Probe("planet clicks") == 1, "on_planet_button ran");
		Check(Probe("landed spawn") == 0 && pending.empty(), "spawn() refuses while landed");
		Check(Probe("landed reputation") == 1250, "reputation() works while landed");
		Panel &panel = panels[PROBE];
		Check(panels.size() == 1 && panel.open && panel.title == "Raid board" && panel.text == "Choose a target."
			&& panel.buttons == vector<string>{"Cancel", "Raid Delta"}, "panel_open() and panel_button() opened the mod's panel");
		ClickPanel(PROBE, 1);
		Check(Probe("panel clicks") == 1 && Probe("panel button") == 1, "on_panel ran with the clicked button's index");
		Check(panel.open && panel.title == "Raid planned" && panel.buttons == vector<string>{"Close"},
			"opening an open panel replaced its contents");
		Check(planetButtons.empty(), "planet_button(\"\") removed the button");
		ClickPanel(PROBE, 0);
		Check(Probe("panel clicks") == 2 && Probe("panel button") == 0 && !panel.open, "panel_close() closed the panel");
		Check(OnlyProbe(panels), "panel calls land under the calling mod's name");

		// Take off: another day.
		flying = true;
		NewDay();
		Check(Probe("days seen") == 2 && Probe("last day") == day, "on_day ran again at take-off");
	}

	const char *const PROBE6 = "api6_probe";

	int64_t Probe6(const string &key)
	{
		auto it = conditions.find("test: api6 " + key);
		return it == conditions.end() ? 0 : it->second;
	}

	// Mod API 6, through test/mods/api6_probe.
	void TestApi6()
	{
		Check(es_data("api6_probe", "state")[0] == 0 && es_planet()[0] == 0, "API 6 calls outside a hook answer nothing");
		conditions["test: api6"] = 1;
		flying = false;
		Enter("Alpha");
		Check(Probe6("data round trip") == 1, "set_data() and data() keep any text: quotes, backticks, percent signs, tabs, line breaks");
		Check(Probe6("data removed") == 1 && !modData["api6_probe"].count("gone"), "set_data(key, \"\") removes a key; a missing key reads \"\"");
		Check(modData.size() >= 1 && modData["api6_probe"].count("state") && !modData.count("api5_probe"),
			"stored text lands under the calling mod's name");
		Check(Probe6("landed on Alpha I") == 1, "planet() is the landed planet's true name");
		Check(Probe6("planets in Alpha 2") == 1 && Probe6("planet Alpha I Alpha Prime I Alpha Republic") == 1
			&& Probe6("planet Alpha Station Alpha Station Alpha Republic") == 1,
			"planet_count, planet_at, planet_name, planet_system and planet_government");
		Check(Probe6("bank Alpha I") == 1 && Probe6("bank Alpha Station") == 0, "planet_service(planet, \"bank\")");
		Check(Probe6("planet out of range") == 1, "a planet out of range or unknown answers \"\"");

		ClickPlanetButton(PROBE6);
		Panel &panel = panels[PROBE6];
		Check(panel.open && panel.columns == "Company\tPrice\tChange" && panel.rows == vector<string>{"Acme\t12.50\t+3%", "Boreal\t8.00\t-1%"}
			&& panel.buttons == vector<string>{"Close"}, "panel_columns() and panel_row() give the panel a table");
		inHook = playerAction = true;
		esmod_panel_row(PROBE6, 1);
		inHook = playerAction = false;
		Check(Probe6("row") == 2, "on_panel_row ran with the clicked row's index");
		Check(numberAsks.size() == 1 && numberAsks[0].mod == PROBE6 && numberAsks[0].token == 7 && numberAsks[0].most == 500
			&& numberAsks[0].text == "How many shares?", "ask_number() asked under the mod's name");
		inHook = playerAction = true;
		esmod_number(PROBE6, numberAsks[0].token, 42);
		inHook = playerAction = false;
		Check(Probe6("number token") == 7 && Probe6("number value") == 42, "on_number ran with the token and the number");
		flying = true;
	}

	// Mod API 7, through test/mods/api7_probe.
	void TestApi7()
	{
		const char *const PROBE7 = "api7_probe";
		conditions["test: api7"] = 1;
		flying = false;
		ClickPlanetButton(PROBE7);
		Panel &panel = panels[PROBE7];
		Check(panel.open && panel.full && panel.title == "Board" && panel.text == "Side text", "screen_open() opens the mod's panel on a full screen");
		Check(panel.columns == "Name\tValue" && panel.rows.size() == 2 && panel.selected == 1 && panel.buttons == vector<string>{"Leave"},
			"the screen holds a table, a chosen row (panel_select) and buttons");
		inHook = playerAction = true;
		esmod_panel_header(PROBE7, 1);
		inHook = playerAction = false;
		Check(conditions["test: api7 header"] == 2, "on_panel_header ran with the clicked column");
		inHook = true;
		es_screen_open(PROBE7, "Sneaky", "");
		inHook = false;
		Check(panel.title == "Board", "a screen opens only from a hook the player started");
		flying = true;
	}

	const char *const PROBE5 = "api5_probe";

	int64_t Probe5(const string &key)
	{
		auto it = conditions.find("test: api5 " + key);
		return it == conditions.end() ? 0 : it->second;
	}

	// The outfitter asks whether a mod's button is enabled, and the player clicks it.
	bool CanOutfitter(const char *mod, const string &outfit, const vector<string> &selected)
	{
		inHook = true;
		outfitterShips = &selected;
		bool active = esmod_can_outfitter(mod, outfit.c_str()) != 0;
		outfitterShips = nullptr;
		inHook = false;
		return active;
	}
	void ClickOutfitter(const char *mod, const string &outfit, const vector<string> &selected)
	{
		inHook = playerAction = true;
		outfitterShips = &selected;
		esmod_outfitter(mod, outfit.c_str());
		outfitterShips = nullptr;
		inHook = playerAction = false;
	}
	// The player clicks OK on the last dialog: the game runs on_confirm with the outfitter's ships.
	void ConfirmLast(const vector<string> &selected)
	{
		if(confirms.empty())
			return;
		Confirm last = confirms.back();
		confirms.pop_back();
		inHook = playerAction = true;
		outfitterShips = &selected;
		esmod_confirm(last.mod.c_str(), last.token);
		outfitterShips = nullptr;
		inHook = playerAction = false;
	}

	// Mod API 5, through test/mods/api5_probe.
	void TestApi5()
	{
		Check(es_fleet_count() == 0 && es_fleet_ship(0)[0] == 0 && es_ship_integrate("uuid-a", "Laser", .75) == 0,
			"API 5 calls outside a hook answer zeros and change nothing");
		conditions["test: api5"] = 1;
		flying = false;
		Enter("Alpha");
		Check(outfitterButtons.size() == 1 && outfitterButtons[PROBE5] == "Integrate",
			"outfitter_button() set the mod's button, under its name");
		Check(Probe5("fleet 2") == 1 && Probe5("ship 0 uuid-a Buggy Barge Star Barge") == 1
			&& Probe5("ship 1 uuid-b Full Barge Star Barge") == 1 && Probe5("fleet out of range") == 1,
			"fleet_count(), fleet_ship(), fleet_ship_name() and fleet_ship_model() list the player's ships by UUID");
		Check(Probe5("laser Laser Cannon Guns") == 1 && Probe5("laser mass") == 10 && Probe5("laser cost") == 1000
			&& Probe5("battery energy") == 1000, "outfit_name(), outfit_category() and outfit_attribute() (mass, cost, attributes)");
		Check(Probe5("laser weapon") == 1 && Probe5("battery weapon") == 0 && Probe5("unknown outfit") == 1,
			"outfit_is_weapon(), and an unknown outfit answers zeros");
		Check(Probe5("no outfitter ships") == 1 && confirms.empty(),
			"outside the outfitter there are no selected ships, and confirm() asks nothing");

		const vector<string> selected = {"uuid-a", "uuid-b"};
		Check(!CanOutfitter(PROBE5, "", selected) && Probe5("selected 2") == 1, "can_outfitter() sees the selected ships; no outfit, no button");
		Check(!CanOutfitter(PROBE5, "Battery", selected), "can_outfitter() is false for an outfit the ship in focus lacks");
		Check(CanOutfitter(PROBE5, "Laser", selected), "can_outfitter() is true for an installed outfit");
		ClickOutfitter(PROBE5, "Laser", selected);
		Check(Probe5("clicked Laser") == 1 && confirms.size() == 1 && confirms[0].mod == PROBE5 && confirms[0].token == 7
			&& confirms[0].text == "Integrate the Laser Cannon?", "on_outfitter ran, and confirm() asked under the mod's name");
		ConfirmLast(selected);
		const FleetShip &a = fleet[0];
		Check(Probe5("confirmed") == 7 && Probe5("integrated Laser") == 1 && Probe5("integrated count Laser") == 1
			&& Count(a.integrated, "Laser") == 1 && Near(a.massSaved, 3.), "on_confirm ran with the token, and ship_integrate() integrated one");
		Check(Probe5("ammo refused") == 1 && Probe5("bad scale refused") == 1,
			"ship_integrate() refuses ammunition and a scale outside 0..1");
		Check(CanOutfitter(PROBE5, "Laser", selected), "one Laser is still not integrated");
		ClickOutfitter(PROBE5, "Laser", selected);
		ConfirmLast(selected);
		Check(Count(a.integrated, "Laser") == 2 && !CanOutfitter(PROBE5, "Laser", selected),
			"with every Laser integrated, the button is off");
		inHook = true;
		Check(es_ship_integrate("uuid-a", "Laser", .75) == 0, "an outfit cannot be integrated more times than it is installed");
		inHook = false;
		Check(Probe5("clicked Battery") == 0, "the probe saw only its own clicks");
		flying = true;
	}
}

int main(int argc, char *argv[])
{
	if(argc != 2)
	{
		std::fprintf(stderr, "usage: fake_game <mods dir>\n");
		return 2;
	}
	Check(esmod_init() == 0, "the harness starts");

	// Load mods the way the game does: every .js in the folder, sorted, by path without ".js".
	std::vector<std::filesystem::path> files;
	for(const auto &entry : std::filesystem::directory_iterator(argv[1]))
		if(entry.path().extension() == ".js")
			files.push_back(entry.path());
	std::sort(files.begin(), files.end());
	inHook = true;
	for(const auto &file : files)
	{
		std::filesystem::path stem = file;
		stem.replace_extension();
		esmod_load(stem.string().c_str(), file.stem().string().c_str());
	}
	Check(esmod_load((std::filesystem::path(argv[1]) / "no_such_mod").string().c_str(), "no_such_mod") != 0,
		"a missing mod is refused");
	// Two test mods must be refused: one does not extend es.Mod, one claims another API version.
	Check(esmod_count() == static_cast<int32_t>(files.size()) - 2, "every mod loaded but the two that must be refused ("
		+ to_string(esmod_count()) + " of " + to_string(files.size()) + ")");

	// Idle until enabled: the example mod must not touch a game that did not ask for it.
	for(step = 0; step < 10; ++step)
		esmod_step(step);
	inHook = false;
	Check(ships[0].hull == 500 && conditions["dado example: steps"] == 0, "the example mod idles until enabled");

	conditions["dado example: enabled"] = 1;
	inHook = true;
	for(step = 0; step <= 180; ++step)
		esmod_step(step);
	inHook = false;

	// Steps 0, 60, 120 and 180 each repair 2% of 1000 on the one damaged, working player ship.
	Check(std::fabs(ships[0].hull - 580) < 1e-9, "the damaged player ship regenerated (hull " +
		std::to_string(ships[0].hull) + ", expected 580)");
	Check(ships[1].hull == 1000, "a ship at full hull is left alone");
	Check(ships[2].hull == 50, "a disabled ship is left alone");
	Check(ships[3].hull == 100, "a ship that is not the player's is left alone");
	Check(conditions["dado example: steps"] == 181, "the mod counted 181 steps (counted " +
		std::to_string(conditions["dado example: steps"]) + ")");
	Check(conditions["dado example: repairs"] == 4, "the mod counted 4 repairs");
	Check(messages.size() == 1 && messages[0] == "Hull regeneration mod active.", "the mod told the player once");
	Check(conditions["test: throws ran"] == 6, "a mod that throws ran until it threw, then was switched off");
	Check(conditions["test: bad indices ok"] == 181, "out-of-range ship indices answer zeros, every step");
	Check(conditions["test: not a mod ran"] == 0, "a class that does not extend es.Mod never ran");
	Check(conditions["test: wrong api ran"] == 0, "a mod built for another API version never ran");

	TestApi2();
	TestApi5();
	TestApi6();
	TestApi7();

	Check(hookTimingBalanced && hooksOpen == 0, "the harness began and ended the timing of every hook, one at a time");
	Check(hookCalls["hull_regen on_load"] == 1 && hookCalls["hull_regen on_step"] > 180 && hookCalls["api5_probe can_outfitter"] >= 5
		&& hookCalls["api5_probe on_confirm"] == 2 && hookCalls["throws_on_step on_step"] > 0
		&& hookCalls["throws_on_step on_step"] < hookCalls["hull_regen on_step"],
		"hooks are timed under their mod's name, including one that throws and is switched off");

	esmod_step(999);
	esmod_shutdown();
	esmod_step(1000);
	Check(true, "steps after shutdown are ignored");

	std::printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
