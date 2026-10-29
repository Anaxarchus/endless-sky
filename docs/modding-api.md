# The native modding API

Endless Sky can run code mods through **mod loaders**: small native libraries that the game finds at start and talks to through a plain C interface. The game knows nothing about any scripting language. It hands each loader a table of functions it may call (the *host table*), and the loader hands back a table of hooks the game calls as it runs. What a loader does with that, such as running scripts from plugins, is up to the loader.

Without a mod loader installed, the game plays exactly as it always has. The API adds no dependency to the game: loaders are opened with SDL, which the game already uses.

The whole contract is one header, [`source/modding/es_api.h`](../source/modding/es_api.h). It is pure C, compiles as C or C++, and is the single source of truth. A loader copies it unchanged.

## Where loaders are found

Once the game's data has loaded, before a pilot is loaded or the main menu is shown, the game looks in two folders:

1. `<resources>/modloaders/`, next to the game's `data/` folder;
2. `<config>/modloaders/`, in the user's config folder (the one that holds `plugins/` and `saves/`).

It looks for files with the platform's library extension: `.dylib` on macOS, `.so` on Linux and `.dll` on Windows (in any letter case on Windows). A file in the config folder replaces a file of the same name in the resources folder, as plugins do. Every compatible loader is opened, in the order of the file names.

In Argosy, the config folder is its own, named `argosy` rather than Endless Sky's `endless-sky`, so Argosy and an installed Endless Sky share no saves, preferences, plugins or loaders (`--config <folder>` still overrides it):

| | Config folder | Resources `modloaders/` in a release |
|---|---|---|
| macOS | `~/Library/Application Support/argosy/` | `Endless Sky.app/Contents/Resources/modloaders/` |
| Linux | `~/.local/share/argosy/` (or `$XDG_DATA_HOME/argosy/`) | inside the AppImage, next to `data/` |
| Windows | `%APPDATA%\argosy\` | next to `data/` in the install folder |

The game copies nothing from the old folder. To bring saves over, copy `saves/` from the Endless Sky folder (`~/Library/Application Support/endless-sky/`, `~/.local/share/endless-sky/` or `%APPDATA%\endless-sky\`) into the Argosy one; plugins can be copied the same way.

Argosy's releases bundle the Dado loader ([Anaxarchus/es-modloader](https://github.com/Anaxarchus/es-modloader)) in the resources `modloaders/` folder (see [releasing.md](releasing.md)). To try another version, or another loader, put it in `<config>/modloaders/`: a file with the bundled one's name replaces it, and any other loader is opened beside it.

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
| `save()` | (API 10) The game is about to write the pilot. What mods write with `set_data` now goes into that file. See [Saving and loading](#saving-and-loading-mod-api-10). |
| `pilot_loaded()` | (API 10) Another pilot was made or loaded, or the mods start with one. Called once, before any `day`, `enter` or `step` for it. |
| `notification(mod, name, payload, sender)` | (API 10) Deliver a notification to mod `mod`, which listens for it. See [The notification bus](#the-notification-bus-mod-api-10). |

Any entry may be left null. The game checks every entry before calling it, and never calls an entry beyond the `size` the loader filled: a loader built for API 9 is never asked for `save`, `pilot_loaded` or `notification`.

### Which files a loader loads

The game does not look for scripts or anything else on a loader's behalf. It only passes the source folders, and each loader decides what to load from them. By convention, a loader keeps its mods in a folder of its own in each plugin, for example `<plugin>/scripts/*.js`. Because the sources are the enabled plugins, enabling or disabling a plugin in the game's plugin list enables or disables its mods, like any data plugin. A later source overrides an earlier one, and loaders should treat their mods the same way.

## Mod identity

The game, not the loader, decides which mod a call comes from. A loader brackets every call into one of its mods' hooks:

```c
host->hook_timing("my_mod", "on_step", 1);
/* run the mod's on_step */
host->hook_timing("my_mod", "on_step", 0);
```

The host calls marked *Mod-scoped* in `es_api.h` (map overlays, planet and outfitter buttons, panels, dialogs, a mod's saved data, jobs, buying, parking and removing ships, notifications, timing sections) take no mod argument. They act for the mod named by the last bracket's begin, and do nothing outside a bracket. The game also uses the brackets to time each mod's hooks, to report slow hooks, and to remember which loader runs which mod. If two loaders run mods of the same name, the first loader to run one keeps the name. The second loader's calls for that name are refused, and the clash is logged.

A bracket lasts at most until the game's call into the loader returns. If a mod fails and the loader returns without the end bracket, the game closes the bracket itself, so the mod's identity never leaks into a later call or another loader. An end without a begin is ignored. While `es_loader_init` runs, the loader runs no mod, so mod-scoped calls do nothing there.

## Pilots

When the player makes or loads another pilot (or another save of the same pilot), the game drops everything the mods put up for the last one: map overlays and side-panel sections, planet and outfitter buttons, the open panel, tagged ships, kills and losses, and the queued notifications. It does not run the days in between. The loaders see a fresh world, starting with `pilot_loaded` (API 10) and then an `enter`. Mods are loaded once per run of the game (`load_mods`, and with it a mod's `on_load`), not once per pilot, so a mod should keep per-pilot state in its saved data (`data` and `set_data`), which is saved and loaded with the pilot.

The game keeps what mods store with a pilot in `source/modding/ModState`, not in `PlayerInfo`. That covers each mod's saved data, the shipyard and outfitter stock mods track, the notes shown for models that are out of stock, and (API 11) the outfits and ship models mods defined. The save file holds it in `mod data`, `ship stock`, `outfit stock` and `runtime definitions` blocks, and a pilot saved without mods has none of them. Stock tracking (both kinds) is switched off whenever a pilot is loaded.

`ship_in_stock` always answers 0 unless the game includes used-hull support, which the fork keeps as an optional patch (`patches/used-hulls.patch`).

## Threads, timing and ownership

- Everything runs on the game's main thread. The game calls the loader only from inside its own hooks: the engine step (while the calculation thread is paused), the planet screen, the map, the outfitter, mod panels, and saving the pilot. The loader calls the host table only from inside those calls, and from `es_loader_init`, `load_mods` and `shutdown`. Outside a hook there is no world: queries answer 0 or `""`, and writes do nothing.
- A string the host returns stays valid until the next call into the host. Copy it to keep it.
- A string the game passes to the loader is valid for that call only.
- Nothing is freed across the boundary.
- Ships in the current system are named by their index into this step's snapshot. An index means something only within one hook call. Booleans are `int32_t` 0 or 1.

## Versions

`ES_API_VERSION` is the mod API number (currently 11; `ES_API_MIN_VERSION` is still 9). Both tables only grow by appending fields at the end. Existing fields never change, move or go away, so an older table is a prefix of a newer one. Each table's `size` says how much of it exists. Check `ES_HOST_HAS(host, field)` or `ES_LOADER_HAS(loader, field)` before using a field that is newer than the version you required.

- The game accepts any loader whose `version` is at least `ES_API_MIN_VERSION`, including versions newer than the game's. It uses `min(loader->size, sizeof(EsLoader))` bytes of the loader's table and ignores fields it does not know.
- The loader is responsible for checking `host->version` and `ES_HOST_HAS` for everything it needs, and for answering nonzero from `es_loader_init` when the game is too old for it. It must not write more of `EsLoader` than the `size` the game passed in.
- The game rejects a loader whose `version` is below `ES_API_MIN_VERSION`, or whose `size` does not cover the API 9 table.
- `api_has(feature)` and `api_features()` (API 10) name what the game offers, so a loader or a mod can ask for a feature instead of a version: `"call:<field>"` for each `EsHost` call added since API 9, `"on_<hook>"` for each loader entry the game calls since API 9, and short names (`"notify"`, `"trade_price"`, `"outfit_stock"`, `"ship_parking"`, `"cargo"`, `"timing_section"`, `"ship_fractions"`; API 11: `"runtime_definitions"`, `"outfit_transfer"`, `"ship_reactivate"`). They are registered in one place, `Features()` in `ModHarness.cpp`, and `tests/modloader/run.sh` fails when a call added since API 9 is not registered there.

### Changing the API

To add a call:

1. Append a field to the end of `EsHost` (or `EsLoader`) in `es_api.h`, with a comment that says what it does and which API version added it.
2. Raise `ES_API_VERSION` if the change is part of a new API version.
3. Implement the call in `source/ModHarness.cpp` as an `es_<name>` function, set it in `Host()`, and register it in `Features()` as `"call:<name>"` (and a feature name, if it adds one).
4. If the call changes something a run of hooks caches (the player's fleet, the galaxy, a system's defense fleets, shipyards), clear that cache in the call.

Never reorder, retype or remove a field. Loaders built against an older header rely on the layout.

## Mod API 10

### Saving and loading (mod API 10)

The game calls the loaders' `save` first thing in `PlayerInfo::Save(DataWriter &)`, the one function every save goes through, before it writes a line: so what the mods write with `set_data` (or the conditions, or stock) lands in that same file, and `ModState::Save` writes it with the rest. That covers every save: taking off (`PlanetPanel::TakeOff`), quitting while landed (`main.cpp`), opening the load panel while landed (`LoadPanel` saves first, so a snapshot or a switch to another pilot starts from the current state), the autosave `PlayerInfo::Autosave` writes when the player accepts a mission marked `autosave` (`~autosave.txt`; this can happen in flight), the `~~previous-spaceport.txt` backup, and a conversation's snapshot (`StartTransaction`: a save during the conversation writes that moment, so the mods' data is flushed then too). Because the game writes a backup in some saves, `save` may come twice for one save; mods should make it cheap and idempotent (flush what is dirty).

It runs as a hook with a world but no UI and no ship snapshot, also in flight (`ship_count` is 0): queries, `set_data`, conditions and stock work; panels, dialogs, `buy_ship`, `ship_park` and `ship_remove` do not. It is not called from inside another hook (a mod's own call may make the game save, e.g. through an event), before the mods have started, or for a pilot no hook has run for yet (the mods still hold the last pilot's state in memory, so flushing it would write it into the wrong pilot). What mods post with `notify` in `save` is delivered at the next drain point, after the file is written; what they post in the save made just before the player loads another pilot (the load panel saves first) or quits is lost, because the queue is cleared with the pilot or at shutdown.

`pilot_loaded` runs at the point where the harness already sees a new pilot (`ModState::Generation()` moved on: `PlayerInfo::Clear` bumps it for every new or loaded pilot), in the first hook that runs for it, after the harness has dropped what the mods put up for the last pilot. The order is:

1. `load_mods` (once per run of the game, so each mod's `on_load`), then the notifications posted there are delivered;
2. `pilot_loaded` (each mod's `on_pilot_loaded`), then the notifications posted there;
3. then the hook that found the new pilot: `enter` on landing (a loaded pilot starts on a planet) or the first engine step (no `day` for the days in between, then `enter`, then `step`), or a UI hook.

So `on_pilot_loaded` always sees the new pilot's saved data, and always comes before any `on_enter`, `on_day` or `on_step` for it.

### Trade and cargo

`trade_price(system, commodity)` is `System::Trade`: what the trading panel charges there now. `trade_base_price` is the price before supply and demand move it: `System::BasePrice`, a five-line accessor the fork adds to `System` for it (the base is a private field of `System::Price`; working it back out of the price and the supply was wrong until the next economy step after an event changed the system's trade, because `Price::SetBase` does not update the price). The engine keeps no price from before events: an event that changes a system's `trade` changes the base too, and `GameData`'s pre-event copy of the systems is private. Both answer 0 when the system has no market for the commodity and -1 for an unknown system or commodity. `commodity_count` and `commodity_name` list `GameData::Commodities()`, the standard commodities in the trading panel's order. `cargo(commodity)` adds the pooled cargo (`PlayerInfo::Cargo`, where it is while landed) and the holds of the player's ships in the player's system that are not parked (where it is in flight); 0 for an unknown name.

### Parking and removing ships

`ship_park(uuid, on)`, `ship_remove(uuid)` and `ship_parked(uuid)` name the player's ships by UUID, as the API 5 fleet calls do. `ship_park` and `ship_remove` are mod-scoped and work only in a hook the player started (a button, panel, row, number dialog, confirm or outfitter button), while landed, like the player's own park and disown buttons (`PlayerInfoPanel`, `ShipInfoPanel`):

- `ship_park` goes through `PlayerInfo::ParkShip`, which refreshes the cargo capacities and the flagship. A parked ship may be at another planet, and unparking one there is allowed, as in the player info panel. The flagship and disabled ships cannot be parked, and it is refused in the outfitter's hooks. Mission cargo is no more stranded than with the player's own buttons: while landed, the cargo of the ships at the planet is pooled with the player (`PoolCargo`), so parking one there moves nothing, and a ship elsewhere keeps its hold whether parked or not.
- `ship_remove` goes through `PlayerInfo::DisownShip` (no money, no stock, gifted-ship records forgotten), then refreshes the cargo capacities and the harness's fleet cache, and logs it. A ship whose hold has a mission's cargo or passengers (a ship parked at another planet keeps its own hold) is refused, so no mission loses them silently. The flagship goes only when another ship landed with the player (same system and planet, not parked, able to be a flagship) can take its place. It is refused in the outfitter's hooks, whose panel holds pointers to its selected ships; the removed ship is also kept alive until the next engine step, so a panel that still points at it never points at freed memory.

### Outfitter stock

As shipyard stock (mod API 9), but for outfits, kept in `ModState` (`OutfitStock`, `SetOutfitStock`, saved as an `outfit stock` block in the same shape as `ship stock`, and validated with it after loading). `outfit_stock_on` switches it on for the session; it is not saved, and loading a pilot turns it off, so the outfitters are upstream's unless a mod turns it on. While it is on, an outfitter shows `in stock: N` (or `out of stock`) on each outfit it sells that a mod tracks there, and sells no more than that. The outfits the player sold there (the outfitter's own "in stock", bought back until takeoff) are sold first and do not count against it; outfits the player sells are not added to it. It applies to buying and installing, buying into cargo or storage, and the automatic ammunition refill. Maps and licenses are never tracked (`ModState::OutfitTrackable`): the outfitter sells them as the knowledge they give, without a count, so `set_outfit_stock` ignores them.

The enforcement is two helpers in `ModState` (`OutfitsAvailable`, which caps a purchase, and `OutfitsBought`, which takes it off the count), called from the few places in `OutfitterPanel.cpp` where it buys from the shop: 18 added lines and no changed ones in that upstream file, against about 100 for the shipyard's stock.

### The notification bus (mod API 10)

`notify(name, payload)`, `listen(pattern)` and `unlisten(pattern)` (mod-scoped) and the loader entry `notification(mod, name, payload, sender)` let mods talk to each other, also across loaders. The game owns the bus (`source/modding/ModBus`): the sender is the bracketed mod (the game's record, never the loader's word), names are 1 to 128 bytes of `A-Z a-z 0-9 . _ : / -`, payloads are opaque text of up to 64 KiB, and patterns are exact names or prefixes ending in `*`.

Posts are queued, never delivered synchronously. The harness drains the queue after an engine step's hooks, after each hook run from the UI, after landing, after `pilot_loaded`, and once after `load_mods`. Each notification goes, oldest first, to every mod that listens for it except its sender, in the order of the mods' names, through `ForMod(receiver)`: so only the receiver's loader is called, and it brackets the delivery as `hook_timing(receiver, "on_notification", 1/0)`, which charges its time (and its loader's watchdog) to the receiver. Posts made during a drain join it, for up to 4 rounds or about 1024 deliveries; the rest waits for the next drain, and that is logged. The queue holds 1024 notifications and 4 MiB of names and payloads (`ModBus::MAX_QUEUED_BYTES`), counting the part a cut-off drain holds back for the next one, and a mod may post 256 between drains; past that a post is dropped, `notify` answers 0, and it is logged (problems are logged at most 20 times a minute). The queue is not saved, and another pilot empties it; who listens lasts until `unlisten` or shutdown, across pilots.

### Timing sections and fractions

`timing_section(name)` splits the open bracket's time into named sections; the summary at exit then has a row per mod, hook and section. A name with a control character is ignored, and a mod's names past its 32nd are timed together as `(other)` (`ModLoaders::SectionName`), so the table cannot grow without end. `ship_energy_fraction` and `ship_fuel_fraction` answer 0..1, and 0 for a ship with no capacity for it, where `ship_energy` and `ship_fuel` answer absolute amounts (`Ship::EnergyLevel` and `FuelLevel`).

## Mod API 11

API 11 lets mods create outfits and ship models while the game runs, make variants of any existing one, swap outfits on the player's ships, move outfits in and out of the player's cargo and storage, and wake disabled ships up. The calls are appended to `EsHost` in this order: `define`, `define_outfit_variant`, `define_ship_variant`, `defined`, `ship_replace_outfit`, `outfit_take`, `outfit_give`, then `ship_distance`, `ship_x`, `ship_y` and `ship_reactivate`. The first seven are mod-scoped; the ship queries and `ship_reactivate` act on this step's ship snapshot, like the other ship calls. `api_has` knows each as `"call:<name>"`, and the features `"runtime_definitions"`, `"outfit_transfer"` and `"ship_reactivate"`.

### Runtime definitions

`define(text)` takes top-level `outfit "<name>"`, `ship "<name>"` and `ship "<base>" "<name>"` nodes in data-file syntax. `define_outfit_variant(name, base, changes)` copies an outfit (game data, plugin or runtime) and applies `changes` (an outfit node's children) on top; `define_ship_variant(name, base, changes)` loads `ship "<base>" "<name>"` with `changes` as its children, as the game loads a variant. `defined(kind, name)` answers 0 (unknown), 1 (game data or a plugin) or 2 (defined at runtime for this pilot).

A node is refused, and logged, when the game data or a plugin defines its name, another mod defined it for this pilot, it does not parse, or (for a ship) it installs an outfit that is not defined. The same mod defining the same name with the same text again is a no-op that counts as defined; other text is refused: definitions are immutable for the pilot, and a new version needs a new name. "The same text" is compared after a round trip through `DataWriter`, so spacing and quoting do not matter.

The code is in `source/modding/RuntimeDefinitions.cpp`. It keeps, for the current pilot, an ordered list of records (the owner mod, the kind, the name, and either the node it loaded or the base and the changes), and `ModState` clears, loads and saves it with the rest of the mods' state. In the save it is a block before the ships:

```
"runtime definitions"
	define my_mod
		outfit "Shield Core"
			...
	"outfit variant" my_mod "Heavy Laser Mk2" "Heavy Laser"
		weapon
			"shield damage" 30
	"ship variant" my_mod "Barge X" "Star Barge"
		"display name" "Barge X"
```

A variant is saved as (name, base, changes), so it is derived from its base again on load, whatever the base is then.

#### Load order and pointer stability

The game's sets (`Set<T>`, a `std::map`) never move an object, and `Set::Get` makes an empty placeholder for any name it does not hold, which every part of a save uses to name outfits and ships. So a definition is always loaded into the set's own object of that name, never a new one: a placeholder an earlier lookup made becomes the real thing, and every pointer to it stays valid. Nothing is ever erased (other parts of the game may hold pointers; `GameData::Revert` does not touch outfits or ships either).

`PlayerInfo::Save` writes the block before the ships (the one line the fork adds there), and `PlayerInfo::Load` applies it as soon as it reads it. That is before everything that needs the definitions complete, which I checked one by one:

- the player's ships (`Ship(node)`, which looks outfits up by pointer, and `FinishLoading(false)` after `ApplyChanges`, which looks the model up by name for its explosion, display names and thumbnail);
- missions, whose NPC ships are loaded **and finished** inside `Mission`/`NPC` loading (`NPC::Load` calls `FinishLoading(false)` there, so a model must be complete by then), and whose actions name outfits;
- cargo, planetary storage, outfitter stock, depreciation records (`Ships().Get`/`Outfits().Get`, by pointer);
- `mod data`, `ship stock` and `outfit stock`, which use `Find` and drop unknown names, and `ModState::Validate`, which drops undefined ones;
- `changes` (events), applied in `ApplyChanges` after the whole file is read, which may put runtime ships in shipyards;
- `GameData::CheckReferences`, which names (and warns about) placeholders that were never defined: none, for what the block defines.

A save whose block comes after its ships (written by hand) still loads, since the objects are filled in place, except for missions' NPCs finished before it. A save without the block loads exactly as before.

#### Another pilot

Loading or making another pilot (`ModState::Clear`) makes every record stale: its object stays in the set (objects cannot be freed safely), but it is no longer "defined for this pilot": `defined` answers 0, and the harness's outfit and model lookups (`FindOutfit`, `FindModel`) do not see it, so a mod cannot give it to the new pilot. When the new pilot's block, or a mod, defines the name again, the object is loaded anew if the text differs (a ship is reset to `Ship()` first; an outfit is replaced by a freshly loaded one) and left alone if it is the same; a variant is derived again if its base was. A record that cannot be loaded (its base came from a plugin that was removed) is kept and saved again, and its object is emptied, so the save behaves as with a removed plugin.

#### What Outfit and Ship need after Load

- **Outfit**: nothing after `Outfit::Load`; it links its weapon's ammunition and its `ammo`/`linked` outfits by pointer, and adds a jump drive's range to `GameData`'s neighbor distances itself. That range only takes effect after `GameData::UpdateSystems`, which loading a pilot runs (`ApplyChanges`) and `define` runs when it defines such an outfit. Weapons need nothing either: their damage totals are computed lazily, and the cached lifetime is reset for variants.
- **Outfit variants** copy the base and load the changes on top, as `Outfit::Load` supports (a `weapon` block loads on top of a copy of the weapon). Three things `Load` adds to rather than replaces are handled: a changed `description` replaces the base's, a changed `display name` gets its own plural (unless `plural` is given), and the weapon's live effects, which `Weapon::Load` converts from counts into per-frame chances, are not converted twice. List-like weapon keys (effects, submunitions) add to the base's. `RuntimeDefinitionsDetail` is a friend of `Outfit`, `Weapon` and `Ship` for this (one line in each header).
- **Ship models** need `FinishLoading(true)`, as `UniverseObjects::FinishLoading` gives every model: it sums the outfits into the attributes, copies a variant's missing parts from its base (sprite, hardpoints, outfits, engines...), fills in the thumbnail from the sprite, and calibrates the caches. As the game data does, a pilot's block loads every outfit and ship first and then finishes the ships, in order (so a base is finished before its variants); and if anything was loaded again, every runtime model is finished again, since a model sums its outfits when it finishes. `define` defines a text's outfits before its ships, so a ship may install an outfit defined later in the same text. Sprites and thumbnails are named by path, and are there if the game's images have them.
- **Caches**: nothing in `GameData` indexes outfits or ships by category: the outfitter and the shipyard build their catalogs from the whole set when they open, so an open shop shows a new definition only when it is opened again. An outfit whose category is not in the game's category list is not shown in the outfitter, as with data. The harness forgets its shipyard models, fleet strengths and defense fleets after a definition.

### Replacing outfits

`ship_replace_outfit(ship, from, to, count)` works like the outfitter would, without a shop: on one of the player's ships (UUID) at the planet where the player is landed, one at a time, it uninstalls a removable `from` (integrated outfits are not) and installs `to`, only when the ship's attributes allow both changes together: `Outfit::CanAdd` with the old one out and the new one in, which covers outfit, weapon, engine and cargo space, gun ports and turret mounts (a weapon takes one), `installable`, required crew, and every other attribute with a minimum. The armament puts a weapon in a free hardpoint of its kind. Crew are adjusted and the ship recharged as the outfitter does, and the cargo capacities updated. Nothing is paid; the new outfit has no depreciation record, so it sells as a used one. Maps and licenses cannot be installed. Only from a hook the player started, while landed, and not from the outfitter's hooks (its panel holds what it shows of the ships).

### Moving outfits

`outfit_take(outfit, count, from)` and `outfit_give(outfit, count, to)` move outfits in and out of `"cargo"` (the pooled cargo while landed; in flight, taking is from the holds of the player's ships in the system that are not parked, as `cargo` counts them, and giving is into the flagship's hold) and `"storage"` (the planet's storage, landed only); `"any"` takes from cargo, then storage. Installed outfits are never taken. Giving ignores free space, as missions' gifts do. Maps and licenses cannot be given. Both work in any hook with a world, also `on_enter` on landing and `on_step`, but not in `save`.

### Waking ships up

`ship_distance(i)`, `ship_x(i)` and `ship_y(i)` answer a ship's distance from the flagship (-1 without a flagship in the system) and its position. `ship_reactivate(i, government, personality)` brings a disabled ship that is not the player's back: crew up to what it needs (at least 1), hull up to 10% of its maximum above its disabled threshold (never above the maximum), shields as they are, and the disabled flag cleared (`ModHarness::Reactivate`, as a friendly boarding repairs a ship, plus crew). A government takes it and its carried ships over; a personality replaces its own. AI and missions treat `derelict` ships specially (`AI.cpp` does not ask for help for them and holds a stranded derelict still, and `NPC::HasSucceeded` counts a derelict as immobile), so a reactivated ship always loses the `derelict` flag, also when its personality is kept, and the AI then flies it as any other ship. It keeps its tag; the harness's tag strengths are recounted.

### Testing API 11

`tests/modloader/api11/run.sh` runs the game itself on an integration test (`api11_tests.txt`, headless with `xvfb-run`) with a test loader (`api11_loader.c`) in a scratch config folder. The test loads a hand-made save whose `runtime definitions` block defines a runtime outfit, a variant of a game outfit, a runtime ship model and a variant of it, with a flagship of that model carrying those outfits; the loader checks the definitions, refusals, idempotence, variants and moving outfits when the pilot loads, replaces outfits from its planet button, and in flight wakes a placed derelict up, which must then fly. The test takes off (which saves), loads that save again and checks it all still holds. The script then checks that the saved block comes before the ships, and that a fresh game without the loader loads the save (`--parse-save`) with no errors.

## Testing

`tests/modloader/run.sh` builds a minimal C loader (`test_loader.c`) in several variants, plus a host test (`test_host.cpp`) that runs the game's discovery, dispatch and mod identity code (`source/modding/ModLoaders.cpp`) against them. SDL's shared object calls are stubbed over `dlopen`, so no game libraries are needed, only a C and a C++ compiler and SDL2's headers:

```sh
SDL_INCLUDE=<folder holding SDL2/SDL.h> sh tests/modloader/run.sh
```

It runs on Linux (`.so`, linked with `-ldl`) and on macOS (`.dylib`, no `-ldl`; for example `SDL_INCLUDE="$(brew --prefix)/include"`). It checks that:

- `es_loader_init` receives a valid host table;
- the loaders' hooks run, and per-mod callbacks reach the right loader;
- old-version, too-small, declined and broken loaders are rejected, while a loader built for a newer API is accepted and only the part of its table the game knows is used;
- a loader built for API 9 is accepted, the API 10 entries past its table are zeroed (not taken from what it left there), and `save`, `pilot_loaded` and `notification` reach only the API 10 loaders;
- the notification bus (`source/modding/ModBus.cpp`, built into the test): two loaders play ping-pong through it, the sender never gets its own notification, a loader cannot post as another loader's mod or outside a bracket, an endless cascade stops after 4 rounds and goes on at the next drain, and the limits on names, payloads, the queue (its count and its bytes, also while a drain holds part of it) and each sender hold;
- timing section names: control characters are refused, and a mod's 33rd name and later are `(other)`;
- a loader with a non-ASCII file name is opened through its UTF-8 path;
- a config folder loader replaces a resources folder loader of the same name;
- mod identity holds: calls during `es_loader_init` act for no mod, a bracket a loader leaves open does not reach its next call or another loader, an end without a begin is ignored, and a mod name another loader already runs is refused;
- `ModHarness.cpp` sets every field of the host table, and registers every call added since API 9 in `Features()`.

It then builds two deliberately broken copies of `ModLoaders` (one whose `RunningMod` trusts the last bracket, one that keeps brackets across calls) and fails unless the tests catch both.

The game's condition `"mod loaders"` holds the number of open loaders, set when they are opened after the data loads. `"mods loaded"` holds the number of mods they loaded: 0 until the first hook has them load their mods, then updated every engine step.
