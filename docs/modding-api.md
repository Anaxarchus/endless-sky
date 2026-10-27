# The native modding API

Endless Sky can run code mods through **mod loaders**: small native libraries that the game finds at start and talks to through a plain C interface. The game knows nothing about any scripting language. It hands each loader a table of functions it may call (the *host table*), and the loader hands back a table of hooks the game calls as it runs. What a loader does with that, such as running scripts from plugins, is up to the loader.

Without a mod loader installed, the game plays exactly as it always has. The API adds no dependency to the game: loaders are opened with SDL, which the game already uses.

The whole contract is one header, [`source/modding/es_api.h`](../source/modding/es_api.h). It is pure C, compiles as C or C++, and is the single source of truth. A loader copies it unchanged.

## Where loaders are found

Once the game's data has loaded, before a pilot is loaded or the main menu is shown, the game looks in two folders:

1. `<resources>/modloaders/`, next to the game's `data/` folder;
2. `<config>/modloaders/`, in the user's config folder (the one that holds `plugins/` and `saves/`).

It looks for files with the platform's library extension: `.dylib` on macOS, `.so` on Linux and `.dll` on Windows (in any letter case on Windows). A file in the config folder replaces a file of the same name in the resources folder, as plugins do. Every compatible loader is opened, in the order of the file names.

For each file, the game:

1. opens it with `SDL_LoadObject`;
2. looks up `es_loader_init` (`ES_LOADER_INIT_SYMBOL`) with `SDL_LoadFunction`;
3. calls it with the host table and a zeroed `EsLoader` whose `size` is the number of bytes the game reserved;
4. rejects the loader, logs why and closes it if `es_loader_init` answers nonzero, if the loader's `version` is older than `ES_API_MIN_VERSION`, or if its `size` does not cover the API 9 table. A loader built for a newer API than the game's is accepted (see [Versions](#versions)).

The game opens the loaders once, at that point. The loaders load their mods later: each loader's `load_mods` runs once, at the first hook (the first engine step, landing, or a UI hook), because mods may use the world (the player, their saved data) as they load. The game then logs how many loaders and mods it loaded. With no loaders it logs nothing at all.

Paths are passed to SDL and to the loaders as UTF-8 on every platform, so folders with non-ASCII names work.

## The loader contract

A loader exports one function:

```c
#include "es_api.h"

ES_LOADER_EXPORT int32_t ES_CALL es_loader_init(const EsHost *host, EsLoader *loader);
```

`ES_LOADER_EXPORT` marks it for export: `__declspec(dllexport)` on Windows, default visibility elsewhere, and `extern "C"` when the loader is written in C++. `ES_CALL` is the calling convention of `es_loader_init` and of every function in both tables: `__cdecl` on 32-bit Windows, the platform's C default elsewhere.

On Windows, a loader should link its own dependencies statically (or they must sit next to the game's executable): Windows does not search the `modloaders` folder for DLLs a loader needs. The function checks the host (its `version`, and `ES_HOST_HAS(host, field)` for any field it needs), fills in `loader` and answers 0.

`EsLoader` holds:

| Field | Meaning |
|---|---|
| `size`, `version` | The bytes the loader filled (at most the game's `size`) and the `ES_API_VERSION` it was built with. |
| `name` | The loader's name and version, for the log. Must stay valid until `shutdown` returns. |
| `load_mods(count, paths, names)` | Called once, with the enabled sources in load order: the game's resources folder first (name `""`), then every enabled plugin's folder and name. Paths are UTF-8. A zipped plugin is passed as-is, as the path of its `.zip` file, and a loader may skip such sources. |
| `count()` | How many mods are currently loaded and running, not counting mods that failed to load or that the loader has since disabled. The game asks every engine step. |
| `day(day)`, `enter(system)`, `step(step)` | An engine step's hooks, in that order: `day` for each new day that passed in play, `enter` when the player is in a new system (also on landing), then `step`. |
| `planet_button(mod)`, `panel(mod, button)`, `can_outfitter(mod, outfit)`, `outfitter(mod, outfit)`, `confirm(mod, token)`, `panel_row(mod, row)`, `panel_header(mod, column)`, `number(mod, token, value)` | The player acted on something a mod put up. The game sends these only to the loader that runs `mod`, with the mod's name. |
| `shutdown()` | End every mod. The library is closed right after. |

Any entry may be left null. The game checks every entry before calling it.

### Which files a loader loads

The game does not look for scripts or anything else on a loader's behalf. It only passes the source folders, and each loader decides what to load from them. By convention, a loader keeps its mods in a folder of its own in each plugin, for example `<plugin>/scripts/*.js`. Because the sources are the enabled plugins, enabling or disabling a plugin in the game's plugin list enables or disables its mods, like any data plugin. A later source overrides an earlier one, and loaders should treat their mods the same way.

## Mod identity

The game, not the loader, decides which mod a call comes from. A loader brackets every call into one of its mods' hooks:

```c
host->hook_timing("my_mod", "on_step", 1);
/* run the mod's on_step */
host->hook_timing("my_mod", "on_step", 0);
```

The host calls marked *Mod-scoped* in `es_api.h` (map overlays, planet and outfitter buttons, panels, dialogs, a mod's saved data, jobs, buying ships) take no mod argument. They act for the mod named by the last bracket's begin, and do nothing outside a bracket. The game also uses the brackets to time each mod's hooks, to report slow hooks, and to remember which loader runs which mod. If two loaders run mods of the same name, the first loader to run one keeps the name. The second loader's calls for that name are refused, and the clash is logged.

A bracket lasts at most until the game's call into the loader returns. If a mod fails and the loader returns without the end bracket, the game closes the bracket itself, so the mod's identity never leaks into a later call or another loader. An end without a begin is ignored. While `es_loader_init` runs, the loader runs no mod, so mod-scoped calls do nothing there.

## Pilots

When the player makes or loads another pilot (or another save of the same pilot), the game drops everything the mods put up for the last one: map overlays and side-panel sections, planet and outfitter buttons, the open panel, tagged ships, kills and losses. It does not run the days in between. The loaders see a fresh world, starting with an `enter`. Mods are loaded once per run of the game (`load_mods`, and with it a mod's `on_load`), not once per pilot, so a mod should keep per-pilot state in its saved data (`data` and `set_data`), which is saved and loaded with the pilot.

The game keeps what mods store with a pilot in `source/modding/ModState`, not in `PlayerInfo`. That covers each mod's saved data, the shipyard stock mods track, and the notes shown for models that are out of stock. The save file holds it in `mod data` and `ship stock` blocks, and a pilot saved without mods has neither. Stock tracking is switched off whenever a pilot is loaded.

`ship_in_stock` always answers 0 unless the game includes used-hull support, which the fork keeps as an optional patch (`patches/used-hulls.patch`).

## Threads, timing and ownership

- Everything runs on the game's main thread. The game calls the loader only from inside its own hooks: the engine step (while the calculation thread is paused), the planet screen, the map, the outfitter and mod panels. The loader calls the host table only from inside those calls, and from `es_loader_init`, `load_mods` and `shutdown`. Outside a hook there is no world: queries answer 0 or `""`, and writes do nothing.
- A string the host returns stays valid until the next call into the host. Copy it to keep it.
- A string the game passes to the loader is valid for that call only.
- Nothing is freed across the boundary.
- Ships in the current system are named by their index into this step's snapshot. An index means something only within one hook call. Booleans are `int32_t` 0 or 1.

## Versions

`ES_API_VERSION` is the mod API number (currently 9). Both tables only grow by appending fields at the end. Existing fields never change, move or go away, so an older table is a prefix of a newer one. Each table's `size` says how much of it exists. Check `ES_HOST_HAS(host, field)` or `ES_LOADER_HAS(loader, field)` before using a field that is newer than the version you required.

- The game accepts any loader whose `version` is at least `ES_API_MIN_VERSION`, including versions newer than the game's. It uses `min(loader->size, sizeof(EsLoader))` bytes of the loader's table and ignores fields it does not know.
- The loader is responsible for checking `host->version` and `ES_HOST_HAS` for everything it needs, and for answering nonzero from `es_loader_init` when the game is too old for it. It must not write more of `EsLoader` than the `size` the game passed in.
- The game rejects a loader whose `version` is below `ES_API_MIN_VERSION`, or whose `size` does not cover the API 9 table.

### Changing the API

To add a call:

1. Append a field to the end of `EsHost` (or `EsLoader`) in `es_api.h`, with a comment that says what it does and which API version added it.
2. Raise `ES_API_VERSION` if the change is part of a new API version.
3. Implement the call in `source/ModHarness.cpp` as an `es_<name>` function and set it in `Host()`.
4. If the call changes something a run of hooks caches (the player's fleet, the galaxy, a system's defense fleets, shipyards), clear that cache in the call.

Never reorder, retype or remove a field. Loaders built against an older header rely on the layout.

## Testing

`tests/modloader/run.sh` builds a minimal C loader (`test_loader.c`) in several variants, plus a host test (`test_host.cpp`) that runs the game's discovery, dispatch and mod identity code (`source/modding/ModLoaders.cpp`) against them. SDL's shared object calls are stubbed over `dlopen`, so no game libraries are needed, only a C and a C++ compiler and SDL2's headers:

```sh
SDL_INCLUDE=<folder holding SDL2/SDL.h> sh tests/modloader/run.sh
```

It runs on Linux (`.so`, linked with `-ldl`) and on macOS (`.dylib`, no `-ldl`; for example `SDL_INCLUDE="$(brew --prefix)/include"`). It checks that:

- `es_loader_init` receives a valid host table;
- the loaders' hooks run, and per-mod callbacks reach the right loader;
- old-version, too-small, declined and broken loaders are rejected, while a loader built for a newer API is accepted and only the part of its table the game knows is used;
- a loader with a non-ASCII file name is opened through its UTF-8 path;
- a config folder loader replaces a resources folder loader of the same name;
- mod identity holds: calls during `es_loader_init` act for no mod, a bracket a loader leaves open does not reach its next call or another loader, an end without a begin is ignored, and a mod name another loader already runs is refused;
- `ModHarness.cpp` sets every field of the host table.

It then builds two deliberately broken copies of `ModLoaders` (one whose `RunningMod` trusts the last bracket, one that keeps brackets across calls) and fails unless the tests catch both.

The game's condition `"mod loaders"` holds the number of open loaders, set when they are opened after the data loads. `"mods loaded"` holds the number of mods they loaded: 0 until the first hook has them load their mods, then updated every engine step.
