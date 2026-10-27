# Handoff notes: Dado mods for Endless Sky

These notes are for picking this work up cold. They cover where things are, how
to build and test, what has been tried, and the traps that already cost time.
[README.md](README.md) is the reference for the harness and its API; read it
next. This file is the working log around it.

Last updated 2026-10-26 (the mod SDK repository was added).

## The goal

Anaxarchus (the owner of this fork) writes Endless Sky mods in his own
language, Dado (https://dadolang.com, toolchain at github.com/dadolangcom/Dado).
Mods are written in DadoScript, the dialect that compiles to JavaScript, and
run in the game on an embedded QuickJS-ng engine. The game exposes a C API.
The harness is a Dado package, transpiled to C and linked into the game. Mods
extend `es.Mod`, are bundled to one `.js` file each, and ship inside an
ordinary plugin's `dado/` folder.

We decided on a C ABI harness, not a port of the game to Dado.

## Where everything is

| What | Where |
| --- | --- |
| Game fork | `~/dev/GitHub/endless-sky` on the Mac. Branch `master`, based on upstream `abb41b23`. **Nothing is committed yet**: all of this work is uncommitted changes in the working tree. Ask for a branch name before committing, and push only when asked. |
| Mod SDK | `~/dev/GitHub/endless-sky-mod-sdk` (intended for github.com/Anaxarchus/endless-sky-mod-sdk; not pushed and with no commits yet). It is what modders clone. Read its README and MAINTAINING.md. |
| Diplomacy mod | `~/dev/GitHub/es-diplomacy`, a separate folder and not a git repo. This is deliberate, so a mod gets deployed into the game from somewhere else. |
| Harness C API | `modding/include/es_host.h`, implemented in `source/ModHarness.cpp` / `.h` |
| Harness (Dado) | `modding/harness/`: `entry.dado` (exports), `host.dado` (the `host_*` wrappers), `Game.dados` (the mod API), `harness.dado` |
| Mod SDK | `modding/sdk/es/`. `game.dado` is generated (`dado-mods.sh sdk`); `Mod.dados` and `version.dado` (`MOD_API = 2`) are written by hand. |
| Build glue | `modding/Dado.cmake` (option `ES_DADO_MODS`), `modding/tools/dado-mods.sh`, `modding/tools/fix-max-align.cmake` |
| Example mod | `modding/examples/hull_regen` with the plugin in `modding/examples/plugin` (target `dado_example_plugin` puts it in `plugins/Dado Example`) |
| Tests | `modding/test/fake_game.cpp` and `modding/test/mods/*`; integration tests in `tests/integration/config/plugins/integration-tests/` (`dado/*.js`, `tests_dado_mods.txt`) |
| Game hooks | `Engine.cpp` (Step, Place), `MapPanel.cpp` (DrawMap), `PlanetPanel.cpp` (Landed, the button, key `k`), `main.cpp` (Shutdown), `PlayerInfo.cpp` (conditions `dado harness`, `dado mods loaded`), `Planet.h/.cpp` (`DefenseFleets()`), `Entity.h` (friend) |

The Mac is an ARM Mac mini. Paths there are under the user's home folder (`~`).
The game's user data is in `~/Library/Application Support/endless-sky`. Claude's
device bridge cannot read that folder (it is a protected location), so ask the
user for log lines or files from it.

## Building and running

The Mac uses VS Code with CMake presets. **On the ARM Mac, use `macos-arm-dado`**:
the configure preset is "MacOS ARM + Dado mods", and the build presets are
`macos-arm-dado-debug` and `-release`. The plain `macos-dado` preset builds
x86_64. The binary is `build/macos-arm-dado/Debug/endless-sky`. Run it from a
terminal to see the log on stdout.

Linux: the `linux-dado` preset, or in Claude's VM, `/home/claude/es/build-dado`
(Ninja, plus a Debug ASan/UBSan build that was used once).

Needs: `dado`/`dadoc` 1.0.0-rc.8 (in `~/.local/bin`), a C compiler, and esbuild
(`brew install esbuild` on the Mac; the script falls back to `npx esbuild@0.24.0`).
On the Mac, Homebrew provided `pkg-config`, `ninja`, `sdl2` and friends.
QuickJS is built by Dado's `tools/quickjs/build.sh` and cached in
`~/.cache/dado-quickjs`.

Build targets with the option on: `endless-sky`, `dado_example_plugin`,
`dado_mods` (rebuilds the test and example `.js`), `dado_sdk`, `dado_mods_test`.

## Testing

* `modding/tools/dado-mods.sh test`: the harness against a fake host. Fast,
  no graphics. Also CTest "Dado mod harness" (label `dado`).
* The game's unit tests (96 cases) and integration tests (46, including "Dado
  mods - Hull regen example"). They run headless under `xvfb-run` with
  `--resources <repo> --config <copy of tests/integration/config> --test "<name>"`.
* Diplomacy: `test/run.sh` runs the simulation against a mock game (Node);
  `test/ingame/run.sh <game-binary> <game-root>` runs 4 real-game tests (War
  Begins, Flotilla In Person, Join And Leave, Debug Mode).

Every one of these runs on Linux x86_64 in Claude's VM. On the ARM Mac the
game has been played by hand, but the tests have not been run there.

## Diplomacy

The design is in `es-diplomacy/DESIGN.md` and the behaviour and tuning in its
README. Deploy with
`tools/deploy.sh [--debug] [--user] <game-root> [<plugin-dir>]`. `--debug` also
installs a "Diplomacy Debug" plugin. When the mod sees that plugin enabled
(`game.plugin_enabled`), it starts from day 0 without the Free Worlds campaign.
It gives the Free Worlds their wartime territory and starts a Republic–Free
Worlds war at once, launching flotillas immediately with a message for each.
It also posts a "[Diplomacy debug] Debug mode is on" message.

On the Mac, the deployed plugins are in `endless-sky/plugins/Diplomacy` and
`plugins/Diplomacy Debug`, the repo's global plugins folder, which a game run
from the build tree loads.

Once per pilot (the condition `diplomacy: debug gifts`), debug mode also
reveals the whole map with one `change()` of `visit "<system>"` lines, and adds
100M credits by writing the `credits` condition. `change()` used to refuse an
event with no data changes, which ruled out visit-only events. It now refuses
only an empty body, so this needs a game rebuilt after 2026-10-26.

Members of the Republic are issued Navy licenses by rank (Navy at Lieutenant,
Auxiliary at Captain, Cruiser at Commodore, Carrier at Admiral). During a war,
members get six kinds of faction job: courier, scout ship, scout systems,
medium warship, capital ship and warfleet. The jobs are generated by
`es-diplomacy/tools/gen-jobs.mjs`. The user's direction is "something like
Mount & Blade's system".

Added on 2026-10-26, in the evening (see the Diplomacy README for the detail):

* **Opt-in.** A pilot who finished the Free Worlds campaign is offered
  Diplomacy on landing, in the planet screen's panel. The choices are Turn on,
  Not now (for this session) and Never (`diplomacy: declined`). Accepting makes
  them a Free Worlds **Commodore**. That rank's salary is 8,900, the
  campaign's final salary, paid through the campaign's own `salary: Free
  Worlds`, so a veteran never draws two.
* **Salary curve:** 200, 600, 1,800, 3,600, 6,000, 8,900, 12,000 (the user's
  200 → 600 → 1,800 → 3,600 "etc."; the rest is Claude's extrapolation).
* **Pirate raids** (side 4, slots 1..4), **relief flotillas** (kind 1, rarer
  at war), **named commanders** with personalities (`Admirals.dados`), and a
  **sim score** (`Quality.dados`, from mod API 3's `fleet_stat`) that weights
  a daily winner roll in battles and sieges. Weaker factions field bigger
  flotillas (`Quality.fleet_scale`, `balance_target` 85%), and the Republic
  keeps an edge, as the user wants. The simulation test's mock uses real
  `fleet_stat` values captured from the game (`REAL_STATS` in
  `test/mock.mjs`), so its balance numbers mean something. Rerun the capture if
  the base fleets change.
* **The mod panel is now a planet sub-panel**, swapped into the content area
  like the bank and trading panels (the user asked for this). A mod may also
  open it from `on_enter` while landing. A panel is closed only by the planet
  screen that owns it: loading a pilot builds the new planet screen before the
  old one is destroyed.

State lives in conditions named `diplomacy: …`. Test overrides: `diplomacy: force
active`, `force war` (a*10+b), `force peace`, `force raid`, `force flotilla` (takes effect on
the next arrival).

## Fixed bug (2026-10-26): the harness did not start on the ARM Mac

The user's log:

```
script: no memory for a VM
Dado mod harness did not start (code 1).
```

So no mod ever ran on the Mac. The plugins show in the plugin list because that
list comes from the data files, not from the harness. That is why
"the mods loaded but it's the normal game".

**Cause:** a bug in the C that Dado rc.8 emits. Its runtime prelude defines
`DADO_MAX_ALIGN` as the alignment of a union of `long double`, `void *` and
`long long`. On Apple ARM, `long double` is 8 bytes, so that comes out as 8.
`script.create` asks the heap for `mem.MAX_ALIGN`, which is 16, and the
emitted heap (`dado_rt__heap`) refuses any alignment above `DADO_MAX_ALIGN`, so
every allocation fails. You can check this anywhere with clang:
`--target=arm64-apple-macos11` gives `offsetof(probe, t) == 8`; x86_64 macOS,
aarch64 Linux and x86_64 Linux all give 16.

**Workaround in this repo:** `tools/fix-max-align.cmake` runs after `dadoc`
(from `Dado.cmake`; `dado-mods.sh` does the same with `sed`). It pins the
define to 16, which is what malloc guarantees on every platform the game
supports. Once Dado emits a correct value, the pattern is gone and the step
does nothing. **This should be fixed in Dado itself:** use 16, or `max_align_t`
together with the knowledge that macOS malloc is 16-aligned, or take
`mem.MAX_ALIGN`. The compiler's source is not in the public Dado repo, so the
fix is the user's to make.

**Status:** fixed. After a rebuild, Diplomacy ran on the Mac (the user saw it
working on 2026-10-26).

## The mod SDK (2026-10-26)

The separate repository `endless-sky-mod-sdk` lets people write mods without
the game's source, in DadoScript, JavaScript or TypeScript. It needs Node 18 or
newer (and `dadoc` for DadoScript). It provides:

* `imports/sdk/es/` and `dado.json`. The Dado language server resolves
  `import "imports:sdk/es"` from a project's `imports/` folder, which was
  verified with `dado-lsp`: no diagnostics, and a typo in a Game method is
  flagged. Hover shows only names, not doc comments; that is a dado-lsp
  limitation.
* `js/es.js` (the JavaScript `es.Mod`), `js/es.d.ts` (generated types) and
  `js/fake-game.js` (`FakeGame`, for tests). `npm install` links them as the
  package `es` in `node_modules`.
* `tools/esmod.mjs`, run as `npm run new | build | check | deploy | package`.
  A mod folder (`plugin.txt`, `data/`, `src/`) builds into
  `dist/<Plugin Name>/{plugin.txt,data/,dado/<id>.js}`, which can be dropped
  into `plugins/`. `package` zips it.
* `tools/sync.mjs` (`npm run sync -- <game repo>`) regenerates the SDK from
  `harness/Game.dados`, `sdk/es/Mod.dados` and `version.dado`, and checks
  `es.js` and `FakeGame` against them. **After changing the API here, run it
  there.** It needs every Game method documented, one line per signature.

Facts found by probing the real game, which the SDK relies on:

* The script engine has `print`, `println`, `queueMicrotask` and `performance`.
  It has no `console`, `setTimeout`, `structuredClone`, `Intl`, `URL`,
  `TextEncoder`, `fetch`, `process` or `require`. ES2023 array methods work.
* A Game call with the wrong type throws "`esmod.host_x`'s parameter `v`
  takes an int, and was given a float". A ship index out of range answers ""
  or 0, and does not throw.
* A plain-JavaScript mod that exports one class (including `export default`)
  loads like a DadoScript one. Verified in the game with all three SDK
  examples.

Mod API 4 (2026-10-26, evening) added `map_detail(system, title, text, r, g, b)`:
a section of the map's side panel, drawn by `ModHarness::DrawMapDetails` from
`MapDetailPanel::Draw` below the "orbits and key" box. The user found the long
marker labels cluttered: Diplomacy's markers are now only `Free Worlds flotilla
(small)` and the rest is in the side panel.

Mod API 5 (2026-10-27) added the outfitter button (`outfitter_button`;
hooks `can_outfitter`, `on_outfitter`), the player's fleet by UUID, outfit
queries, `ship_integrate` and `confirm` with the `on_confirm` hook. It is for
the "Outfit Integration" mod (`endless-sky-mod-sdk/mods/outfit_integration`):
integrating an outfit makes it permanent, 25% lighter and 25% smaller in
outfit space (savings rounded to whole numbers; the user does not want weapon
or engine capacity, cargo space or hardpoints changed)
for a fee of a quarter of its cost. The first build saved only mass; the user
reported "no weight freed" and wanted both, so `ship_integrate`'s `mass_scale`
became `scale` (API 5 was unreleased). Saves from that build (a fourth token,
the tons saved) load as the same reduction. The engine side is `Ship::Integrate`,
`IntegratedCount`/`RemovableCount` and the `integrated` save line, enforced in
`OutfitterPanel`, `BoardingPanel`, `GameAction` and `JettisonInstalled`
(README, "Integrated outfits"). The user chose: weapons may integrate and keep
their hardpoint and their place in the UI; ammunition may not; no outfit is
excluded from the mass reduction for now. Verified on 2026-10-27 in Claude's
aarch64 Linux VM: `dado-mods.sh test` (with `test/mods/api5_probe`) passes,
and every changed source passes `g++ -std=c++20 -fsyntax-only` (with SDL2,
OpenAL and GLEW headers fetched for it). The full game was not built there
(the VM has no SDL2 or cmake), so build it on the Mac.

Mod API 3 (2026-10-26) added `fleet_stat(fleet, stat)`, `system_fleet_count` and
`system_fleet`. It needed `Fleet::Variants()` in the game. The SDK is synced to
it.

## Traps already hit

* Endless Sky treats a folder as a plugin only if it has a `data`, `images`,
  `sounds` or `shaders` subfolder. A plugin that holds only `dado/` is ignored,
  so the debug plugin carries a nearly empty data file.
* In ES data text, `_` renders as underline markup, and a token containing
  quotes must be quoted with backticks.
* A `salary: X` condition is paid daily by the game itself.
* The harness starts before a pilot is loaded, so mods see an empty player
  whose date is 0. `SyncDay` treats a jump of more than 7 days, or a date that
  goes backwards, as a new pilot, and resyncs instead of catching up. Mods
  should do nothing while `game.here() == ""`. Diplomacy had activated itself
  on the menu player until this was fixed.
* `on_enter` runs after `on_day` on the step you arrive. Ships spawned on a
  takeoff or jump day are lost when the engine rebuilds its list
  (`ShipsReplaced`), so spawn from `on_enter`.
* The script VM belongs to its thread. Everything runs on the main thread.
* When staging files from the VM to the Mac: stage first, commit in a separate
  call, then compare checksums on the Mac. Doing it in one go sent stale files
  once.
* Dado quirks are listed at the end of README.md: no `extern "C"` in `--emit=h`;
  inner classes crash `dadoc`; there are no private `.dados` headers; there is
  no `const` in classes; a method named like a Dado function recurses (hence
  `host_*`); and mods bundle their own `es.Mod` (js-package plus esbuild).
* In `dado-mods.sh`, a shell function named `esbuild` shadowed the binary, so
  it is now `run_esbuild`.

## Delivered so far

* The harness, the example mod, the presets and the tests in the game repo,
  uncommitted on the Mac. A patch against `abb41b23` is
  `endless-sky-dado-harness.patch` in Claude's outputs (stale after
  2026-10-26; regenerate it with `git diff`).
* The Diplomacy mod with the simulation, in-game and debug tests, deployed in
  the Mac's `plugins/` folder.

* The user's real name was replaced with "Anaxarchus" everywhere at the
  user's request. Use Anaxarchus in copyright headers and plugin authors.
* In-game screenshots and tests load **every plugin in the game folder**. The
  Mac's `plugins/` has `Diplomacy Debug` deployed, which turns on debug mode
  and breaks three of the Diplomacy in-game tests; move it out before running
  `test/ingame/run.sh` against that folder. `Load First Savegame` loads the
  first save in the config, so clear `saves/` between screenshot runs.
* Testing in-game screens: `--test` runs headless. Add `--debug` to get a real
  window under Xvfb, wait about 45 s for loading, then `import -window root`
  captures it. Don't `pkill -f` a pattern that also matches your own shell
  command.

## Performance (2026-10-27)

The user asked whether to put a command buffer under `es.Mod` so that setters
are batched. Measured first (README, "Performance"): a call into the game is
about 40 ns, and appending a command to a script-side array is 50 ns, so a
buffer would cost more than it saves, and it would make reads after writes
stale and break the calls that answer something. Decided instead, with the
user: cache the lookups that walk a list, for one run of hooks
(`Caches` in the `Context`), fix the galaxy list that was never rebuilt after
events, and time every hook (`es_hook_timing`: slow calls logged, a summary at
shutdown). Bulk getters (one call answering a whole list) are the next step if
the timings ever show a mod reading many values each step.

### Diplomacy's on_day was slow (fixed in the game, 2026-10-27)

The user's timing summary: diplomacy on_day 102 ms average, 243 ms slowest (a
hitch on every jump with a capture). The dev build's per-call profile settled
it: `es_change` 5 calls, 327 ms, 65 ms each (Debug build). A capture is
`change()` with `system "X"` / `government "Y"`, and `PlayerInfo::AddChanges`
treated every `system` node as moving the galaxy: `GameData::UpdateSystems`
(every system's neighbors for every jump range, and every planet's
FinishLoading), rebuilding the seen systems, `CacheMissionInformation(true)`
and `RecheckStellarObjects`. None of those read a government, so
`AddChanges` now skips them when a system node only sets `government`
(`OnlyChangesGovernment` in PlayerInfo.cpp). This also speeds up the game's
own story events that hand systems over. Not yet measured in the game after
the fix: ask the user for the next Debug log.

Next in that profile, but spread thin (Debug numbers, unoptimized):
`condition` 40k calls and `system_government` 39k calls at ~2.7 us. In a
release build they cost far less; if they ever matter, Diplomacy can cache
system owners for the day (it reads them through `system_government` again
and again) and clear that on a capture.

## Mod API 6 and the Corporations mod (2026-10-27)

API 6 was added for "Corporations" (`endless-sky-mod-sdk/mods/corporations`),
a stock exchange the user asked for: about 40 companies (the 15 canonical
ones from the game's data never fail) bid for contracts; profits drive share
prices; invented companies go bankrupt or get bought; antitrust breaks up
companies that grow too large (the user's request: over max(35%, 2 / peers)
of a sector's capacity, or 15% of the exchange's value, for 60 days, canonical
companies included); the player trades at an Exchange button on every planet
with a bank. The design doc is a claude.ai Docs artifact, "Corporations &
Stock Exchange - Design Proposal".

API 6: `data`/`set_data` (per-mod text saved with the pilot in a `mod data`
block, percent-encoded by `PlayerInfo`), planets (`planet`, `planet_at`,
`planet_service`, ...), a scrolling table in the mod panel (`panel_columns`,
`panel_row`, hook `on_panel_row`), and `ask_number` with hook `on_number`
(the game's integer dialog; one pending request at a time). Tested by
`test/mods/api6_probe` in the fake game; the save encoding was round-tripped
over every byte value in Claude's VM. The panel table, the number dialog and
the save block are not yet seen in the real game: ask the user how they look.

The mod's simulation was balanced in Node over 3,000 to 8,000 days on a
human-space fixture built from the game's data (`tools/make-galaxy.mjs`):
listed companies stay 38 to 50; per year about 3.6 buyouts, 0.7
bankruptcies, 2 breakups, 2.4 listings; the largest company 12 to 16% of
the market. `tools/balance.mjs` prints a report. In QuickJS (the game's
engine) `on_day` is 2.6 ms on average, 3.6 ms at worst, half of it encoding
the 66 KB state it saves every day.

Mod API 7 (2026-10-27): the user found the exchange in the planet screen's
content area unusable (two companies visible at a time) and asked for a page
of its own like the outfitter's. `screen_open(title, text)` puts the mod's
panel on a full screen (`ModScreen` in ModHarness.cpp, pushed over the planet
screen): the table fills the left, a sidebar holds the text and buttons.
`panel_select(row)` marks a row; heading clicks run `on_panel_header`; Up and
Down step through rows; Escape closes. `ModView` is the base both views share
(table, buttons, clicks). `PanelOpen()` now means the planet screen's own
panel only. The exchange is one screen with Market, Portfolio, Contracts and
News tabs and a sortable market. Not yet seen in the real game: ask the user
how it looks (sidebar width 360, rows of 20 px).

## Ideas not done

* Hot reload: mods load once per run of the game, so changing a mod needs a
  restart. `Script.load` reads the file again each time it is called, so a
  reload command is feasible.
* Show harness failures in the game (a message on first landing, or in the
  plugin list), not only in the log. This one would have saved a round trip.
* Let a host publish classes that loaded scripts may import, so that mods
  don't have to bundle `es.Mod`.
* Diplomacy: more than one war at a time, Free Worlds–Syndicate contact,
  better flotilla AI, faction missions beyond Dispatches.
