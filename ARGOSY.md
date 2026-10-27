# Argosy

Argosy is a fork of [Endless Sky](https://github.com/endless-sky/endless-sky) that adds a native modding API for scripted plugins. It follows upstream Endless Sky closely and merges it regularly; without mods it plays like Endless Sky, apart from the optional gameplay patches. The game itself keeps Endless Sky's name, title, icons and identifiers for now.

How it differs from Endless Sky:

- **Native mod API.** The game opens mod loaders (native libraries) from `modloaders/` folders and talks to them through one C header, `source/modding/es_api.h`. See [docs/modding-api.md](docs/modding-api.md).
- **Bundled Dado loader.** Releases include the [Dado mod loader](https://github.com/Anaxarchus/es-modloader), which runs mods written in JavaScript or DadoScript from a plugin's `scripts/` folder. Mods are written with the mod SDK. See [docs/releasing.md](docs/releasing.md#the-bundled-mod-loader).
- **Gameplay patches.** Optional changes to gameplay, such as buying back used hulls, are kept as patches in [patches/](patches/README.md) and applied to release builds.
- **Its own config folder.** Saves, preferences, plugins and mod loaders go in an `argosy` folder, not Endless Sky's `endless-sky` folder, so the two games don't share anything:

  | | |
  |---|---|
  | macOS | `~/Library/Application Support/argosy/` |
  | Linux | `~/.local/share/argosy/` |
  | Windows | `%APPDATA%\argosy\` |

  Nothing is copied over from Endless Sky. To bring your saves, copy `saves/` from the old `endless-sky` folder into the `argosy` one.

More:

- [docs/releasing.md](docs/releasing.md): how releases are built and published.
- [docs/upstream.md](docs/upstream.md): merging upstream Endless Sky, and the weekly check that it still merges.
- [docs/modding-api.md](docs/modding-api.md): the mod API, for loader authors.
- [README.md](README.md): Endless Sky's own README (building, contributing, licensing).
