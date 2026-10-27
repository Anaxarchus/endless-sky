/* test_host.cpp
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

// Tests the game's mod loader discovery, calls and mod identity bookkeeping
// (source/modding/ModLoaders) against the test loaders built from test_loader.c.
// Built and run by run.sh, with the game's Logger replaced by a recorder and,
// unless ES_TEST_REAL_SDL is set, SDL's shared object calls implemented here
// over dlopen, so it needs neither the game's libraries nor SDL's.
// Usage: test-host <resources dir> <config dir>, each holding a "modloaders" folder.

// From the folder run.sh names with -I: source/modding, or a mutated copy of it.
#include "ModLoaders.h"

#include "../../source/Logger.h"
#include "../../source/SDL.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef ES_TEST_REAL_SDL
#include <dlfcn.h>
#endif

using namespace std;

namespace {
	vector<string> logs;
	int failures = 0;

	void Check(bool ok, const string &what)
	{
		printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
		if(!ok)
			++failures;
	}

	bool Logged(const string &part)
	{
		for(const string &line : logs)
			if(line.find(part) != string::npos)
				return true;
		return false;
	}

	// The host table's hook_timing and one mod-scoped call, doing what
	// ModHarness.cpp does with them: all the identity bookkeeping is the real
	// ModLoaders code.
	ModLoaders loaders;
	// Each mod-scoped call, as "mod:label", or "refused:label".
	vector<string> calls;
	int ignoredEnds = 0;

	void ES_CALL HostLog(const char *text)
	{
		logs.push_back(string("host: ") + (text ? text : ""));
	}

	void ES_CALL HostHookTiming(const char *mod, const char *, int32_t begin)
	{
		if(begin)
			loaders.BeginHook(mod ? mod : "");
		else if(!loaders.HookOpen())
			++ignoredEnds;
		else
			loaders.EndHook();
	}

	void ES_CALL HostPlanetButton(const char *label)
	{
		const char *mod = loaders.RunningMod();
		calls.push_back(string(mod ? mod : "refused") + ":" + label);
	}

	struct TlStats {
		int init_ok;
		int load_calls;
		int sources;
		char last_source_name[64];
		int steps;
		int days;
		int enters;
		int planet_buttons;
		int panels;
		char last_target[64];
	};

	TlStats *Stats(const ModLoaders::Loader &loader)
	{
		using StatsFn = TlStats *(ES_CALL *)();
#ifdef ES_USE_SDL3
		auto symbol = SDL_LoadFunction(static_cast<SDL_SharedObject *>(loader.handle), "tl_stats");
#else
		auto symbol = SDL_LoadFunction(loader.handle, "tl_stats");
#endif
		return symbol ? reinterpret_cast<StatsFn>(symbol)() : nullptr;
	}

	size_t Count(const string &call)
	{
		size_t n = 0;
		for(const string &c : calls)
			n += (c == call);
		return n;
	}
}



// The game's Logger, recorded.
void Logger::Log(const string &message, Level level)
{
	logs.push_back(string(1, static_cast<char>(level)) + ": " + message);
}



#ifndef ES_TEST_REAL_SDL
// SDL's shared object calls, over dlopen.
namespace {
	string sdlError;
}

extern "C" {
void *SDL_LoadObject(const char *file)
{
	void *handle = dlopen(file, RTLD_NOW | RTLD_LOCAL);
	if(!handle)
		sdlError = dlerror();
	return handle;
}

void *SDL_LoadFunction(void *handle, const char *name)
{
	return dlsym(handle, name);
}

void SDL_UnloadObject(void *handle)
{
	dlclose(handle);
}

const char *SDL_GetError()
{
	return sdlError.c_str();
}
}
#endif



int main(int argc, char *argv[])
{
	if(argc != 3)
	{
		fprintf(stderr, "usage: %s <resources dir> <config dir>\n", argv[0]);
		return 2;
	}
	const filesystem::path resources = argv[1];
	const filesystem::path config = argv[2];
	const string ext = ModLoaders::Extension();

	EsHost host;
	memset(&host, 0, sizeof(host));
	host.size = sizeof(EsHost);
	host.version = ES_API_VERSION;
	host.log = HostLog;
	host.hook_timing = HostHookTiming;
	host.planet_button = HostPlanetButton;

	// The table checks, without any library.
	{
		EsLoader api;
		memset(&api, 0, sizeof(api));
		api.size = sizeof(EsLoader);
		api.version = ES_API_VERSION;
		Check(ModLoaders::Incompatible(api).empty(), "a current table is compatible");
		api.version = ES_API_VERSION + 1;
		api.size = sizeof(EsLoader) + 16;
		Check(ModLoaders::Incompatible(api).empty(), "a newer version with a larger table is accepted");
		api.size = sizeof(EsLoader);
		api.version = ES_API_MIN_VERSION - 1;
		Check(!ModLoaders::Incompatible(api).empty(), "an older version is rejected");
		api.version = ES_API_VERSION;
		api.size = offsetof(EsLoader, shutdown);
		Check(!ModLoaders::Incompatible(api).empty(), "a table without shutdown is rejected");
	}
	Check(ModLoaders::Utf8(filesystem::path(u8"/x/\u00fc.so")) == "/x/\xc3\xbc.so", "paths convert to UTF-8");

	// Outside any call into a loader, nothing is running.
	Check(!loaders.RunningMod() && !loaders.HookOpen() && !loaders.EndHook(), "no mod runs outside a loader call");

	// Discovery: the resources folder, then the config folder.
	size_t count = loaders.Load({resources / "modloaders", config / "modloaders"}, host);
	for(const string &line : logs)
		printf("      log %s\n", line.c_str());
	Check(count == 4, "four compatible loaders open (got " + to_string(count) + ")");
	const auto &open = loaders.Get();
	Check(open.size() == 4 && open[0].name == "alpha (config)" && open[1].name == "newer"
		&& open[2].name == "unicode" && open[3].name == "beta",
		"they open in name order, the config folder's alpha replacing the resources' one");
	Check(open.size() == 4 && open[1].api.version == ES_API_VERSION + 1 && open[1].api.size == sizeof(EsLoader),
		"a newer loader is accepted, and the game uses only the part of its table it knows");
	const string unicodeFile = "y_\xc3\xbcn\xc3\xaf" + ext;
	Check(open.size() == 4 && open[2].file == unicodeFile && Logged(unicodeFile + ", mod API 9"),
		"a loader with a non-ASCII file name opens through its UTF-8 path");
	Check(Logged("replaces the one in"), "the replacement is logged");
	Check(Logged("init alpha (config)") && !Logged("init alpha (resources)"),
		"es_loader_init was called with a valid host table, and only for the replacing alpha");
	Check(Logged("b_old_version" + ext + " was not loaded: it was built for mod API"),
		"an older loader version is rejected");
	Check(Logged("shutdown old version"), "a rejected loader of a sane size is shut down");
	Check(Logged("c_small" + ext + " was not loaded: its loader table is"), "a too small loader table is rejected");
	Check(!Logged("shutdown small"), "a loader with a bad size is not called again");
	Check(Logged("d_declines" + ext + " was not loaded: its es_loader_init answered 3"),
		"a loader that declines is not loaded");
	Check(Logged("e_no_init" + ext + " was not loaded: it has no es_loader_init"),
		"a library without the entry point is rejected");
	Check(Logged("f_junk" + ext + " was not loaded: it could not be opened"), "a file that is no library is rejected");
	Check(!Logged("notes.txt"), "other files are ignored");
	// R1: alpha bracketed a mod while it started, and called for it.
	Check(Count("refused:init") == 1 && Count("init_mod:init") == 0,
		"a loader's calls for a mod while es_loader_init runs are refused");
	if(open.size() != 4)
	{
		printf("%d failure(s)\n", failures + 1);
		return 1;
	}
	TlStats *alpha = Stats(open[0]);
	TlStats *beta = Stats(open[3]);
	Check(alpha && beta && alpha->init_ok && beta->init_ok, "both loaders accepted the host table");
	if(!alpha || !beta)
		return 1;

	// load_mods gets the sources; count adds up.
	const char *paths[] = {"/game/resources", "/plugins/Some Plugin"};
	const char *names[] = {"", "Some Plugin"};
	loaders.Each([&](const EsLoader &api) {
		if(api.load_mods)
			api.load_mods(2, paths, names);
	});
	Check(alpha->load_calls == 1 && beta->load_calls == 1 && alpha->sources == 2
		&& string(beta->last_source_name) == "Some Plugin", "load_mods gets every source with its name");
	Check(loaders.ModCount() == 4, "the mod count adds up over the loaders");
	Check(Count("alpha_mod:on_load") == 1 && Count("beta_mod:on_load") == 1,
		"calls in on_load are for the mod the loader bracketed");
	Check(!loaders.RunningMod() && !loaders.HookOpen(), "no bracket is open after the calls return");

	// The engine step's hooks reach every loader, in order, and identify the mod.
	// Alpha misbehaves: its step leaves its bracket open, as if its mod threw.
	loaders.Each([](const EsLoader &api) {
		if(api.day)
			api.day(100);
		if(api.step)
			api.step(1);
	});
	Check(alpha->steps == 1 && beta->steps == 1 && alpha->days == 1, "every loader's day and step hooks run");
	Check(Count("alpha_mod:leak") == 1, "a call inside a bracket the mod never ends still acts for that mod");
	Check(Count("beta_mod:on_step") == 1, "the next loader's bracketed call is for its own mod");
	Check(Count("shared:on_day") == 1 && Count("refused:on_day") == 3, "a mod name another loader runs is refused");
	Check(Logged("already runs one of that name"), "the name clash is logged");
	Check(!loaders.RunningMod() && !loaders.HookOpen(), "a bracket left open is closed when the call returns");

	// B2: after alpha's unclosed bracket, unbracketed calls (from alpha's next
	// call, and from any other loader) act for no mod; an end without a begin
	// is ignored.
	auto step = [](const EsLoader &api) {
		if(api.step)
			api.step(2);
	};
	auto enter = [](const EsLoader &api) {
		if(api.enter)
			api.enter("Sol");
	};
	loaders.ForMod("alpha_mod", step);
	loaders.ForMod("alpha_mod", enter);
	Check(Count("alpha_mod:leak") == 2 && alpha->enters == 1, "alpha's step leaves a bracket open, then enter runs");
	Check(Count("refused:stray") == 1 && Count("alpha_mod:stray") == 0,
		"an unbracketed call after an unclosed bracket is refused, in the same loader's next call");
	Check(ignoredEnds == 1, "an end without a begin is ignored");
	Check(Count("alpha_mod:on_enter") == 1, "after the stray calls, the loader's bracketed call is for its mod");
	loaders.ForMod("alpha_mod", step);
	loaders.ForMod("beta_mod", [](const EsLoader &) { HostPlanetButton("cross"); });
	Check(Count("refused:cross") == 1, "an unclosed bracket in one loader does not reach another loader's call");
	loaders.Each(enter);
	Check(alpha->enters == 2 && beta->enters == 1 && Count("refused:stray") == 2 && Count("alpha_mod:stray") == 0
		&& Count("beta_mod:on_enter") == 1 && Count("unicode_mod:on_enter") == 1,
		"every loader's enter hook runs, each for its own mod");
	{
		// Directly: loader 0 opens a bracket and returns without ending it; loader 3
		// then calls with no bracket.
		const char *seen = "unset";
		size_t i = 0;
		loaders.Each([&](const EsLoader &) {
			if(i == 0)
				loaders.BeginHook("alpha_mod");
			else if(i == 3)
				seen = loaders.RunningMod();
			++i;
		});
		Check(!seen, "an unclosed bracket in one loader does not reach another loader's unbracketed call");
	}

	// Per-mod callbacks go to the loader that runs the mod.
	loaders.ForMod("beta_mod", [](const EsLoader &api) {
		if(api.planet_button)
			api.planet_button("beta_mod");
	});
	Check(alpha->planet_buttons == 0 && beta->planet_buttons == 1 && string(beta->last_target) == "beta_mod",
		"a mod's planet button reaches only its loader, with its name");
	loaders.ForMod("alpha_mod", [](const EsLoader &api) {
		if(api.panel)
			api.panel("alpha_mod", 2);
		// Entries the loader left null are skipped by the caller.
		if(api.number)
			api.number("alpha_mod", 1, 1);
	});
	Check(alpha->panels == 1 && beta->panels == 0, "a mod's panel click reaches only its loader");
	loaders.ForMod("nobody", [](const EsLoader &api) {
		if(api.planet_button)
			api.planet_button("nobody");
	});
	Check(alpha->planet_buttons == 1 && beta->planet_buttons == 2, "an unclaimed mod name goes to every loader");

	// Shutdown calls every loader once, then closes them.
	loaders.Unload();
	Check(Logged("shutdown alpha (config)") && Logged("shutdown beta") && loaders.Empty(),
		"every loader is shut down and closed");

	printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
