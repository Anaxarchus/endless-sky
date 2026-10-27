# Dado mods for Endless Sky

Picking this up cold? Start with [HANDOFF.md](HANDOFF.md).

The game can run mods written in **DadoScript**, the scripting dialect of
[Dado](https://dadolang.com), alongside the usual data-file plugins. A mod is a
class with hooks that the game calls while you fly. Through the `Game` object
the game hands it, a mod can read and change ships, read and write the
player's conditions, read the galaxy, change the world as an event would,
spawn fleets, draw on the map, and put a button and a panel on the planet
screen. Missions and events use the same conditions, so mods and data plugins
can talk to each other.

**Writing mods without this repository:** modders use the separate mod SDK
(`endless-sky-mod-sdk`). It has the SDK, the build and test tools, examples,
and editor setup for DadoScript, JavaScript and TypeScript, and it builds each
mod into a plugin folder that can be dropped into `plugins/`. Its `GUIDE.md`
teaches modding from scratch: installing Dado, a first mod, and a complete one
in DadoScript and TypeScript. This page is about the harness itself, in the
game.

The current mod API is version 7. The API only grows: each version adds and
removes nothing, so mods built for an earlier version keep working.

## How it fits together

```
 Endless Sky (C++)                     modding/harness (Dado, compiled into the game)
 ───────────────────                   ──────────────────────────────────────────────
 Engine::Step ─────────── ModHarness::Step ─▶ esmod_day/enter/step()  ─▶ Harness ─▶ each mod's hook(game, ...)
 PlanetPanel (landing) ── ModHarness::Landed ──────▶ esmod_enter()                          │
 PlanetPanel (button) ─── ModHarness::PlanetButton ▶ esmod_planet_button()                  │
 mod panel (button) ────────────────────────────────▶ esmod_panel()                         │
                                                                                            │
 es_ship_hull(), es_spawn(), es_map_line() ... ◀── host_* wrappers ◀── Game methods ◀───────┘
 (modding/include/es_host.h,
  implemented in source/ModHarness.cpp)

 MapPanel::Draw ── ModHarness::DrawMap      draws the overlays the mods left
```

* **`modding/include/es_host.h`** is the C surface the game exposes. It is
  implemented in `source/ModHarness.cpp`.
* **`modding/harness/`** is a Dado package. `dadoc` transpiles it to one C file
  that is linked into the game together with the QuickJS script engine.
  - `entry.dado` exports `esmod_init`, `esmod_load`, `esmod_step` and so on to C.
  - `host.dado` wraps the game's C functions and checks every value a script
    passes before it reaches the game.
  - `Game.dados` is the API mods see.
  - `harness.dado` loads mods and runs their hooks.
* **`modding/sdk/es/`** is what a mod builds against:
  - `Mod.dados` is the base class every mod extends.
  - `game.dado` holds declarations of `Game`, generated from the harness.
  - `version.dado` holds the mod API version.
* **Mods** are compiled ahead of time into one JavaScript file each. You ship
  only that `.js` file. The game loads it when it runs, from the `dado/` folder
  of any resource or plugin folder.

The game calls the harness from these places:

* **`Engine.cpp`**: `Engine::Step` runs the flight hooks and hands over the
  step's new-ship list and ship events; `Engine::Place` (taking off, loading)
  tells the harness the ships were replaced.
* **`PlanetPanel.cpp`**: landing runs `on_enter`, and the planet screen draws
  the mods' button and sends its click (or the `K` key) to the harness.
* **`MapPanel.cpp`**: every map view draws the mods' overlays.
* **`OutfitterPanel.cpp`** (API 5): the mods' outfitter buttons, in rows above
  the outfitter's own; drawing one asks the mod's `can_outfitter`, and a
  click runs `on_outfitter` through `ModHarness::OutfitterButton`.
* **The mod panel** is `ModPanel` in `ModHarness.cpp`. It sends its button
  clicks back to the mod that opened it.

`Planet::DefenseFleets()` is an accessor added for mods, so they can read what
defends a system's planets.

All of this runs on the main thread: in flight inside `Engine::Step` while the
simulation thread is paused, and from the planet screen, the map and the mod
panel, which run only while it is stopped. The script engine belongs to the
thread that created it, and those are the only moments it is safe to change
the world.

## Building the game with mods

You need:

* **The Dado toolchain**, 1.0.0-rc.8 or later:
  `curl -fsSL https://raw.githubusercontent.com/dadolangcom/Dado/main/install.sh | sh`.
  It goes in `~/dado`, with `dado` and `dadoc` linked into `~/.local/bin`.
* **esbuild** for bundling mods. Node's `npx` fetches it on first use, so
  `brew install node` is enough.

The harness runs on macOS and Linux. Windows has to wait until DadoScript
supports it.

**In VS Code (CMake Tools):**

1. Pick the configure preset **"MacOS ARM + Dado mods"** (or **"Linux + Dado
   mods"**, **"MacOS + Dado mods"**). It builds into its own folder,
   `build/<preset>-dado`, so your plain build is left alone.
2. Build with **Debug** or **Release**. That builds the game, and the
   `dado_example_plugin` target puts the example mods into the repository's
   gitignored `plugins/Dado Example/`. A game run from a build folder loads that
   folder.
3. Run the game, load a pilot and land anywhere. The plugin's invisible mission
   switches the example on, and in flight you'll see *"Hull regeneration mod
   active."* The game log shows each mod that loaded.

**From a terminal:**

```sh
cmake --preset macos-arm-dado
cmake --build --preset macos-arm-dado-release
ctest --preset macos-arm-dado-test            # the harness against a fake game
```

Without a preset, pass `-DES_DADO_MODS=ON` to any configure. It is off by
default, and then the game builds and behaves exactly as before. The first
configure builds Dado's patched QuickJS with your C compiler. Later configures
reuse the cached build in `~/.cache/dado-quickjs`.

Targets, all of which you can pick under "Set Build Target":

| Target | |
|---|---|
| `dado_example_plugin` | build the examples into `plugins/Dado Example/` (`DADO_EXAMPLE_PLUGIN` moves it) |
| `dado_mods` | rebuild the committed `.js` of the examples and the integration tests' mods |
| `dado_sdk` | regenerate `modding/sdk/es/game.dado` after changing `Game.dados` |
| `dado_mods_test` | build the harness against a fake game and run every example and test mod |

## Writing a mod

A mod is a directory holding one `.dados` file, whose class extends `es.Mod`:

```dados
class ShieldWall extends es.Mod

import "imports:sdk/es"

string name():
    return "shield wall"

void on_load(es.Game game):
    log(game, "loaded")

void on_step(es.Game game, int step):
    if every(step, 1.0):
        for i in player_ships(game):
            game.set_shields(i, game.shields(i) + 10.0)
```

Build it:

```sh
modding/tools/dado-mods.sh mod path/to/shield_wall          # writes path/to/shield_wall/shield_wall.js
```

Ship it by putting the `.js` file into a plugin's `dado` folder:

```
plugins/My Plugin/
    data/...            the usual data files
    dado/shield_wall.js
```

**Workflow, in short:** you write against the SDK in `modding/sdk/es` and ship
only the built JavaScript.

1. `dadoc` checks every call against the SDK. For example, `game.hul(i)` fails
   with *`Game` has no method `hul`* and a *did you mean `hull`?* hint.
2. The mod is built as a JavaScript library (`--emit=js-package`).
3. esbuild then flattens it into one file. That file holds your class, `es.Mod`
   and the parts of Dado's runtime they use, and it imports nothing.
4. The `Game` declarations are not in the output. At run time the mod receives
   the game's real `Game`.

A built mod is a few kilobytes of plain JavaScript, so one file works on every
platform the harness runs on. Building needs esbuild: on your PATH, through
`npx`, or named by the `ESBUILD` environment variable.

Rules a mod lives by:

* **Extend `es.Mod`, and override what you need.** A file whose class doesn't
  extend it is refused. So is a mod built for a newer mod API than the game
  speaks, as `es.Mod.api_version()` reports it. Mods built for the game's
  version or an earlier one run.
* **Take `Game` as a parameter; don't store it.** A script has no null, so a
  field of `Game`'s type can't be declared. The game passes `Game` into every
  hook.
* **Ship indices are per hook.** `0 ..< game.ship_count()` indexes the ships
  in the current system *for this call only*. An index out of range answers
  zeros or `""`, and writes to it are ignored. In a hook run from the planet
  screen or a panel there are no ships, so `ship_count()` is 0.
* **Spawned ships live only in the system.** `spawn()` works only in flight
  (in `on_step`, `on_day` or `on_enter` run by the engine; on landing it
  answers 0). The ships go into the engine's queue of new ships and appear in
  `ship_count()` from the next step. They exist only while the player is in
  that system: after a jump, landing or loading they are gone, and are not
  saved. Spawn them again from `on_enter` if they should still be there.
* **Failures are contained.** If a mod fails to load or fails in `on_load`,
  the failure is logged and the other mods load anyway. If `on_step`,
  `on_day` or `on_enter` throws, it is logged once and that hook is switched
  off for that mod. A failure in `on_planet_button` or `on_panel` is logged
  and the hook stays on. The game keeps running.
* **Mods see only `Game`.** The game refuses a built file that imports
  anything, which keeps mods to the API. It is not a hardened security sandbox,
  so only install mods you trust, as with any plugin.
* **One class per directory.** Every file in a directory belongs to one Dado
  package, so give each mod its own folder.

### Hooks

| Hook | When it runs |
|---|---|
| `on_load(Game)` | once, when the harness starts: on the first landing (a loaded pilot starts on a planet) or at the first engine step in flight, whichever comes first. It runs before any other hook of the mod. |
| `on_day(Game, int day)` | once for each new day, oldest first; `day` counts days since the game's epoch. Days pass when the player jumps or takes off. |
| `on_enter(Game, string system)` | when the player arrives in a system (its true name), on the first step after the harness starts, on landing, and again on the first step after taking off or loading. |
| `on_step(Game, int step)` | every engine step (60 per second) while the player is flying and the game is not paused. |
| `on_planet_button(Game)` | when the player clicks this mod's planet-screen button (or presses `K`). |
| `on_panel(Game, int button)` | when the player clicks a button on this mod's panel; `button` is its index, in the order the buttons were added. |
| `can_outfitter(Game, string outfit) bool` | (API 5) every frame the outfitter draws this mod's button: whether it is enabled for the selected outfit (`""` for none). The default answers true. A failure switches the button off. |
| `on_outfitter(Game, string outfit)` | (API 5) when the player clicks this mod's outfitter button. |
| `on_confirm(Game, int token)` | (API 5) when the player clicks OK in a dialog the mod opened with `confirm()`. |
| `on_panel_row(Game, int row)` | (API 6) when the player clicks a row of the table in this mod's panel. |
| `on_number(Game, int token, int value)` | (API 6) when the player answers an `ask_number()` dialog with OK. |
| `on_panel_header(Game, int column)` | (API 7) when the player clicks a column heading of the table on this mod's full screen. |

In each engine step the harness runs `on_day` for every new day, then
`on_enter` if the player has arrived in a system since the last step, then
`on_step`. Each hook runs for every mod in turn, in order of the mods' file
names.

Some details follow from how the game works:

* **Taking off replaces every ship**, so `on_enter` runs again for the system
  the player took off into. That is where a mod puts back the ships it
  spawned.
* **Landing runs `on_enter`** for the planet's system, with no ships and no
  spawning. It lets a mod put up its planet button even for a pilot that was
  loaded on a planet.
* **A jump in the date of more than a week**, or backwards, is not play: it is
  another pilot or another save. The harness runs no `on_day` for it and runs
  `on_enter` again instead, so the mod can look around and rebuild what it
  shows.

### `es.Mod`

| Member | |
|---|---|
| `name() string` | the mod's name, used by `log`; override it |
| `on_load(Game)`, `on_step(Game, int step)` | hooks (API 1); the defaults do nothing |
| `on_day(Game, int day)`, `on_enter(Game, string system)` | hooks (API 2); the defaults do nothing |
| `on_planet_button(Game)`, `on_panel(Game, int button)` | hooks (API 2); the defaults do nothing |
| `log(Game, string)` | log a line prefixed with `name()` |
| `every(int step, float seconds) bool` | true once every `seconds` of game time |
| `player_ships(Game) []int` | the player's ships in the system that are not disabled |
| `api_version() int` | the API the mod was built against; don't override |

These are compiled into each mod, so calling them costs nothing extra.

### The `Game` API

Systems, governments and fleets are named by their **true names**, the names
in the data files. `system()` is the one exception: it answers the current
system's display name, for showing to the player. `here()`, `system_at()`,
`link()`, `system_government()`, `ship_government()` and everything that takes
a system or a government use true names. An unknown name or an index out of
range answers zeros, `false` or `""`, and changes nothing.

**Messages and state** (API 1)

| Method | |
|---|---|
| `log(string)`, `message(string)` | write to the game log, or show the player a message |
| `step() int` | the engine's step counter: 60 steps per second of game time |
| `system() string` | the display name of the player's system |
| `condition(name) int`, `set_condition(name, v)` | the player's conditions, shared with missions and events |

**Ships in the system** (API 1)

| Method | |
|---|---|
| `ship_count() int`, `flagship() int` | ships in the system; the flagship's index or -1 |
| `ship_name(i)`, `ship_model(i)` | names |
| `is_player(i)`, `is_disabled(i)` | flags |
| `hull(i)`, `max_hull(i)`, `shields(i)`, `max_shields(i)`, `energy(i)`, `fuel(i)` | levels |
| `set_hull(i, v)`, `set_shields(i, v)` | set a level; clamped to `[0, max]` |

**The galaxy** (API 2)

| Method | |
|---|---|
| `day() int` | the date, as days since the game's epoch |
| `here() string` | the true name of the player's system, or `""` |
| `system_count() int`, `system_at(i) string` | every valid, accessible system, sorted by true name |
| `system_government(system) string` | the government a system belongs to |
| `link_count(system) int`, `link(system, k) string` | the systems linked to this one by hyperspace lanes |
| `inhabited(system) bool` | whether the system is inhabited |
| `defense_count(system) int`, `defense_fleet(system, k) string` | the defense fleets of the system's planets, the ones a player fights to dominate them |

**World changes and governments** (API 2)

| Method | |
|---|---|
| `change(text) bool` | apply `text` as the body of an event, as a plugin's event would; false only for an empty body |
| `attitude(government, toward) float` | one government's attitude toward another; below 0 is hostile |
| `reputation(government) float`, `set_reputation(government, v)` | the player's reputation with a government; below 0 is hostile |
| `fleet_strength(fleet) float` | a fleet definition's average strength: the sum of its ships' costs |
| `fleet_stat(fleet, stat) float` | (API 3) a fleet's expected stats, averaged over its variants: `"ships"`, `"hull"`, `"shields"`, `"dps"` (shield plus hull damage per second of its weapons) and `"cost"` are totals; `"speed"` is its ships' average top speed (thrust ÷ drag). 0 for an unknown fleet or stat |
| `system_fleet_count(system) int`, `system_fleet(system, k) string` | (API 3) the fleets that arrive in a system on their own, its `fleet` lines |
| `plugin_enabled(name) bool` | whether a plugin with this name is installed and enabled; a mod can ship a companion plugin as a switch the player flips in Preferences → Plugins (Diplomacy's debug mode works this way) |

`change()` takes the same lines an event holds, for example
`game.change("system \"Alpha Centauri\"\n\tgovernment \"Free Worlds\"")`. The
harness wraps them in an unnamed event, applies it at once and adds it to the
pilot, so the change is saved. Anything an event may hold works, including
`visit "<system>"` and `visit planet "<planet>"` lines and condition
assignments. Only an empty body answers false.

Some of the game's conditions are wired to the player's state and can be
written too. For example, `set_condition("credits", n)` sets the player's
credits.

**Spawning, tags and kills** (API 2)

| Method | |
|---|---|
| `spawn(fleet, government, tag, arrive) int` | in flight only: add a fleet to the player's system under `government`, its ships tagged `tag`. With `arrive` they jump in; otherwise they are already there. Answers how many ships were added, counting the fighters and drones in their bays. |
| `ship_tag(i) string` | the tag a spawned ship carries, or `""` |
| `ship_government(i) string` | a ship's government |
| `ship_strength(i) float` | a ship's strength |
| `tag_strength(tag) float` | the strength of the ships in the system with this tag that can still fight |
| `player_kills(prefix) int` | how many ships whose tag starts with `prefix` the player has disabled or destroyed since the last call with this prefix |

A ship counts once for `player_kills()`: disabling it and then destroying it
is one kill. Each call answers the kills since the last call with the same
prefix and resets that count.

**Map overlays** (API 2)

| Method | |
|---|---|
| `map_line(from, to, r, g, b)` | a dashed line from one system to another, with an arrowhead at `to` |
| `map_marker(system, r, g, b, label)` | a ring around a system, with `label` under it |
| `map_detail(system, title, text, r, g, b)` | (API 4) a section of the map's side panel: while the player has `system` selected, the map shows `title` in the colour and `text` (wrapped; `\n` breaks a line) in a panel below the map key, like the vanilla side panels. Sections from every mod stack in order; what doesn't fit above the map buttons is left out with a note. `map_clear` removes them |
| `map_clear()` | remove this mod's lines and markers |

Colours are 0 to 1. Each mod has its own set of overlays. They stay until the
mod clears them and are drawn on every map view (not on the simplified map).
Labels on the same system stack downward.

**Planet button and panels** (API 2)

| Method | |
|---|---|
| `planet_button(label)` | show a button with this label on the planet screen; `""` removes it. Clicking it runs this mod's `on_planet_button`. |
| `panel_open(title, text)` | open this mod's panel with a title and text, or replace its contents if it is open |
| `panel_button(label)` | add a button to this mod's open panel; clicking it runs `on_panel` with its index |
| `panel_close()` | close this mod's panel |

The planet button sits in the free slot above *Depart*, and the `K` key
presses it. When several mods have a button, it reads *Mods* and opens a
chooser with one button per mod.

The panel is a planet sub-panel, like the bank and trading panels: the planet
screen swaps it into its content area (`ModHarness::NewPlanetPanel`, held by
`PlanetPanel`). It shows the title, the text in a scrolling text area, and the
buttons along the bottom, numbered for the keys `1` to `9`. Every other key
passes through to the planet screen, so *Bank*, *Trading*, *Depart* and the
rest work as usual and replace the panel. A panel opens from
`on_planet_button` or `on_panel` (the hooks the player starts), or from
`on_enter` while the player lands, which the planet screen then shows at once.
From any other hook, `panel_open()` can only refresh a panel of the same mod
that is already open. Leaving the panel, or the planet, closes it.

**The outfitter, the fleet and outfits** (API 5)

| Method | |
|---|---|
| `outfitter_button(label)` | a button with this label in the outfitter (`""` removes it), in rows of three above the outfitter's own buttons (up to six mods). It is enabled while the mod's `can_outfitter` says so; clicking it runs `on_outfitter` |
| `outfitter_ship_count() int`, `outfitter_ship(k) string` | in `can_outfitter`, `on_outfitter` and the `on_confirm` they lead to: the outfitter's selected ships as UUIDs, the one in focus first |
| `fleet_count() int`, `fleet_ship(k) string` | the player's ships that are not destroyed, wherever they are, as UUIDs (which last across saving and loading) |
| `fleet_ship_name(ship)`, `fleet_ship_model(ship)` | one of the player's ships' name and model |
| `ship_outfit_count(ship, outfit) int`, `ship_integrated_count(ship, outfit) int` | how many of an outfit (by true name) a ship has installed, and how many of those are integrated |
| `ship_integrate(ship, outfit, scale) bool` | integrate one installed outfit that is not yet integrated; see below |
| `outfit_name(outfit)`, `outfit_category(outfit)` | an outfit's display name and category |
| `outfit_attribute(outfit, attribute) float` | an attribute as in its data; `"mass"` and `"cost"` answer its mass and cost |
| `outfit_is_weapon(outfit) bool` | whether it goes in a gun port or turret mount |
| `confirm(text, token)` | a dialog with OK and Cancel (the default); OK runs `on_confirm` with `token`. Only from a hook the player started |

**Storage, planets, tables and numbers** (API 6)

| Method | |
|---|---|
| `data(key) string`, `set_data(key, text)` | text the mod keeps with the pilot, by key, apart from the conditions missions share (`""` removes a key; up to 4 MB). Saved in the pilot's `mod data` block, percent-encoded so any text survives |
| `planet() string` | the planet the player is landed on (true name), or `""` |
| `planet_name(planet)`, `planet_system(planet)`, `planet_government(planet)` | a planet's display name, system and government |
| `planet_count(system) int`, `planet_at(system, k) string` | a system's planets |
| `planet_service(planet, service) bool` | whether the player may use `"bank"`, `"trading"`, `"job board"`, `"hire crew"`, `"outfitter"` or `"shipyard"` there now: the planet has it and the player has access |
| `panel_columns(headers)`, `panel_row(cells)` | a table in the open panel, under its text: tab-separated headings, then rows. It scrolls with the mouse wheel; the first column is aligned left and cut short, the rest right. Clicking a row runs `on_panel_row` |
| `ask_number(text, token, most)` | the game's number dialog, from 1 to `most`; OK runs `on_number(token, value)`. Only from a hook the player started |
| `screen_open(title, text)` | (API 7) the panel on a full screen of its own, pushed over the planet screen as the outfitter is (`ModScreen`): the title and the table fill the left, the text and the buttons a sidebar on the right. Clicking a heading runs `on_panel_header`; Up and Down step through the rows as clicks; Escape or `panel_close` closes it; `panel_open` goes back to the planet screen's panel |
| `panel_select(row)` | (API 7) show a row of the table as chosen (`-1` for none); the screen scrolls to it |

**Integrated outfits** are a game feature mods switch on: `Ship` keeps a count
of integrated outfits per outfit, saved with the ship as
`integrated "<outfit>" <count>` with a `reduction <sum of 1 - scale>` child. An integrated outfit stays
installed (a weapon keeps its hardpoint and shows as usual), but the outfitter
cannot uninstall, sell, store or move it (`Ship::RemovableCount`), mission
actions cannot take it, boarding cannot plunder it, and it cannot be
jettisoned. Its mass and the outfit space it takes up are scaled by `scale`,
each outfit's savings rounded to whole numbers as the game's masses and spaces
are. Weapon and engine capacity, cargo space, and its gun port or turret
mount stay taken: `Ship::IntegrationSavings`, applied in
`Ship::FinishLoading` and `Ship::Integrate`. Ammunition, and anything an
installed outfit stores or fires as ammunition, cannot be integrated: its
count changes in flight. A game without this feature reads such a save with
the outfits installed as usual, removable, and at full mass.

The game also provides two read-only conditions that missions can test:
`"dado harness"` is 1 when the harness is built in, and `"dado mods loaded"` is
how many mods loaded.

### Persistence

The game saves the pilot, not the mods. What lasts:

* **The player's conditions.** Keep a mod's state in them, with
  `set_condition()`. They are saved with the pilot.
* **`change()` edits.** They are saved with the pilot as events and applied
  again when it loads.
* **Reputations** set with `set_reputation()`, which the game saves as it
  does any reputation.

What does not last: spawned ships, map overlays, the planet button and panels.
The mod rebuilds them from its conditions, in `on_enter` (which runs again on
landing, after taking off and after loading another pilot) and `on_day`.

## Performance

A call from a mod into the game is cheap: about 40 ns for a number, 70 ns with
a string argument, against 28 ns for a call that stays inside the script
(measured on 2026-10-27 in an aarch64 Linux VM with this harness and a stub
game). So the harness does not batch calls; what costs is work the game does
inside a call, and what a mod does itself.

**Lookups are cached for one run of hooks** (`Caches` in `ModHarness.cpp`):
one engine step, shared by every mod's hooks in it, or one hook the UI runs.
The cache lives in the `Context` and goes with it. It holds the player's
fleet (for `fleet_count`, `fleet_ship` and every lookup by UUID), each
system's defense fleets, the strength of every tag (one pass for all of
them), and `fleet_stat` answers. The world does not change under a run except
through the mods' own calls, and those that change something cached clear it:
`spawn` and `set_hull` clear the tag strengths, and `change` clears the
defense fleets and fleet stats.

**The galaxy list** behind `system_count` and `system_at`, and the fleet
names, last longer: they are rebuilt after events may have changed them, on a
new day, on landing, on taking off or loading (`ShipsReplaced`), and after
`change`.

**Hooks are timed.** The harness calls `es_hook_timing` around each hook it
runs for a mod. A call of 2 ms or more is logged as a warning ("Dado mod
"x": on_step took 3.1 ms"), and then again only when one is twice as slow as
the last one logged. When the game quits, the log gets each mod's calls, total
time, average and slowest call per hook, and each mod's **share of the
frame**: the time its hooks took in the engine's steps (on_day, on_enter and
on_step, which the frame waits for), averaged per step, as a percentage of a
60 fps frame, with its worst step.

**Dev builds** (no `NDEBUG`, e.g. Debug) also log the frame shares for each
minute of play, and time every call a mod makes into the game: at shutdown
the log lists the 25 calls that took the most time in all, by mod and call
("diplomacy es_change: 3 calls, 212.0 ms, 70666.67 us"). That timing adds
about 40 ns to each call, so release builds leave it out.

## The example, and the tests

`examples/hull_regen` makes the player's ships slowly regenerate hull. It stays
idle until something sets the condition `"dado example: enabled"`. In the
example plugin, that is the mission in `examples/plugin/data/dado example.txt`,
which fires on your first landing. `examples/plugin/` is the plugin's source
apart from its mods: `plugin.txt` (its name and the description shown in the
game's plugin list) and `data/`. `dado_example_plugin` copies it and builds the
mods into its `dado/` folder. Mods and data files talk through conditions
like this.

```sh
modding/tools/dado-mods.sh test       # the harness against a fake game: fast, no graphics
modding/tools/dado-mods.sh examples   # rebuild the examples and the integration-test plugin's mods
```

* `test` builds the harness with `test/fake_game.cpp`, a stand-in host with a
  few fake ships and a small fake galaxy. It loads the example and the test
  mods in `test/mods` and drives them as the game would: steps in flight, new
  days, a jump, landing, and clicks on the planet button and a panel. Then it
  checks what they did. The adversarial mods: one throws, one passes bad ship
  indices, one doesn't extend `es.Mod`, and one claims an API version newer
  than the game's. `test/mods/api2_probe` calls every method API 2 added and
  records what it saw in conditions. The test checks, among other things, true
  and display names, out-of-range and unknown names, `change()`, spawning and
  when spawned ships appear, tags and kill counts, map overlays and the
  planet button landing under the calling mod's name, that a panel cannot
  open from `on_step`, and that `spawn()` refuses while landed. The probe stays
  idle unless the condition `"test: api2"` is set, because the test mods are
  also copied into the real game's integration-test plugin. With
  `ES_DADO_MODS` on, `test` is also the CTest test "Dado mod harness" and the
  `dado_mods_test` target.
* The integration test `"Dado mods - Hull regen example"`
  (`tests/integration/.../tests_dado_mods.txt`) flies the real game with the
  mods in `tests/integration/config/plugins/integration-tests/dado/`. A test
  mod halves the flagship's hull, and the test asserts that the example mod
  repairs it. In a build without the harness, the test only checks that the
  game flies.

## Changing the API

1. Add the C function to `include/es_host.h` and implement it in
   `source/ModHarness.cpp`, and in `test/fake_game.cpp`.
2. Declare it in the `foreign` block in `harness/host.dado`, with a `host_*`
   wrapper that checks its arguments. The wrapper needs its own name because a
   `Game` method of the same name would win over it and call itself.
3. Add the method to `harness/Game.dados`.
4. Run `modding/tools/dado-mods.sh sdk` and `examples`, then commit the
   regenerated `sdk/es/game.dado` and the `.js` files.
5. Raise `MOD_API` in `sdk/es/version.dado` for each release that adds to the
   API, and note what it added in the comment there. The harness reads the
   same constant.
6. In the mod SDK repository, run `npm run sync -- <this repository>` and
   `npm test`. Then add the new method to its `js/fake-game.js`; the sync
   names anything missing. Every method in `Game.dados` needs a doc comment
   right above it, because the SDK's editor docs come from there.

**Keep the caches right.** A new call that adds or removes the player's
ships, spawns or changes ships, or changes the galaxy, its planets or its
fleets must clear what it invalidates in `Context::cache` (and `ForgetWorld()`
for the galaxy list). A new lookup that walks a list each call is a candidate
for the cache. See "Performance".

**The API only grows.** Never remove a method or a hook, and never change what
one takes or answers: a game runs every mod built for its version or an
earlier one, and refuses only newer ones. A mod built for version 2 then
keeps working in every later game. If something has to change, add a new
method next to the old one.

A method that belongs to one mod (a map overlay, the planet button, a panel)
passes the mod's name to the game: `Game` keeps it in its private `caller`,
which the harness sets before each hook, and the C function takes it as its
first argument.

A new hook needs:

* a default in `sdk/es/Mod.dados`;
* a call in `harness/harness.dado`, run only if the mod has the method (a mod
  built for an earlier API bundles an `es.Mod` without it);
* an export in `entry.dado`;
* a call in `source/ModHarness.cpp`, and a call site in the game: in flight,
  inside `ModHarness::Step` (which sets the hook order); for the planet screen,
  `ModHarness::Landed` or `ModHarness::PlanetButton`, called from
  `PlanetPanel.cpp`; for a panel, `ModPanel` in `ModHarness.cpp`;
* the same driving in `test/fake_game.cpp`, and a check.

A new UI feature also needs its drawing and input in the game: map overlays
are drawn by `ModHarness::DrawMap`, called from `MapPanel.cpp`; the planet
button is drawn and its key handled in `PlanetPanel.cpp`; the panel is
`ModPanel` in `ModHarness.cpp`. A hook the player starts from the UI runs
through `RunUIHook`, which gives it the `UI` that panels open on.

## A larger example

A separate project, "Diplomacy" (es-diplomacy), is a mod built on this API that
uses every API 2 feature: it reads the galaxy and its defense fleets, changes
system governments with `change()`, spawns tagged fleets and counts the
player's kills, draws lines and markers on the map, and runs its own planet button and
panels. It keeps its state in conditions and rebuilds the rest in `on_enter`
and `on_day`, as described above. Read it to see how the pieces fit in a real
mod.

## Dado notes (as of 1.0.0-rc.8)

* `dado build` does not build libraries, so the harness is built with
  `dadoc --package --library --emit=c|h` and Dado's `tools/quickjs/build.sh`.
* `dadoc --emit=h` writes no `extern "C"` guard, so C++ includes it inside
  `extern "C" { }`.
* An inner class inside a `.dado` class block crashes `dadoc` ("a class block's
  node is one of its file's items"). `harness.dado` uses two top-level private
  class blocks instead.
* A `.dados` header cannot be `private`, so classes meant to stay out of the
  SDK are `private class X:` blocks in `.dado` files.
* `const` inside a class is not built yet; use a field.
* A script loaded with `Script.load` may import nothing but the runtime, so a
  mod cannot import `es.Mod` from the game. Each mod therefore bundles its own
  copy of `es.Mod` and of the runtime parts it uses, which is why the build
  goes through `--emit=js-package` and esbuild. A failure thrown by that copy
  reaches the harness with its code in the text (`error 70: …`). A cleaner
  fix in Dado would let a host publish classes that its loaded scripts may
  import and extend.
* The emitted C works out its heap alignment from `long double`, which is 8
  bytes on Apple ARM. The heap then refuses the engine's 16-byte requests, and
  the harness fails with "script: no memory for a VM". `tools/fix-max-align.cmake`
  pins it to 16 after `dadoc` runs; see HANDOFF.md.
