/* ModHarness.cpp
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

#include "ModHarness.h"

#include "Ship.h"

#include <algorithm>

using namespace std;



void ModHarness::SetHull(Ship &ship, double value)
{
	ship.levels.hull = clamp(value, 0., ship.MaxHull());
}



void ModHarness::SetShields(Ship &ship, double value)
{
	ship.levels.shields = clamp(value, 0., ship.MaxShields());
}



#ifndef ES_DADO_MODS

bool ModHarness::IsBuiltIn()
{
	return false;
}



int64_t ModHarness::LoadedCount()
{
	return 0;
}



void ModHarness::Step(PlayerInfo &, const list<shared_ptr<Ship>> &, list<shared_ptr<Ship>> &,
	const list<ShipEvent> &, int)
{
}



void ModHarness::Shutdown()
{
}



void ModHarness::Landed(PlayerInfo &)
{
}



void ModHarness::ShipsReplaced()
{
}



void ModHarness::DrawMap(const PlayerInfo &, const Point &, double)
{
}



void ModHarness::DrawMapDetails(const System *, double)
{
}



string ModHarness::PlanetButtonLabel()
{
	return string();
}



void ModHarness::PlanetButton(PlayerInfo &, UI &)
{
}



shared_ptr<Panel> ModHarness::NewPlanetPanel(PlayerInfo &)
{
	return nullptr;
}



bool ModHarness::PanelOpen()
{
	return false;
}



void ModHarness::ClosePanel()
{
}



vector<string> ModHarness::OutfitterButtons()
{
	return {};
}



bool ModHarness::OutfitterButtonActive(size_t, PlayerInfo &, const vector<Ship *> &, const Outfit *)
{
	return false;
}



void ModHarness::OutfitterButton(size_t, PlayerInfo &, UI &, const vector<Ship *> &, const Outfit *)
{
}

#else

#include "es_host.h"
// dadoc writes esmod.h without a C++ guard.
extern "C" {
#include "esmod.h"
}

#include "Color.h"
#include "DataFile.h"
#include "DataNode.h"
#include "Date.h"
#include "DialogPanel.h"
#include "EsUuid.h"
#include "shader/FillShader.h"
#include "Fleet.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "GameData.h"
#include "GameEvent.h"
#include "Government.h"
#include "Hardpoint.h"
#include "Interface.h"
#include "shader/LineShader.h"
#include "Logger.h"
#include "Message.h"
#include "Messages.h"
#include "Outfit.h"
#include "Panel.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Port.h"
#include "Plugin.h"
#include "PluginManager.h"
#include "Point.h"
#include "Rectangle.h"
#include "shader/RingShader.h"
#include "Screen.h"
#include "ShipEvent.h"
#include "StellarObject.h"
#include "System.h"
#include "TextArea.h"
#include "UI.h"
#include "Variant.h"
#include "Weapon.h"
#include "text/DisplayText.h"
#include "text/Layout.h"
#include "text/Truncate.h"
#include "text/WrappedText.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>
#include <vector>

namespace {
	// Lookups cached for one run of hooks: one engine step (every mod's hooks in
	// it), or one hook the UI ran. The world does not change under a run except
	// through the mods' own calls, and the calls that change what a cache holds
	// clear it (see "Changing the API" in modding/README.md).
	struct Caches {
		// The player's ships that are not destroyed, in fleet order, and by UUID.
		bool fleetBuilt = false;
		vector<Ship *> fleet;
		map<string, Ship *, less<>> fleetByUuid;
		// Each system's defense fleets.
		map<const System *, vector<const Fleet *>> defense;
		// The strength of the tagged ships that can still fight here, by tag.
		bool tagsBuilt = false;
		map<string, double, less<>> tagStrength;
		// fleet_stat answers.
		map<pair<const Fleet *, string>, double> fleetStats;
	};

	// The world a hook may see. It exists only while a hook runs.
	struct Context {
		PlayerInfo *player = nullptr;
		vector<Ship *> ships;
		int flagship = -1;
		int step = 0;
		// Set in flight: where spawned ships go.
		list<shared_ptr<Ship>> *newShips = nullptr;
		const list<shared_ptr<Ship>> *allShips = nullptr;
		// Set in a hook the player started from the UI: where panels open.
		UI *ui = nullptr;
		// Set while the player lands: on_enter may open a panel, which the planet screen shows.
		bool landing = false;
		// Set in the outfitter's hooks: its selected ships, the one in focus first.
		const vector<Ship *> *outfitterShips = nullptr;
		Caches cache;
	};
	Context *context = nullptr;

	enum class State { UNSTARTED, RUNNING, FAILED };
	State state = State::UNSTARTED;
	// Read by condition getters, which may run on another thread than the engine's.
	atomic<int64_t> loadedCount = 0;

	// What the step hook last saw, to tell when a day passes or the player arrives.
	int lastDay = 0;
	const System *lastSystem = nullptr;

	// Ships the mods spawned, and their tags.
	struct Tagged {
		weak_ptr<Ship> ship;
		string tag;
		bool credited = false;
	};
	map<const Ship *, Tagged> tagged;
	// Ships the player disabled or destroyed, by tag, not yet counted by a mod.
	map<string, int> kills;

	// The galaxy as mods index it: valid, accessible systems in name order. Built
	// when first asked for, and again after events may have changed it.
	vector<const System *> galaxy;
	map<const Fleet *, string> fleetNames;

	// Events change the galaxy and its fleets: on a new day, on landing and
	// taking off, on loading a pilot, and through es_change.
	void ForgetWorld()
	{
		galaxy.clear();
		fleetNames.clear();
	}

	// How long each mod's hooks take, by mod and hook. A call slower than
	// SLOW_HOOK_MS is logged, then again only when one is twice as slow as the
	// last logged, and Shutdown logs a summary.
	constexpr double SLOW_HOOK_MS = 2.;
	struct HookTiming {
		int64_t calls = 0;
		double totalMs = 0.;
		double maxMs = 0.;
		double reportedMs = 0.;
	};
	map<pair<string, string>, HookTiming> timings;
	chrono::steady_clock::time_point hookStart;
	// The mod whose hook is running ("" between hooks).
	string currentMod;

	// The frame budget: the engine steps 60 times a second.
	constexpr double FRAME_MS = 1000. / 60.;
	// Each mod's share of the engine steps: time in its hooks during Step, which
	// is time the frame waits for. Logged every FRAME_REPORT_STEPS in dev builds.
	constexpr int64_t FRAME_REPORT_STEPS = 3600;
	struct FrameShare {
		double totalMs = 0.;
		double worstMs = 0.;
		double windowMs = 0.;
		double windowWorstMs = 0.;
	};
	map<string, FrameShare> frameShares;
	int64_t frameSteps = 0;
	int64_t windowSteps = 0;
	// While Step runs its hooks: each mod's time in this step.
	bool inStep = false;
	map<string, double> stepMs;

	// "12.3%": a share of the frame.
	string FramePercent(double ms)
	{
		char text[32];
		snprintf(text, sizeof(text), "%.2f%%", 100. * ms / FRAME_MS);
		return text;
	}

	// Fold one step's hook times into each mod's share, and in dev builds log
	// the last minute's shares.
	void EndStepTiming()
	{
		++frameSteps;
		++windowSteps;
		for(const auto &[mod, ms] : stepMs)
		{
			FrameShare &share = frameShares[mod];
			share.totalMs += ms;
			share.worstMs = max(share.worstMs, ms);
			share.windowMs += ms;
			share.windowWorstMs = max(share.windowWorstMs, ms);
		}
		stepMs.clear();
#ifndef NDEBUG
		if(windowSteps < FRAME_REPORT_STEPS)
			return;
		string line = "Dado mods, share of each frame over the last " + to_string(windowSteps / 60) + " s (average, worst step):";
		for(auto &[mod, share] : frameShares)
		{
			line += "\n\t" + mod + ": " + FramePercent(share.windowMs / windowSteps) + ", " + FramePercent(share.windowWorstMs);
			share.windowMs = 0.;
			share.windowWorstMs = 0.;
		}
		Logger::Log(line, Logger::Level::INFO);
		windowSteps = 0;
#endif
	}

#ifndef NDEBUG
	// Dev builds: time every call a mod makes into the game, by mod and call.
	struct CallStat {
		int64_t calls = 0;
		double ms = 0.;
	};
	map<pair<string, const char *>, CallStat> callStats;
	class CallTimer {
	public:
		explicit CallTimer(const char *name) : name(name), start(chrono::steady_clock::now()) {}
		~CallTimer()
		{
			CallStat &stat = callStats[make_pair(currentMod, name)];
			++stat.calls;
			stat.ms += chrono::duration<double, milli>(chrono::steady_clock::now() - start).count();
		}
	private:
		const char *name;
		chrono::steady_clock::time_point start;
	};
#define ES_CALL_TIMER CallTimer callTimer(__func__)
#else
#define ES_CALL_TIMER
#endif

	// Map overlays, by mod.
	struct Overlay {
		const System *from = nullptr;
		const System *to = nullptr;
		Color color;
		string label;
	};
	map<string, vector<Overlay>> overlays;

	// Sections of the map's side panel, by mod (cleared with the overlays).
	struct Detail {
		const System *system = nullptr;
		string title;
		string text;
		Color color;
	};
	map<string, vector<Detail>> details;

	// Planet screen buttons, by mod.
	map<string, string> planetButtons;
	// Outfitter buttons, by mod (mod API 5).
	map<string, string> outfitterButtons;

	// A string handed back to the harness lives until the next one: the
	// harness copies it into the script at once.
	string returned;
	const char *Return(string value)
	{
		returned = std::move(value);
		return returned.c_str();
	}

	// The player's ships that are not destroyed, in fleet order, listed once per
	// run of hooks. Empty outside one.
	const vector<Ship *> &PlayerFleet()
	{
		static const vector<Ship *> none;
		if(!context)
			return none;
		Caches &cache = context->cache;
		if(!cache.fleetBuilt)
		{
			for(const shared_ptr<Ship> &ship : context->player->Ships())
				if(ship && !ship->IsDestroyed())
				{
					cache.fleet.push_back(ship.get());
					cache.fleetByUuid.emplace(ship->UUID().ToString(), ship.get());
				}
			cache.fleetBuilt = true;
		}
		return cache.fleet;
	}

	// One of the player's ships, by its UUID.
	Ship *FindPlayerShip(const char *uuid)
	{
		if(!context || !uuid || !*uuid)
			return nullptr;
		PlayerFleet();
		const auto &byUuid = context->cache.fleetByUuid;
		auto it = byUuid.find(string_view(uuid));
		return it == byUuid.end() ? nullptr : it->second;
	}

	const Outfit *FindOutfit(const char *name)
	{
		const Outfit *outfit = name ? GameData::Outfits().Find(name) : nullptr;
		return outfit && outfit->IsDefined() ? outfit : nullptr;
	}

	// The one mod panel, and what it shows.
	class ModPanel;
	struct PanelState {
		string mod;
		string title;
		string text;
		vector<string> buttons;
		// A chooser among several mods' planet buttons, run by the harness itself.
		bool chooser = false;
		// Whether it is open: the planet screen shows it while it is.
		bool open = false;
		// A table under the text (mod API 6): its column headings and its rows.
		vector<string> columns = {};
		vector<vector<string>> rows = {};
		// Mod API 7: shown on its own full screen, and the row shown as chosen (-1 for none).
		bool full = false;
		int selected = -1;
	};

	// "a\tb\tc" as its cells.
	vector<string> SplitCells(const char *text)
	{
		vector<string> cells;
		string cell;
		for(const char *c = text ? text : ""; *c; ++c)
			if(*c == '\t')
			{
				cells.push_back(cell);
				cell.clear();
			}
			else
				cell += *c;
		cells.push_back(cell);
		return cells;
	}
	PanelState panel;
	// The newest planet screen's mod panel.
	const Panel *currentView = nullptr;

	Ship *GetShip(int32_t i)
	{
		if(!context || i < 0 || i >= static_cast<int32_t>(context->ships.size()))
			return nullptr;
		return context->ships[i];
	}

	const System *FindSystem(const char *name)
	{
		return name ? GameData::Systems().Find(name) : nullptr;
	}

	const Government *FindGovernment(const char *name)
	{
		return name ? GameData::Governments().Find(name) : nullptr;
	}

	const vector<const System *> &GalaxySystems()
	{
		if(galaxy.empty())
			for(const auto &it : GameData::Systems())
				if(it.second.IsValid() && !it.second.Inaccessible())
					galaxy.push_back(&it.second);
		return galaxy;
	}

	vector<const Fleet *> FindDefenseFleets(const System *system)
	{
		vector<const Fleet *> fleets;
		if(system)
			for(const StellarObject &object : system->Objects())
				if(object.HasValidPlanet())
					for(const Fleet *fleet : object.GetPlanet()->DefenseFleets())
						if(fleet)
							fleets.push_back(fleet);
		return fleets;
	}

	// A system's defense fleets, found once per run of hooks.
	const vector<const Fleet *> &DefenseFleets(const System *system)
	{
		static vector<const Fleet *> uncached;
		if(!context)
		{
			uncached = FindDefenseFleets(system);
			return uncached;
		}
		auto it = context->cache.defense.find(system);
		if(it == context->cache.defense.end())
			it = context->cache.defense.emplace(system, FindDefenseFleets(system)).first;
		return it->second;
	}

	const string &FleetName(const Fleet *fleet)
	{
		if(fleetNames.empty())
			for(const auto &it : GameData::Fleets())
				fleetNames[&it.second] = it.first;
		static const string none;
		auto it = fleetNames.find(fleet);
		return it == fleetNames.end() ? none : it->second;
	}

	const string *TagOf(const Ship *ship)
	{
		auto it = tagged.find(ship);
		return it == tagged.end() ? nullptr : &it->second.tag;
	}

	void Tag(const shared_ptr<Ship> &ship, const string &tag)
	{
		tagged[ship.get()] = Tagged{ship, tag, false};
	}

	// Forget ships that no longer exist.
	void PruneTags()
	{
		for(auto it = tagged.begin(); it != tagged.end(); )
			if(it->second.ship.expired())
				it = tagged.erase(it);
			else
				++it;
	}

	// Credit the player for tagged ships they disabled or destroyed.
	void CountKills(const list<ShipEvent> &events)
	{
		const Government *player = GameData::PlayerGovernment();
		for(const ShipEvent &event : events)
		{
			if(!(event.Type() & (ShipEvent::DISABLE | ShipEvent::DESTROY)) || event.ActorGovernment() != player)
				continue;
			auto it = tagged.find(event.Target().get());
			if(it == tagged.end() || it->second.credited)
				continue;
			it->second.credited = true;
			++kills[it->second.tag];
		}
	}

	void Start(PlayerInfo &player)
	{
		if(int code = esmod_init())
		{
			Logger::Log("Dado mod harness did not start (code " + to_string(code) + ").", Logger::Level::ERROR);
			state = State::FAILED;
			return;
		}
		state = State::RUNNING;
		lastDay = player.GetDate().DaysSinceEpoch();
		lastSystem = nullptr;

		// Every resource and plugin folder may hold mods in its "dado" folder, one .js file each.
		// A mod is named by its file; as with data, a later source's mod replaces an earlier one's.
		map<string, filesystem::path> mods;
		for(const filesystem::path &source : GameData::Sources())
		{
			filesystem::path folder = source / "dado";
			error_code error;
			if(!filesystem::is_directory(folder, error))
				continue;
			for(const filesystem::directory_entry &entry : filesystem::directory_iterator(folder, error))
				if(entry.is_regular_file() && entry.path().extension() == ".js")
				{
					string name = entry.path().stem().string();
					auto it = mods.find(name);
					if(it != mods.end())
						Logger::Log("Dado mod \"" + name + "\" in " + folder.string() + " replaces the one in "
							+ it->second.parent_path().string() + ".", Logger::Level::INFO);
					mods[name] = entry.path();
				}
		}
		for(const auto &[name, file] : mods)
		{
			filesystem::path stem = file;
			stem.replace_extension();
			esmod_load(stem.string().c_str(), name.c_str());
		}
		int found = static_cast<int>(mods.size());
		loadedCount = esmod_count();
		Logger::Log("Dado mod harness: " + to_string(loadedCount.load()) + " of " + to_string(found) + " mod(s) loaded.",
			Logger::Level::INFO);
	}

	// Whether the days since the last step are days that passed in play (a jump
	// or a takeoff is one day) rather than another pilot or save: then the mods
	// run them. Otherwise the clock just moves, and the mods look around again.
	bool SyncDay(int today)
	{
		if(lastDay > 0 && today >= lastDay && today - lastDay <= 7)
			return true;
		lastDay = today;
		lastSystem = nullptr;
		return false;
	}

	// Run a hook from the UI, with a world but no ships in flight. With a UI, it
	// is a hook the player started, which may open panels and dialogs.
	template <class Hook>
	void RunUIHook(PlayerInfo &player, UI *ui, const vector<Ship *> *outfitterShips, Hook hook);

	template <class Hook>
	void RunUIHook(PlayerInfo &player, UI &ui, Hook hook)
	{
		RunUIHook(player, &ui, nullptr, hook);
	}

	template <class Hook>
	void RunUIHook(PlayerInfo &player, UI *ui, const vector<Ship *> *outfitterShips, Hook hook)
	{
		if(state == State::FAILED)
			return;
		Context current;
		current.player = &player;
		current.ui = ui;
		current.outfitterShips = outfitterShips;
		Context *outer = context;
		context = &current;
		if(state == State::UNSTARTED)
			Start(player);
		if(state == State::RUNNING)
			hook();
		context = outer;
	}

	// What both of a mod's views share: drawing the panel's table, its buttons,
	// and sending clicks and keys back to the mod.
	class ModView : public Panel {
	public:
		explicit ModView(PlayerInfo &player) : player(player) {}

	protected:
		// The table: a row of headings, then a scrolling list of rows the player
		// can click. Every column but the first is as wide as its widest cell and
		// aligned right; the first takes the rest and is cut short. With
		// `headings`, a click on a heading runs the mod's on_panel_header.
		void DrawTable(const Rectangle &area, bool headings)
		{
			const Font &font = FontSet::Get(14);
			const Color &bright = *GameData::Colors().Get("bright");
			const Color &medium = *GameData::Colors().Get("medium");
			const Color &dim = *GameData::Colors().Get("dim");
			const double ROW = 20.;
			const double GAP = 16.;
			const size_t columns = panel.columns.size();
			vector<double> widths(columns, 0.);
			for(size_t c = 0; c < columns; ++c)
				widths[c] = font.Width(panel.columns[c]);
			for(const vector<string> &row : panel.rows)
				for(size_t c = 0; c < columns && c < row.size(); ++c)
					widths[c] = max(widths[c], static_cast<double>(font.Width(row[c])));
			double others = 0.;
			for(size_t c = 1; c < columns; ++c)
				others += widths[c] + GAP;
			widths[0] = max(60., area.Width() - others);

			static const string none;
			auto drawCells = [&](const vector<string> &cells, double y, const Color &color) {
				double x = area.Left();
				for(size_t c = 0; c < columns; ++c)
				{
					const string &cell = c < cells.size() ? cells[c] : none;
					const int width = static_cast<int>(widths[c]);
					if(c == 0)
						font.Draw({cell, {width, Truncate::BACK}}, Point(x, y), color);
					else
						font.Draw({cell, {width, Alignment::RIGHT}}, Point(x, y), color);
					x += widths[c] + GAP;
				}
			};

			const Point mouse = UI::GetMouse();
			drawCells(panel.columns, area.Top(), headings ? medium : dim);
			if(headings)
			{
				double x = area.Left();
				for(size_t c = 0; c < columns; ++c)
				{
					const Rectangle zone = Rectangle::FromCorner(Point(x - 4., area.Top() - 3.), Point(widths[c] + 8., ROW));
					if(zone.Contains(mouse))
						FillShader::Fill(zone, *GameData::Colors().Get("faint"));
					const int column = static_cast<int>(c);
					AddZone(zone, [this, column]() { PressHeader(column); });
					x += widths[c] + GAP;
				}
			}
			FillShader::Fill(Point(area.Center().X(), area.Top() + ROW + 1.), Point(area.Width(), 1.), dim);

			const double rowsTop = area.Top() + ROW + 6.;
			const int visible = max(1, static_cast<int>((area.Bottom() - rowsTop) / ROW));
			const int count = static_cast<int>(panel.rows.size());
			// A newly selected row is scrolled into view.
			if(panel.selected != shownSelected)
			{
				shownSelected = panel.selected;
				if(panel.selected >= 0 && panel.selected < tableScroll)
					tableScroll = panel.selected;
				else if(panel.selected >= tableScroll + visible)
					tableScroll = panel.selected - visible + 1;
			}
			tableScroll = max(0, min(tableScroll, count - visible));
			shownRows = visible;
			for(int i = tableScroll; i < count && i < tableScroll + visible; ++i)
			{
				const double y = rowsTop + (i - tableScroll) * ROW;
				const Rectangle zone = Rectangle::FromCorner(Point(area.Left() - 4., y - 3.), Point(area.Width() + 8., ROW));
				const bool hover = zone.Contains(mouse);
				const bool chosen = i == panel.selected;
				if(chosen)
					FillShader::Fill(zone, *GameData::Colors().Get("dim"));
				else if(hover)
					FillShader::Fill(zone, *GameData::Colors().Get("faint"));
				drawCells(panel.rows[i], y, hover || chosen ? bright : medium);
				AddZone(zone, [this, i]() { PressRow(i); });
			}
			if(count > visible)
			{
				const string more = to_string(tableScroll + 1) + "-" + to_string(min(count, tableScroll + visible))
					+ " of " + to_string(count) + ", scroll for more";
				font.Draw({more, {static_cast<int>(area.Width()), Alignment::RIGHT}},
					Point(area.Left(), area.Bottom() + 2.), dim);
			}
		}

		// One button, numbered for its key.
		void DrawButton(int i, const Rectangle &button)
		{
			const Font &font = FontSet::Get(14);
			const string label = (i < 9 ? to_string(i + 1) + ". " : string()) + panel.buttons[i];
			const bool hover = button.Contains(UI::GetMouse());
			FillShader::Fill(button, *GameData::Colors().Get(hover ? "dim" : "faint"));
			font.Draw({label, {static_cast<int>(button.Width() - 8.), Truncate::BACK}},
				button.Center() - Point(.5 * min(static_cast<double>(font.Width(label)), button.Width() - 8.), .5 * font.Height()),
				*GameData::Colors().Get(hover ? "hover" : "active"));
			AddZone(button, [this, i]() { Press(i); });
		}

		bool Scroll(double dx, double dy) override
		{
			if(panel.columns.empty())
				return false;
			tableScroll -= static_cast<int>(round(dy * 3.));
			if(tableScroll < 0)
				tableScroll = 0;
			return true;
		}

		// Keys 1-9 press the buttons.
		bool PressDigit(SDL_Keycode key)
		{
			if(key >= '1' && key <= '9' && key - '1' < static_cast<int>(panel.buttons.size()))
			{
				Press(key - '1');
				return true;
			}
			return false;
		}

		// The table starts over at the top when the panel gets a new title.
		void TrackTitle()
		{
			if(panel.title != scrollTitle)
			{
				scrollTitle = panel.title;
				tableScroll = 0;
				shownSelected = -1;
			}
		}

		void PressRow(int i)
		{
			if(i < 0 || i >= static_cast<int>(panel.rows.size()))
				return;
			UI::PlaySound(UI::UISound::NORMAL);
			string mod = panel.mod;
			RunUIHook(player, GetUI(), [&mod, i]() { esmod_panel_row(mod.c_str(), i); });
		}

		void PressHeader(int column)
		{
			UI::PlaySound(UI::UISound::NORMAL);
			string mod = panel.mod;
			RunUIHook(player, GetUI(), [&mod, column]() { esmod_panel_header(mod.c_str(), column); });
		}

		void Press(int i)
		{
			if(i < 0 || i >= static_cast<int>(panel.buttons.size()))
				return;
			UI::PlaySound(UI::UISound::NORMAL);
			if(panel.chooser)
			{
				// One button per mod's planet button, in name order. The chosen mod's
				// hook may open its own panel in place of this one.
				auto it = planetButtons.begin();
				advance(it, min<size_t>(i, planetButtons.size()));
				panel.open = false;
				if(it != planetButtons.end())
				{
					string mod = it->first;
					RunUIHook(player, GetUI(), [&mod]() { esmod_planet_button(mod.c_str()); });
				}
				return;
			}
			string mod = panel.mod;
			RunUIHook(player, GetUI(), [&mod, i]() { esmod_panel(mod.c_str(), i); });
		}

		PlayerInfo &player;
		// The table's first shown row and how many show, kept while the panel keeps its title.
		int tableScroll = 0;
		int shownRows = 1;
		int shownSelected = -1;
		string scrollTitle;
	};



	// The panel mods open, shown in the planet screen's content area as the bank
	// and trading panels are: a title, scrolling text, and a row of buttons. The
	// planet screen pushes it on top of itself, and the keys it does not use (all
	// but 1-9) pass through to the planet screen.
	class ModPanel : public ModView {
	public:
		explicit ModPanel(PlayerInfo &player) : ModView(player), body(make_shared<TextArea>())
		{
			SetTrapAllEvents(false);
			body->SetFont(FontSet::Get(14));
			body->SetColor(*GameData::Colors().Get("medium"));
			AddChild(body);
		}
		~ModPanel() override
		{
			// Leaving the planet closes the panel, unless a newer planet screen
			// (loading a pilot replaces it) already shows its own.
			if(currentView == this)
			{
				currentView = nullptr;
				if(!panel.full)
					panel.open = false;
			}
		}

		void Draw() override
		{
			if(panel.full)
				return;
			// The planet screen's content area, where its description and the spaceport's text go.
			const Interface *ui = GameData::Interfaces().Get(Screen::Width() < 1280 ? "planet (small screen)" : "planet");
			const Rectangle area = ui->GetBox("content");
			const Rectangle box = Rectangle::WithCorners(area.TopLeft() + Point(10., 8.), area.BottomRight() - Point(10., 8.));
			const Font &big = FontSet::Get(18);
			const Font &font = FontSet::Get(14);
			big.Draw(panel.title, box.TopLeft(), *GameData::Colors().Get("bright"));
			FillShader::Fill(Point(box.Center().X(), box.Top() + 26.), Point(box.Width(), 1.), *GameData::Colors().Get("dim"));

			// The text scrolls between the title and the buttons, or, over a table, in a
			// band of three lines above it.
			const bool hasTable = !panel.columns.empty();
			const double textTop = box.Top() + 34.;
			const double tableBottom = box.Bottom() - 42.;
			const double textBottom = !hasTable ? tableBottom : panel.text.empty() ? textTop : min(textTop + 64., tableBottom);
			Rectangle textBox = Rectangle::WithCorners(Point(box.Left(), textTop), Point(box.Right(), textBottom));
			if(textBox.TopLeft() != shownBox.TopLeft() || textBox.Dimensions() != shownBox.Dimensions())
			{
				shownBox = textBox;
				body->SetRect(textBox);
			}
			if(panel.text != shownText)
			{
				shownText = panel.text;
				body->SetText(shownText);
			}
			TrackTitle();
			if(hasTable)
				DrawTable(Rectangle::WithCorners(Point(box.Left(), textBottom + (panel.text.empty() ? 0. : 8.)),
					Point(box.Right(), tableBottom - 8.)), false);

			// The buttons, right-aligned along the bottom.
			double right = box.Right();
			const double y = box.Bottom() - 15.;
			for(int i = static_cast<int>(panel.buttons.size()) - 1; i >= 0; --i)
			{
				const string label = (i < 9 ? to_string(i + 1) + ". " : string()) + panel.buttons[i];
				const double w = font.Width(label) + 24.;
				DrawButton(i, Rectangle(Point(right - .5 * w, y), Point(w, 30.)));
				right -= w + 8.;
			}
		}

	protected:
		bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override
		{
			return !panel.full && PressDigit(key);
		}

	private:
		shared_ptr<TextArea> body;
		Rectangle shownBox;
		string shownText;
	};



	// A mod's full screen (mod API 7), pushed over the planet screen as the
	// outfitter and shipyard are: the title across the top, the table filling
	// the left, and a sidebar on the right with the text and the buttons. It
	// closes when the mod closes its panel, or on Escape.
	class ModScreen;
	ModScreen *currentScreen = nullptr;
	class ModScreen : public ModView {
	public:
		explicit ModScreen(PlayerInfo &player) : ModView(player), body(make_shared<TextArea>())
		{
			body->SetFont(FontSet::Get(14));
			body->SetColor(*GameData::Colors().Get("medium"));
			AddChild(body);
			currentScreen = this;
		}
		~ModScreen() override
		{
			if(currentScreen == this)
				currentScreen = nullptr;
		}

		void Step() override
		{
			if(!panel.open || !panel.full)
				GetUI().Pop(this);
		}

		void Draw() override
		{
			const double SIDE = 360.;
			const double PAD = 20.;
			const Color &bright = *GameData::Colors().Get("bright");
			const Color &dim = *GameData::Colors().Get("dim");
			FillShader::Fill(Rectangle::WithCorners(Screen::TopLeft(), Screen::BottomRight()),
				*GameData::Colors().Get("shop info panel background"));
			const Rectangle side = Rectangle::WithCorners(Point(Screen::Right() - SIDE, Screen::Top()), Screen::BottomRight());
			FillShader::Fill(side, *GameData::Colors().Get("shop side panel background"));

			// The title over the table.
			const double left = Screen::Left() + PAD;
			const double right = side.Left() - PAD;
			FontSet::Get(18).Draw(panel.title, Point(left, Screen::Top() + PAD), bright);
			FillShader::Fill(Point((left + right) / 2., Screen::Top() + PAD + 28.), Point(right - left, 1.), dim);
			TrackTitle();
			if(!panel.columns.empty())
				DrawTable(Rectangle::WithCorners(Point(left, Screen::Top() + PAD + 44.), Point(right, Screen::Bottom() - PAD - 18.)), true);

			// The buttons, two to a row, along the bottom of the sidebar; the text above them.
			const double bw = (SIDE - 3. * 12.) / 2.;
			const int count = static_cast<int>(panel.buttons.size());
			const int rows = (count + 1) / 2;
			const double buttonsTop = Screen::Bottom() - 12. - rows * 38.;
			for(int i = 0; i < count; ++i)
			{
				const double x = side.Left() + 12. + (i % 2) * (bw + 12.);
				const double y = buttonsTop + (i / 2) * 38.;
				DrawButton(i, Rectangle::FromCorner(Point(x, y), Point(bw, 30.)));
			}
			const Rectangle textBox = Rectangle::WithCorners(Point(side.Left() + 16., Screen::Top() + PAD),
				Point(side.Right() - 16., buttonsTop - 12.));
			if(textBox.TopLeft() != shownBox.TopLeft() || textBox.Dimensions() != shownBox.Dimensions())
			{
				shownBox = textBox;
				body->SetRect(textBox);
			}
			if(panel.text != shownText)
			{
				shownText = panel.text;
				body->SetText(shownText);
			}
		}

	protected:
		bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override
		{
			if(key == SDLK_ESCAPE)
			{
				panel.open = false;
				GetUI().Pop(this);
				return true;
			}
			if(PressDigit(key))
				return true;
			const int count = static_cast<int>(panel.rows.size());
			if(count && (key == SDLK_DOWN || key == SDLK_UP))
			{
				// Arrow keys step the selection, as clicking the next row would.
				int next = panel.selected < 0 ? 0 : panel.selected + (key == SDLK_DOWN ? 1 : -1);
				PressRow(max(0, min(count - 1, next)));
				return true;
			}
			if(key == SDLK_PAGEDOWN || key == SDLK_PAGEUP)
			{
				tableScroll += (key == SDLK_PAGEDOWN ? 1 : -1) * max(1, shownRows - 1);
				return true;
			}
			return true;
		}

	private:
		shared_ptr<TextArea> body;
		Rectangle shownBox;
		string shownText;
	};



	// The one number a mod is asking the player for (ask_number), answered by
	// the game's number dialog.
	struct NumberRequest {
		string mod;
		int32_t token = 0;
		PlayerInfo *player = nullptr;
		UI *ui = nullptr;
		vector<Ship *> ships;

		void Answer(int value)
		{
			string name = mod;
			int32_t asked = token;
			RunUIHook(*player, ui, ships.empty() ? nullptr : &ships,
				[&name, asked, value]() { esmod_number(name.c_str(), asked, value); });
		}
	};
	NumberRequest numberRequest;

	void OpenPanel(const string &mod, const string &title, const string &text, bool chooser)
	{
		panel = PanelState{mod, title, text, {}, chooser, true};
	}
}



bool ModHarness::IsBuiltIn()
{
	return true;
}



int64_t ModHarness::LoadedCount()
{
	return loadedCount;
}



void ModHarness::Step(PlayerInfo &player, const list<shared_ptr<Ship>> &ships, list<shared_ptr<Ship>> &newShips,
	const list<ShipEvent> &events, int step)
{
	if(state == State::FAILED)
		return;

	Context current;
	current.player = &player;
	current.step = step;
	current.newShips = &newShips;
	current.allShips = &ships;
	const Ship *flagship = player.Flagship();
	for(const shared_ptr<Ship> &ship : ships)
		if(ship && !ship->IsDestroyed())
		{
			if(ship.get() == flagship)
				current.flagship = static_cast<int>(current.ships.size());
			current.ships.push_back(ship.get());
		}

	context = &current;
	if(state == State::UNSTARTED)
		Start(player);
	if(state == State::RUNNING)
	{
		inStep = true;
		PruneTags();
		CountKills(events);
		int today = player.GetDate().DaysSinceEpoch();
		// A new day may have brought events.
		if(today != lastDay)
			ForgetWorld();
		if(SyncDay(today))
			for(int day = lastDay + 1; day <= today; ++day)
				esmod_day(day);
		lastDay = today;
		const System *system = player.GetSystem();
		if(system && system != lastSystem)
		{
			lastSystem = system;
			esmod_enter(system->TrueName().c_str());
		}
		esmod_step(step);
		inStep = false;
		EndStepTiming();
	}
	context = nullptr;
}



void ModHarness::Shutdown()
{
	if(state == State::RUNNING)
		esmod_shutdown();
	// How long each mod's hooks took over the whole run.
	if(!timings.empty())
	{
		string summary = "Dado mod hook times (calls, total, average, slowest):";
		char line[256];
		for(const auto &[key, t] : timings)
		{
			snprintf(line, sizeof(line), "\n\t%s %s: %lld calls, %.1f ms, %.1f us, %.2f ms", key.first.c_str(),
				key.second.c_str(), static_cast<long long>(t.calls), t.totalMs,
				t.calls ? 1000. * t.totalMs / t.calls : 0., t.maxMs);
			summary += line;
		}
		Logger::Log(summary, Logger::Level::INFO);
		timings.clear();
	}
	if(frameSteps)
	{
		string summary = "Dado mods, share of each frame over " + to_string(frameSteps) + " engine steps (average, worst step):";
		for(const auto &[mod, share] : frameShares)
			summary += "\n\t" + mod + ": " + FramePercent(share.totalMs / frameSteps) + ", " + FramePercent(share.worstMs);
		Logger::Log(summary, Logger::Level::INFO);
	}
	frameShares.clear();
	frameSteps = 0;
	windowSteps = 0;
#ifndef NDEBUG
	if(!callStats.empty())
	{
		// The calls that took the most time in all, slowest first.
		vector<pair<pair<string, const char *>, CallStat>> calls(callStats.begin(), callStats.end());
		sort(calls.begin(), calls.end(), [](const auto &a, const auto &b) { return a.second.ms > b.second.ms; });
		string summary = "Dado mod calls into the game, by time (mod, call: calls, total, average):";
		char line[256];
		for(size_t i = 0; i < calls.size() && i < 25; ++i)
		{
			const auto &[key, stat] = calls[i];
			snprintf(line, sizeof(line), "\n\t%s %s: %lld calls, %.1f ms, %.2f us", key.first.c_str(), key.second,
				static_cast<long long>(stat.calls), stat.ms, stat.calls ? 1000. * stat.ms / stat.calls : 0.);
			summary += line;
		}
		Logger::Log(summary, Logger::Level::INFO);
		callStats.clear();
	}
#endif
	state = State::UNSTARTED;
	loadedCount = 0;
	tagged.clear();
	kills.clear();
	overlays.clear();
	details.clear();
	planetButtons.clear();
	outfitterButtons.clear();
	galaxy.clear();
}



void ModHarness::Landed(PlayerInfo &player)
{
	if(state == State::FAILED)
		return;
	Context current;
	current.player = &player;
	current.landing = true;
	context = &current;
	// Landing may have run missions' events.
	ForgetWorld();
	if(state == State::UNSTARTED)
		Start(player);
	const System *system = player.GetSystem();
	if(state == State::RUNNING && system)
	{
		// A pilot loaded on a planet starts its own clock.
		SyncDay(player.GetDate().DaysSinceEpoch());
		esmod_enter(system->TrueName().c_str());
	}
	context = nullptr;
	// Taking off replaces the ships; the first step in flight runs on_enter again.
	lastSystem = nullptr;
}



void ModHarness::ShipsReplaced()
{
	lastSystem = nullptr;
	// Taking off or loading a pilot may have run events.
	ForgetWorld();
}



void ModHarness::DrawMap(const PlayerInfo &player, const Point &center, double zoom)
{
	const Font &font = FontSet::Get(14);
	// Labels on one system stack downward.
	map<const System *, int> stacked;
	for(const auto &it : overlays)
		for(const Overlay &item : it.second)
		{
			if(!item.from)
				continue;
			Point from = zoom * (item.from->Position() + center);
			if(item.to)
			{
				Point to = zoom * (item.to->Position() + center);
				if(from.Distance(to) < 1.)
					continue;
				Point unit = (to - from).Unit();
				LineShader::DrawDashed(from + 9. * unit, to - 9. * unit, unit, 2.f, item.color, 8., 4.);
				// An arrowhead at the objective.
				Point tip = to - 9. * unit;
				Point side(-unit.Y(), unit.X());
				LineShader::Draw(tip, tip - 8. * unit + 5. * side, 2.f, item.color);
				LineShader::Draw(tip, tip - 8. * unit - 5. * side, 2.f, item.color);
			}
			else
			{
				RingShader::Draw(from, 10.f, 8.f, item.color);
				if(!item.label.empty())
				{
					int row = stacked[item.from]++;
					Point at = from + Point(-.5 * font.Width(item.label), 12. + row * (font.Height() + 2.));
					font.Draw(item.label, at, item.color);
				}
			}
		}
}



void ModHarness::DrawMapDetails(const System *system, double top)
{
	if(!system)
		return;
	vector<const Detail *> sections;
	for(const auto &it : details)
		for(const Detail &item : it.second)
			if(item.system == system)
				sections.push_back(&item);
	if(sections.empty())
		return;

	// The panel sits under the orbits-and-key box, as wide as it, and stops
	// above the map buttons in the bottom right corner.
	const double WIDTH = 250.;
	const double PAD = 10.;
	const double left = Screen::Right() - WIDTH;
	const double bottom = Screen::Bottom() - 100.;
	const Font &font = FontSet::Get(14);
	const Color &text = *GameData::Colors().Get("medium");
	const Color &dim = *GameData::Colors().Get("dim");
	const Color &back = *GameData::Colors().Get("map side panel background");

	WrappedText wrap(font);
	wrap.SetWrapWidth(static_cast<int>(WIDTH - 2. * PAD));
	wrap.SetAlignment(Alignment::LEFT);
	wrap.SetParagraphBreak(0);

	// Lay the sections out first, to size the background; whatever does not
	// fit above the buttons is left out, and the panel says so.
	struct Placed { const Detail *detail; double y; double height; };
	vector<Placed> placed;
	double y = top + PAD;
	bool clipped = false;
	for(const Detail *item : sections)
	{
		wrap.Wrap(item->text);
		double height = font.Height() + 4. + (item->text.empty() ? 0. : wrap.Height(false));
		if(y + height > bottom - PAD - (placed.empty() ? 0. : font.Height()))
		{
			clipped = true;
			break;
		}
		placed.push_back(Placed{item, y, height});
		y += height + PAD;
	}
	if(placed.empty())
		return;
	double end = y - PAD + (clipped ? font.Height() + 4. : 0.) + PAD;
	FillShader::Fill(Rectangle::FromCorner(Point(left, top), Point(WIDTH, end - top)), back);

	for(size_t i = 0; i < placed.size(); ++i)
	{
		const Placed &at = placed[i];
		if(i)
			FillShader::Fill(Rectangle::FromCorner(Point(left + PAD, at.y - .5 * PAD - .5), Point(WIDTH - 2. * PAD, 1.)), dim);
		font.Draw(DisplayText(at.detail->title, Layout(static_cast<int>(WIDTH - 2. * PAD), Truncate::BACK)),
			Point(left + PAD, at.y), at.detail->color);
		if(!at.detail->text.empty())
		{
			wrap.Wrap(at.detail->text);
			wrap.Draw(Point(left + PAD, at.y + font.Height() + 4.), text);
		}
	}
	if(clipped)
		font.Draw("(more not shown)", Point(left + PAD, end - PAD - font.Height()), dim);
}



string ModHarness::PlanetButtonLabel()
{
	if(planetButtons.empty())
		return string();
	if(planetButtons.size() == 1)
		return planetButtons.begin()->second;
	return "Mods";
}



shared_ptr<Panel> ModHarness::NewPlanetPanel(PlayerInfo &player)
{
	auto view = make_shared<ModPanel>(player);
	currentView = view.get();
	return view;
}



bool ModHarness::PanelOpen()
{
	// The planet screen's own panel; a full screen sits over the planet screen instead.
	return panel.open && !panel.full;
}



void ModHarness::ClosePanel()
{
	if(!panel.full)
		panel.open = false;
}



vector<string> ModHarness::OutfitterButtons()
{
	vector<string> labels;
	for(const auto &it : outfitterButtons)
		labels.push_back(it.second);
	return labels;
}



bool ModHarness::OutfitterButtonActive(size_t i, PlayerInfo &player, const vector<Ship *> &ships,
	const Outfit *outfit)
{
	if(i >= outfitterButtons.size() || ships.empty())
		return false;
	auto it = outfitterButtons.begin();
	advance(it, i);
	string mod = it->first;
	string name = outfit ? outfit->TrueName() : string();
	bool active = false;
	RunUIHook(player, nullptr, &ships, [&]() { active = esmod_can_outfitter(mod.c_str(), name.c_str()); });
	return active;
}



void ModHarness::OutfitterButton(size_t i, PlayerInfo &player, UI &ui, const vector<Ship *> &ships,
	const Outfit *outfit)
{
	if(i >= outfitterButtons.size() || ships.empty())
		return;
	auto it = outfitterButtons.begin();
	advance(it, i);
	string mod = it->first;
	string name = outfit ? outfit->TrueName() : string();
	RunUIHook(player, &ui, &ships, [&]() { esmod_outfitter(mod.c_str(), name.c_str()); });
}



void ModHarness::PlanetButton(PlayerInfo &player, UI &ui)
{
	if(planetButtons.empty())
		return;
	if(planetButtons.size() == 1)
	{
		string mod = planetButtons.begin()->first;
		RunUIHook(player, ui, [&mod]() { esmod_planet_button(mod.c_str()); });
		return;
	}
	// Several mods: a panel to choose among them.
	RunUIHook(player, ui, []() {
		OpenPanel(string(), "Mods", "Choose one:", true);
		for(const auto &it : planetButtons)
			panel.buttons.push_back(it.second);
	});
}



namespace {
	// A fleet definition's expected stat, averaged over its variants (es_fleet_stat).
	double FleetStat(const Fleet *fleet, const string &which)
	{
		// One ship's share of a stat, from its model's attributes and weapons.
		auto shipStat = [&which](const Ship &ship) -> double {
			const Outfit &attributes = ship.Attributes();
			if(which == "ships")
				return 1.;
			if(which == "hull" || which == "shields")
				return attributes.Get(which);
			if(which == "cost")
				return static_cast<double>(ship.Cost());
			if(which == "speed")
			{
				double drag = attributes.Get("drag");
				return drag > 0. ? attributes.Get("thrust") / drag : 0.;
			}
			if(which == "dps")
			{
				double dps = 0.;
				for(const Hardpoint &hardpoint : ship.Weapons())
				{
					const Weapon *weapon = hardpoint.GetWeapon();
					if(weapon && weapon->Reload() > 0.)
						dps += (weapon->ShieldDamage() + weapon->HullDamage()) * 60. / weapon->Reload();
				}
				return dps;
			}
			return 0.;
		};
		if(which == "speed")
			return fleet->Variants().Average([&shipStat](const Variant &variant) -> double {
				const vector<const Ship *> &ships = variant.Ships();
				double sum = 0.;
				for(const Ship *ship : ships)
					sum += shipStat(*ship);
				return ships.empty() ? 0. : sum / ships.size();
			});
		return fleet->Variants().Average([&shipStat](const Variant &variant) -> double {
			double sum = 0.;
			for(const Ship *ship : variant.Ships())
				sum += shipStat(*ship);
			return sum;
		});
	}
}



// The C surface the harness calls: modding/include/es_host.h.
extern "C" {

void es_log(const char *text)
{
	ES_CALL_TIMER;
	Logger::Log(text, Logger::Level::INFO);
}



void es_message(const char *text)
{
	ES_CALL_TIMER;
	if(context)
		Messages::Add({text, GameData::MessageCategories().Get("normal")});
}



int32_t es_step()
{
	ES_CALL_TIMER;
	return context ? context->step : 0;
}



const char *es_system_name()
{
	ES_CALL_TIMER;
	return context && context->player->GetSystem() ? context->player->GetSystem()->DisplayName().c_str() : "";
}



int32_t es_ship_count()
{
	ES_CALL_TIMER;
	return context ? static_cast<int32_t>(context->ships.size()) : 0;
}



int32_t es_flagship()
{
	ES_CALL_TIMER;
	return context ? context->flagship : -1;
}



const char *es_ship_name(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->GivenName().c_str() : "";
}



const char *es_ship_model(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->DisplayModelName().c_str() : "";
}



int32_t es_ship_is_player(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship && ship->IsYours();
}



int32_t es_ship_is_disabled(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship && ship->IsDisabled();
}



double es_ship_hull(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->HullLevel() : 0.;
}



double es_ship_max_hull(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->MaxHull() : 0.;
}



double es_ship_shields(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->ShieldLevel() : 0.;
}



double es_ship_max_shields(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->MaxShields() : 0.;
}



double es_ship_energy(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->EnergyLevel() : 0.;
}



double es_ship_fuel(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? ship->FuelLevel() : 0.;
}



void es_ship_set_hull(int32_t i, double value)
{
	ES_CALL_TIMER;
	if(Ship *ship = GetShip(i))
	{
		ModHarness::SetHull(*ship, value);
		// A ship's hull decides whether it can still fight (tag_strength).
		context->cache.tagsBuilt = false;
		context->cache.tagStrength.clear();
	}
}



void es_ship_set_shields(int32_t i, double value)
{
	ES_CALL_TIMER;
	if(Ship *ship = GetShip(i))
		ModHarness::SetShields(*ship, value);
}



int64_t es_condition_get(const char *name)
{
	ES_CALL_TIMER;
	return context ? context->player->Conditions().Get(name) : 0;
}



void es_condition_set(const char *name, int64_t value)
{
	ES_CALL_TIMER;
	if(context)
		context->player->Conditions().Set(name, value);
}



// ---- Mod API 2 ----

int32_t es_day()
{
	ES_CALL_TIMER;
	return context ? context->player->GetDate().DaysSinceEpoch() : 0;
}



const char *es_here()
{
	ES_CALL_TIMER;
	return context && context->player->GetSystem() ? context->player->GetSystem()->TrueName().c_str() : "";
}



int32_t es_system_count()
{
	ES_CALL_TIMER;
	return static_cast<int32_t>(GalaxySystems().size());
}



const char *es_system_at(int32_t i)
{
	ES_CALL_TIMER;
	const auto &systems = GalaxySystems();
	return i >= 0 && i < static_cast<int32_t>(systems.size()) ? systems[i]->TrueName().c_str() : "";
}



const char *es_system_government(const char *name)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	return system ? system->GetGovernment()->TrueName().c_str() : "";
}



int32_t es_system_link_count(const char *name)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	return system ? static_cast<int32_t>(system->Links().size()) : 0;
}



const char *es_system_link(const char *name, int32_t k)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	if(!system || k < 0 || k >= static_cast<int32_t>(system->Links().size()))
		return "";
	auto it = system->Links().begin();
	advance(it, k);
	return (*it)->TrueName().c_str();
}



int32_t es_system_inhabited(const char *name)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	return system && system->IsInhabited(nullptr);
}



int32_t es_system_defense_count(const char *name)
{
	ES_CALL_TIMER;
	return static_cast<int32_t>(DefenseFleets(FindSystem(name)).size());
}



const char *es_system_defense_fleet(const char *name, int32_t k)
{
	ES_CALL_TIMER;
	const vector<const Fleet *> &fleets = DefenseFleets(FindSystem(name));
	return k >= 0 && k < static_cast<int32_t>(fleets.size()) ? FleetName(fleets[k]).c_str() : "";
}



int32_t es_change(const char *text)
{
	ES_CALL_TIMER;
	if(!context || !text)
		return 0;
	// The text is an event's body: indent it under an unnamed event, which saves its raw changes.
	string body = "event\n";
	istringstream lines(text);
	for(string line; getline(lines, line); )
		body += "\t" + line + "\n";
	istringstream in(body);
	DataFile file(in);
	for(const DataNode &node : file)
		if(node.Token(0) == "event")
		{
			// An event with nothing in it changes nothing; one with only visits or
			// condition assignments (no data changes) is still applied.
			if(!node.HasChildren())
				return 0;
			GameEvent event(node, &context->player->Conditions());
			context->player->AddEvent(std::move(event), context->player->GetDate());
			// The event may have changed systems, planets and fleets.
			ForgetWorld();
			context->cache.defense.clear();
			context->cache.fleetStats.clear();
			return 1;
		}
	return 0;
}



double es_attitude(const char *government, const char *toward)
{
	ES_CALL_TIMER;
	const Government *a = FindGovernment(government);
	const Government *b = FindGovernment(toward);
	return a && b ? a->AttitudeToward(b) : 0.;
}



double es_reputation(const char *government)
{
	ES_CALL_TIMER;
	const Government *gov = FindGovernment(government);
	return gov ? gov->Reputation() : 0.;
}



void es_set_reputation(const char *government, double value)
{
	ES_CALL_TIMER;
	if(const Government *gov = FindGovernment(government))
		gov->SetReputation(value);
}



int32_t es_plugin_enabled(const char *name)
{
	ES_CALL_TIMER;
	const Plugin *plugin = name ? PluginManager::Get().Find(name) : nullptr;
	return plugin && plugin->IsValid() && plugin->enabled;
}



double es_fleet_strength(const char *name)
{
	ES_CALL_TIMER;
	const Fleet *fleet = name ? GameData::Fleets().Find(name) : nullptr;
	return fleet && fleet->IsValid() ? static_cast<double>(fleet->Strength()) : 0.;
}



double es_fleet_stat(const char *name, const char *stat)
{
	ES_CALL_TIMER;
	const Fleet *fleet = name ? GameData::Fleets().Find(name) : nullptr;
	if(!fleet || !fleet->IsValid() || !stat)
		return 0.;
	if(!context)
		return FleetStat(fleet, stat);
	auto key = make_pair(fleet, string(stat));
	auto it = context->cache.fleetStats.find(key);
	if(it == context->cache.fleetStats.end())
		it = context->cache.fleetStats.emplace(key, FleetStat(fleet, key.second)).first;
	return it->second;
}



int32_t es_system_fleet_count(const char *name)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	return system ? static_cast<int32_t>(system->Fleets().size()) : 0;
}



const char *es_system_fleet(const char *name, int32_t k)
{
	ES_CALL_TIMER;
	const System *system = FindSystem(name);
	if(!system || k < 0 || k >= static_cast<int32_t>(system->Fleets().size()))
		return "";
	return FleetName(system->Fleets()[k].Get()).c_str();
}



int32_t es_spawn(const char *name, const char *government, const char *tag, int32_t arrive)
{
	ES_CALL_TIMER;
	if(!context || !context->newShips || !context->player->GetSystem())
		return 0;
	const Fleet *fleet = name ? GameData::Fleets().Find(name) : nullptr;
	const Government *gov = FindGovernment(government);
	if(!fleet || !fleet->IsValid())
		return 0;
	list<shared_ptr<Ship>> spawned;
	if(arrive)
		fleet->Enter(*context->player->GetSystem(), spawned);
	else
		fleet->Place(*context->player->GetSystem(), spawned);
	int32_t count = 0;
	string label = tag ? tag : "";
	for(const shared_ptr<Ship> &ship : spawned)
	{
		if(gov)
			ship->SetGovernment(gov);
		Tag(ship, label);
		++count;
		// Fighters and drones in the ship's bays launch later, as part of the same flotilla.
		for(const Ship::Bay &bay : ship->Bays())
			if(bay.ship)
			{
				if(gov)
					bay.ship->SetGovernment(gov);
				Tag(bay.ship, label);
				++count;
			}
	}
	context->newShips->splice(context->newShips->end(), spawned);
	// New tagged ships count toward tag_strength at once.
	context->cache.tagsBuilt = false;
	context->cache.tagStrength.clear();
	return count;
}



const char *es_ship_tag(int32_t i)
{
	ES_CALL_TIMER;
	const string *tag = TagOf(GetShip(i));
	return tag ? tag->c_str() : "";
}



const char *es_ship_government(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship && ship->GetGovernment() ? ship->GetGovernment()->TrueName().c_str() : "";
}



double es_ship_strength(int32_t i)
{
	ES_CALL_TIMER;
	const Ship *ship = GetShip(i);
	return ship ? static_cast<double>(ship->Strength()) : 0.;
}



double es_tag_strength(const char *tag)
{
	ES_CALL_TIMER;
	if(!context || !tag)
		return 0.;
	// Every tag's strength in one pass, once per run of hooks.
	Caches &cache = context->cache;
	if(!cache.tagsBuilt)
	{
		const System *here = context->player->GetSystem();
		for(const auto &it : tagged)
		{
			shared_ptr<Ship> ship = it.second.ship.lock();
			if(ship && !ship->IsDestroyed() && !ship->IsDisabled()
					&& (ship->GetSystem() == here || !ship->GetSystem()))
				cache.tagStrength[it.second.tag] += static_cast<double>(ship->Strength());
		}
		cache.tagsBuilt = true;
	}
	auto it = cache.tagStrength.find(string_view(tag));
	return it == cache.tagStrength.end() ? 0. : it->second;
}



int32_t es_player_kills(const char *prefix)
{
	ES_CALL_TIMER;
	string start = prefix ? prefix : "";
	int32_t total = 0;
	for(auto it = kills.begin(); it != kills.end(); )
		if(it->first.compare(0, start.size(), start) == 0)
		{
			total += it->second;
			it = kills.erase(it);
		}
		else
			++it;
	return total;
}



void es_map_clear(const char *mod)
{
	ES_CALL_TIMER;
	overlays.erase(mod ? mod : "");
	details.erase(mod ? mod : "");
}



void es_map_line(const char *mod, const char *from, const char *to, double r, double g, double b)
{
	ES_CALL_TIMER;
	const System *a = FindSystem(from);
	const System *z = FindSystem(to);
	if(a && z)
		overlays[mod ? mod : ""].push_back(Overlay{a, z, Color(r, g, b, 1.f), string()});
}



void es_map_marker(const char *mod, const char *system, double r, double g, double b, const char *label)
{
	ES_CALL_TIMER;
	if(const System *at = FindSystem(system))
		overlays[mod ? mod : ""].push_back(Overlay{at, nullptr, Color(r, g, b, 1.f), label ? label : ""});
}



void es_map_detail(const char *mod, const char *system, const char *title, const char *text, double r, double g, double b)
{
	ES_CALL_TIMER;
	if(const System *at = FindSystem(system))
		details[mod ? mod : ""].push_back(Detail{at, title ? title : "", text ? text : "", Color(r, g, b, 1.f)});
}



void es_planet_button(const char *mod, const char *label)
{
	ES_CALL_TIMER;
	string name = mod ? mod : "";
	if(label && *label)
		planetButtons[name] = label;
	else
		planetButtons.erase(name);
}



void es_panel_open(const char *mod, const char *title, const char *text)
{
	ES_CALL_TIMER;
	string name = mod ? mod : "";
	// A mod opens a panel from a hook the player started or on landing, or refreshes its own open one.
	if(!(context && (context->ui || context->landing)) && !(panel.open && panel.mod == name))
		return;
	OpenPanel(name, title ? title : "", text ? text : "", false);
}



void es_panel_button(const char *mod, const char *label)
{
	ES_CALL_TIMER;
	if(panel.open && panel.mod == (mod ? mod : "") && label)
		panel.buttons.push_back(label);
}



void es_panel_close(const char *mod)
{
	ES_CALL_TIMER;
	if(panel.open && panel.mod == (mod ? mod : ""))
		panel.open = false;
}



// The harness times each hook it runs for a mod (not part of the mod API).
void es_hook_timing(const char *mod, const char *hook, int32_t begin)
{
	auto now = chrono::steady_clock::now();
	if(begin)
	{
		currentMod = mod ? mod : "";
		hookStart = now;
		return;
	}
	double ms = chrono::duration<double, milli>(now - hookStart).count();
	if(inStep)
		stepMs[currentMod] += ms;
	currentMod.clear();
	HookTiming &t = timings[make_pair(string(mod ? mod : ""), string(hook ? hook : ""))];
	++t.calls;
	t.totalMs += ms;
	t.maxMs = max(t.maxMs, ms);
	if(ms >= SLOW_HOOK_MS && ms >= 2. * t.reportedMs)
	{
		t.reportedMs = ms;
		char text[256];
		snprintf(text, sizeof(text), "Dado mod \"%s\": %s took %.1f ms (slow is %.0f ms or more).",
			mod ? mod : "", hook ? hook : "", ms, SLOW_HOOK_MS);
		Logger::Log(text, Logger::Level::WARNING);
	}
}



// ---- Mod API 5 ----

void es_outfitter_button(const char *mod, const char *label)
{
	ES_CALL_TIMER;
	string name = mod ? mod : "";
	if(label && *label)
		outfitterButtons[name] = label;
	else
		outfitterButtons.erase(name);
}



int32_t es_outfitter_ship_count()
{
	ES_CALL_TIMER;
	return context && context->outfitterShips ? static_cast<int32_t>(context->outfitterShips->size()) : 0;
}



const char *es_outfitter_ship(int32_t k)
{
	ES_CALL_TIMER;
	if(!context || !context->outfitterShips || k < 0 || k >= static_cast<int32_t>(context->outfitterShips->size()))
		return "";
	return Return((*context->outfitterShips)[k]->UUID().ToString());
}



int32_t es_fleet_count()
{
	ES_CALL_TIMER;
	return static_cast<int32_t>(PlayerFleet().size());
}



const char *es_fleet_ship(int32_t k)
{
	ES_CALL_TIMER;
	const vector<Ship *> &fleet = PlayerFleet();
	if(k < 0 || k >= static_cast<int32_t>(fleet.size()))
		return "";
	return Return(fleet[k]->UUID().ToString());
}



const char *es_fleet_ship_name(const char *uuid)
{
	ES_CALL_TIMER;
	const Ship *ship = FindPlayerShip(uuid);
	return ship ? ship->GivenName().c_str() : "";
}



const char *es_fleet_ship_model(const char *uuid)
{
	ES_CALL_TIMER;
	const Ship *ship = FindPlayerShip(uuid);
	return ship ? ship->DisplayModelName().c_str() : "";
}



int32_t es_ship_outfit_count(const char *uuid, const char *outfit)
{
	ES_CALL_TIMER;
	const Ship *ship = FindPlayerShip(uuid);
	const Outfit *it = FindOutfit(outfit);
	return ship && it ? ship->OutfitCount(it) : 0;
}



int32_t es_ship_integrated_count(const char *uuid, const char *outfit)
{
	ES_CALL_TIMER;
	const Ship *ship = FindPlayerShip(uuid);
	const Outfit *it = FindOutfit(outfit);
	return ship && it ? ship->IntegratedCount(it) : 0;
}



int32_t es_ship_integrate(const char *uuid, const char *outfit, double scale)
{
	ES_CALL_TIMER;
	Ship *ship = FindPlayerShip(uuid);
	const Outfit *it = FindOutfit(outfit);
	return ship && it && ship->Integrate(it, scale);
}



const char *es_outfit_name(const char *outfit)
{
	ES_CALL_TIMER;
	const Outfit *it = FindOutfit(outfit);
	return it ? it->DisplayName().c_str() : "";
}



const char *es_outfit_category(const char *outfit)
{
	ES_CALL_TIMER;
	const Outfit *it = FindOutfit(outfit);
	return it ? it->Category().c_str() : "";
}



double es_outfit_attribute(const char *outfit, const char *attribute)
{
	ES_CALL_TIMER;
	const Outfit *it = FindOutfit(outfit);
	if(!it || !attribute)
		return 0.;
	const string name = attribute;
	if(name == "mass")
		return it->Mass();
	if(name == "cost")
		return static_cast<double>(it->Cost());
	return it->Get(name);
}



int32_t es_outfit_is_weapon(const char *outfit)
{
	ES_CALL_TIMER;
	const Outfit *it = FindOutfit(outfit);
	return it && it->GetWeapon();
}



// ---- Mod API 6 ----

namespace {
	const Planet *FindPlanet(const char *name)
	{
		const Planet *planet = name && *name ? GameData::Planets().Find(name) : nullptr;
		return planet && planet->IsValid() ? planet : nullptr;
	}

	// A system's planets, in the order of its objects, each once.
	vector<const Planet *> PlanetsOf(const System *system)
	{
		vector<const Planet *> planets;
		if(system)
			for(const StellarObject &object : system->Objects())
				if(object.HasValidPlanet() && find(planets.begin(), planets.end(), object.GetPlanet()) == planets.end())
					planets.push_back(object.GetPlanet());
		return planets;
	}
}



const char *es_data(const char *mod, const char *key)
{
	ES_CALL_TIMER;
	if(!context || !mod || !key)
		return "";
	return context->player->ModData(mod, key).c_str();
}



void es_set_data(const char *mod, const char *key, const char *text)
{
	ES_CALL_TIMER;
	// A mod's text is saved with the pilot; 4 MB a key is plenty.
	if(!context || !mod || !key || !*key || !text || strlen(text) > (4u << 20))
		return;
	context->player->SetModData(mod, key, text);
}



const char *es_planet()
{
	ES_CALL_TIMER;
	const Planet *planet = context ? context->player->GetPlanet() : nullptr;
	return planet ? planet->TrueName().c_str() : "";
}



const char *es_planet_name(const char *name)
{
	ES_CALL_TIMER;
	const Planet *planet = FindPlanet(name);
	return planet ? planet->DisplayName().c_str() : "";
}



const char *es_planet_system(const char *name)
{
	ES_CALL_TIMER;
	const Planet *planet = FindPlanet(name);
	return planet && planet->GetSystem() ? planet->GetSystem()->TrueName().c_str() : "";
}



const char *es_planet_government(const char *name)
{
	ES_CALL_TIMER;
	const Planet *planet = FindPlanet(name);
	return planet && planet->GetGovernment() ? planet->GetGovernment()->TrueName().c_str() : "";
}



int32_t es_planet_count(const char *system)
{
	ES_CALL_TIMER;
	return static_cast<int32_t>(PlanetsOf(FindSystem(system)).size());
}



const char *es_planet_at(const char *system, int32_t k)
{
	ES_CALL_TIMER;
	vector<const Planet *> planets = PlanetsOf(FindSystem(system));
	return k >= 0 && k < static_cast<int32_t>(planets.size()) ? planets[k]->TrueName().c_str() : "";
}



int32_t es_planet_service(const char *name, const char *service)
{
	ES_CALL_TIMER;
	const Planet *planet = FindPlanet(name);
	if(!planet || !service || !planet->CanUseServices())
		return 0;
	const string which = service;
	const Port &port = planet->GetPort();
	if(which == "bank")
		return port.HasService(Port::ServicesType::Bank);
	if(which == "trading")
		return port.HasService(Port::ServicesType::Trading);
	if(which == "job board")
		return port.HasService(Port::ServicesType::JobBoard);
	if(which == "hire crew")
		return port.HasService(Port::ServicesType::HireCrew);
	if(which == "outfitter")
		return planet->HasOutfitter();
	if(which == "shipyard")
		return planet->HasShipyard();
	return 0;
}



void es_panel_columns(const char *mod, const char *headers)
{
	ES_CALL_TIMER;
	if(panel.open && panel.mod == (mod ? mod : ""))
	{
		panel.columns = SplitCells(headers);
		panel.rows.clear();
	}
}



void es_panel_row(const char *mod, const char *cells)
{
	ES_CALL_TIMER;
	if(panel.open && panel.mod == (mod ? mod : "") && !panel.columns.empty())
		panel.rows.push_back(SplitCells(cells));
}



void es_ask_number(const char *mod, const char *text, int32_t token, int32_t most)
{
	ES_CALL_TIMER;
	// Only a hook the player started may ask them something.
	if(!context || !context->ui || most < 1)
		return;
	numberRequest.mod = mod ? mod : "";
	numberRequest.token = token;
	numberRequest.player = context->player;
	numberRequest.ui = context->ui;
	numberRequest.ships = context->outfitterShips ? *context->outfitterShips : vector<Ship *>();
	context->ui->Push(DialogPanel::RequestIntegerWithValidation(&numberRequest, &NumberRequest::Answer,
		[most](int value) { return value >= 1 && value <= most; }, text ? text : ""));
}



// ---- Mod API 7 ----

void es_screen_open(const char *mod, const char *title, const char *text)
{
	ES_CALL_TIMER;
	string name = mod ? mod : "";
	// A screen opens only from a hook the player started, or replaces the mod's own open one.
	if(!(context && context->ui) && !(panel.open && panel.mod == name && panel.full))
		return;
	OpenPanel(name, title ? title : "", text ? text : "", false);
	panel.full = true;
	if(!currentScreen && context && context->ui)
		context->ui->Push(new ModScreen(*context->player));
}



void es_panel_select(const char *mod, int32_t row)
{
	ES_CALL_TIMER;
	if(panel.open && panel.mod == (mod ? mod : ""))
		panel.selected = row;
}



void es_confirm(const char *mod, const char *text, int32_t token)
{
	ES_CALL_TIMER;
	// Only a hook the player started may ask them something.
	if(!context || !context->ui)
		return;
	string name = mod ? mod : "";
	PlayerInfo *player = context->player;
	UI *ui = context->ui;
	vector<Ship *> ships;
	if(context->outfitterShips)
		ships = *context->outfitterShips;
	// "Cancel" is the default: what a mod asks to confirm may not be undone.
	ui->Push(DialogPanel::CallFunctionIfOk([name, player, ui, ships, token]() {
		RunUIHook(*player, ui, ships.empty() ? nullptr : &ships,
			[&name, token]() { esmod_confirm(name.c_str(), token); });
	}, text ? text : "", 2));
}

}

#endif
