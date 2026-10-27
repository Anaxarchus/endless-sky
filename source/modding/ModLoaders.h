/* ModLoaders.h
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

#include "es_api.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>



// The mod loaders the game found: shared libraries that implement es_api.h,
// opened through SDL. It finds them, checks that they are compatible, calls
// into them, and remembers which loader runs which mod. It knows nothing of
// the game's world; ModHarness does the rest.
class ModLoaders {
public:
	struct Loader {
		// The library's file name, and the name it gave itself.
		std::string file;
		std::string name;
		// The SDL shared object.
		void *handle = nullptr;
		EsLoader api = {};
	};


public:
	ModLoaders() = default;
	ModLoaders(const ModLoaders &) = delete;
	ModLoaders &operator=(const ModLoaders &) = delete;
	// Call Unload first: a loader is not shut down when this object goes away
	// (at exit, the game it would call may be gone), only left open.
	~ModLoaders() = default;

	// The file extension of a loader on this platform: ".dylib", ".so" or ".dll".
	static const char *Extension();
	// Why a loader's filled-in table cannot be used, or "" when it can. A table
	// from a newer API version is fine: the game uses the part it knows.
	static std::string Incompatible(const EsLoader &api);
	// A path as UTF-8, the encoding SDL and the loaders take on every platform.
	static std::string Utf8(const std::filesystem::path &path);

	// Open every loader in these folders, in the order of their file names. A
	// file in a later folder replaces one of the same name in an earlier folder.
	// Each loader is handed `host`; one that fails or is incompatible is logged
	// and closed. Returns how many loaders are open.
	size_t Load(const std::vector<std::filesystem::path> &folders, const EsHost &host);
	// Shut down every loader and close it.
	void Unload();

	bool Empty() const;
	size_t Count() const;
	const std::vector<Loader> &Get() const;
	// How many mods the loaders have loaded, together.
	int64_t ModCount();

	// Call fn(const EsLoader &) for each loader, in order.
	template <class Fn>
	void Each(Fn fn);
	// Call fn for the loader that runs `mod`, or for every loader if none has
	// claimed that name yet.
	template <class Fn>
	void ForMod(const std::string &mod, Fn fn);
	// Every call into a loader (these two, Load and Unload) knows which loader
	// it calls, and starts and ends with no hook bracket open, so a bracket a
	// loader leaves open (its hook failed before the end) never reaches another
	// call or another loader.

	// hook_timing's bookkeeping. The loader being called began a hook for
	// `mod`: answers whether the mod is that loader's. The first loader to run
	// a mod name keeps it, and a clash is logged once; while a loader starts
	// (es_loader_init) it owns no mod at all.
	bool BeginHook(const std::string &mod);
	// The loader being called ended its hook. Answers false, changing nothing,
	// when it has no bracket open (an end without a begin).
	bool EndHook();
	// Whether the loader being called has a bracket open, and the name its hook
	// is timed under: the mod's, with the loader's after it when the name was refused.
	bool HookOpen() const;
	std::string TimedName() const;
	// The mod a mod-scoped call acts for: the bracketed mod, when the loader
	// being called opened the bracket and owns the mod. Null otherwise.
	const char *RunningMod() const;


private:
	static constexpr size_t NONE = static_cast<size_t>(-1);

	// The hook a loader has open, if any.
	struct Bracket {
		bool open = false;
		size_t loader = NONE;
		std::string mod;
		bool owned = false;
	};

	// One call into a loader (`index`, or NONE for one that is not open yet):
	// it is the current loader, with no bracket, until the call returns; then
	// the outer call's state comes back.
	class CallScope {
	public:
		CallScope(ModLoaders &loaders, size_t index);
		~CallScope();
		CallScope(const CallScope &) = delete;
		CallScope &operator=(const CallScope &) = delete;

	private:
		ModLoaders &loaders;
		size_t outerIndex;
		Bracket outerBracket;
	};


private:
	void LoadOne(const std::string &file, const std::filesystem::path &path, const EsHost &host);


private:
	std::vector<Loader> loaders;
	// Which loader runs each mod, by index.
	std::map<std::string, size_t> owners;
	std::set<std::string> conflicts;
	// The loader being called, by index.
	size_t current = NONE;
	Bracket bracket;
};



inline ModLoaders::CallScope::CallScope(ModLoaders &loaders, size_t index)
	: loaders(loaders), outerIndex(loaders.current), outerBracket(std::move(loaders.bracket))
{
	loaders.current = index;
	loaders.bracket = Bracket();
}



inline ModLoaders::CallScope::~CallScope()
{
	loaders.current = outerIndex;
	loaders.bracket = std::move(outerBracket);
}



template <class Fn>
void ModLoaders::Each(Fn fn)
{
	for(size_t i = 0; i < loaders.size(); ++i)
	{
		CallScope scope(*this, i);
		fn(loaders[i].api);
	}
}



template <class Fn>
void ModLoaders::ForMod(const std::string &mod, Fn fn)
{
	auto it = owners.find(mod);
	if(it == owners.end() || it->second >= loaders.size())
	{
		Each(fn);
		return;
	}
	CallScope scope(*this, it->second);
	fn(loaders[it->second].api);
}
