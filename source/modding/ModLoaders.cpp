/* ModLoaders.cpp
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

#include "ModLoaders.h"

#include "../Logger.h"
#include "../SDL.h"

#include <cctype>
#include <cstring>
#include <system_error>

using namespace std;

namespace {
#ifdef ES_USE_SDL3
	using SharedObject = SDL_SharedObject;
#else
	using SharedObject = void;
#endif

	// Whether a file has the loader extension; on Windows, in any case (".DLL").
	bool IsLoaderFile(const filesystem::path &path, const string &extension)
	{
		string found = ModLoaders::Utf8(path.extension());
#ifdef _WIN32
		for(char &c : found)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
#endif
		return found == extension;
	}

	void Close(void *handle)
	{
		if(handle)
			SDL_UnloadObject(static_cast<SharedObject *>(handle));
	}
}



const char *ModLoaders::Extension()
{
#if defined(_WIN32)
	return ".dll";
#elif defined(__APPLE__)
	return ".dylib";
#else
	return ".so";
#endif
}



string ModLoaders::Incompatible(const EsLoader &api)
{
	if(!ES_LOADER_HAS(&api, shutdown))
		return "its loader table is " + to_string(api.size) + " bytes, smaller than mod API "
			+ to_string(ES_API_MIN_VERSION) + "'s";
	// A newer loader is welcome: it checked the host's version itself, and the
	// game uses only the fields it knows.
	if(api.version < ES_API_MIN_VERSION)
		return "it was built for mod API " + to_string(api.version) + ", and this game needs "
			+ to_string(ES_API_MIN_VERSION) + " or later";
	return string();
}



string ModLoaders::Utf8(const filesystem::path &path)
{
	const u8string text = path.u8string();
	return string(text.begin(), text.end());
}



size_t ModLoaders::Load(const vector<filesystem::path> &folders, const EsHost &host)
{
	// By file name, so a later folder's loader replaces an earlier one's, as
	// plugins do, and they open in name order.
	map<string, filesystem::path> files;
	const string extension = Extension();
	for(const filesystem::path &folder : folders)
	{
		error_code error;
		if(!filesystem::is_directory(folder, error))
			continue;
		for(const filesystem::directory_entry &entry : filesystem::directory_iterator(folder, error))
			if(entry.is_regular_file(error) && IsLoaderFile(entry.path(), extension))
			{
				string file = Utf8(entry.path().filename());
				auto it = files.find(file);
				if(it != files.end())
					Logger::Log("Mod loader " + file + " in " + Utf8(folder) + " replaces the one in "
						+ Utf8(it->second.parent_path()) + ".", Logger::Level::INFO);
				files[file] = entry.path();
			}
	}
	loaders.reserve(loaders.size() + files.size());
	for(const auto &[file, path] : files)
		LoadOne(file, path, host);
	return loaders.size();
}



void ModLoaders::LoadOne(const string &file, const filesystem::path &path, const EsHost &host)
{
	auto reject = [&file](const string &why) {
		Logger::Log("Mod loader " + file + " was not loaded: " + why + ".", Logger::Level::WARNING);
	};

	SharedObject *object = SDL_LoadObject(Utf8(path).c_str());
	if(!object)
	{
		reject(string("it could not be opened (") + SDL_GetError() + ")");
		return;
	}
	auto symbol = SDL_LoadFunction(object, ES_LOADER_INIT_SYMBOL);
	if(!symbol)
	{
		reject("it has no " ES_LOADER_INIT_SYMBOL " function");
		Close(object);
		return;
	}
	auto init = reinterpret_cast<EsLoaderInitFn>(symbol);

	Loader loader;
	loader.file = file;
	loader.handle = object;
	memset(&loader.api, 0, sizeof(loader.api));
	loader.api.size = sizeof(EsLoader);

	// While it starts, the loader owns no mod: its brackets open no mod's calls.
	int32_t result = 0;
	{
		CallScope scope(*this, NONE);
		result = init(&host, &loader.api);
	}
	if(result)
	{
		reject("its " ES_LOADER_INIT_SYMBOL " answered " + to_string(result));
		Close(object);
		return;
	}
	string why = Incompatible(loader.api);
	if(!why.empty())
	{
		reject(why);
		// A table of a sane size has a shutdown the loader expects to be called.
		if(ES_LOADER_HAS(&loader.api, shutdown) && loader.api.shutdown)
		{
			CallScope scope(*this, NONE);
			loader.api.shutdown();
		}
		Close(object);
		return;
	}
	// The game uses min(size, sizeof(EsLoader)): fields of a newer API are
	// ignored, and whatever lies past the size the loader filled is not its.
	if(loader.api.size > sizeof(EsLoader))
		loader.api.size = sizeof(EsLoader);
	else if(loader.api.size < sizeof(EsLoader))
		memset(reinterpret_cast<char *>(&loader.api) + loader.api.size, 0, sizeof(EsLoader) - loader.api.size);
	loader.name = loader.api.name && *loader.api.name ? loader.api.name : file;
	Logger::Log("Mod loader \"" + loader.name + "\" (" + Utf8(path) + ", mod API "
		+ to_string(loader.api.version) + ") loaded.", Logger::Level::INFO);
	loaders.push_back(std::move(loader));
}



void ModLoaders::Unload()
{
	for(size_t i = 0; i < loaders.size(); ++i)
		if(loaders[i].api.shutdown)
		{
			CallScope scope(*this, i);
			loaders[i].api.shutdown();
		}
	// Shut every loader down before closing any: one may use another.
	for(const Loader &loader : loaders)
		Close(loader.handle);
	loaders.clear();
	owners.clear();
	conflicts.clear();
	current = NONE;
	bracket = Bracket();
}



bool ModLoaders::Empty() const
{
	return loaders.empty();
}



size_t ModLoaders::Count() const
{
	return loaders.size();
}



const vector<ModLoaders::Loader> &ModLoaders::Get() const
{
	return loaders;
}



int64_t ModLoaders::ModCount()
{
	int64_t count = 0;
	Each([&count](const EsLoader &api) {
		if(api.count)
			count += api.count();
	});
	return count;
}



bool ModLoaders::BeginHook(const string &mod)
{
	// A new begin replaces a bracket the loader left open in this call.
	bracket = Bracket();
	bracket.open = true;
	bracket.loader = current;
	bracket.mod = mod;
	if(current == NONE || mod.empty())
		return false;
	auto [it, inserted] = owners.emplace(mod, current);
	bracket.owned = inserted || it->second == current;
	if(!bracket.owned && conflicts.insert(mod).second)
		Logger::Log("Mod loader \"" + loaders[current].name + "\" runs a mod named \"" + mod
			+ "\", but loader \"" + loaders[it->second].name + "\" already runs one of that name;"
			" its calls for that mod are refused.", Logger::Level::WARNING);
	return bracket.owned;
}



bool ModLoaders::EndHook()
{
	if(!HookOpen())
		return false;
	bracket = Bracket();
	return true;
}



bool ModLoaders::HookOpen() const
{
	return bracket.open && bracket.loader == current;
}



string ModLoaders::TimedName() const
{
	if(!HookOpen())
		return string();
	if(bracket.owned || current == NONE)
		return bracket.mod;
	return bracket.mod + " (" + loaders[current].name + ")";
}



const char *ModLoaders::RunningMod() const
{
	if(!HookOpen() || !bracket.owned || current == NONE)
		return nullptr;
	auto it = owners.find(bracket.mod);
	if(it == owners.end() || it->second != current)
		return nullptr;
	return bracket.mod.c_str();
}
