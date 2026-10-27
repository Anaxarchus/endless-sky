# Releasing this fork

Releases are built by GitHub Actions with [`.github/workflows/fork_release.yaml`](../.github/workflows/fork_release.yaml). Push a version tag and, once the builds finish, a GitHub Release appears with builds for Linux, Windows and macOS. The first run takes longest (an hour or more) because the vcpkg and ccache caches start empty.

## One-time setup

1. On GitHub, open the fork's **Actions** tab and click **I understand my workflows, go ahead and enable them**. Forks start with Actions turned off.
2. Leave **Settings > Actions > General > Workflow permissions** at the default (read). The release job asks for `contents: write` itself.
3. Commit and push `.github/workflows/fork_release.yaml` to `master`. GitHub only offers the manual **Run workflow** button for workflows on the default branch.
4. Publish the loader release named in `.github/modloader-version` (see [The bundled mod loader](#the-bundled-mod-loader)) before the first game release, or release with `bundle_loader=false`.

## Making a release

Upstream's version is in `CMakeLists.txt` (`project("Endless Sky" VERSION 0.11.4`) and in `credits.txt` (`version 0.11.4-alpha`). Name releases after it, with a fork counter:

```sh
git tag v0.11.4-mods.1
git push origin v0.11.4-mods.1
```

A tag that contains `-` is published as a **prerelease**. That fits while upstream's version is still an alpha. For a full release, use `+` instead, for example `v0.11.4+mods.1`.

The tag must start with `v`. The files drop the `v`: `Endless_Sky-0.11.4-mods.1-x86_64.AppImage`.

A tag push always applies the gameplay patches and bundles the mod loader. To build without them, or to release without making the tag yourself, start the workflow by hand (needs the [GitHub CLI](https://cli.github.com/)):

```sh
gh workflow run fork_release.yaml -f version=v0.11.4-mods.2 -f apply_patches=false
gh workflow run fork_release.yaml -f version=v0.11.4-mods.3 -f bundle_loader=false
gh run watch          # follow it
```

Or use **Actions > Fork Release > Run workflow** on the website. A manual run releases the commit at the tip of the branch you start it on (`--ref <branch>`, default `master`). It creates the tag for you, and it stops if a tag with that name already points at another commit.

If a run fails after the release was created, re-running the failed jobs updates the release and replaces its files.

## What gets built

| Job | Output |
|---|---|
| Setup | Works out the tag, the file version, the prerelease flag and whether to apply patches |
| Mod loader test | Runs `tests/modloader/run.sh` on Ubuntu (with `libsdl2-dev`), with the patches applied if they are used |
| AppImage x64 | `Endless_Sky-<version>-x86_64.AppImage` |
| Windows x64 | `EndlessSky-win64-<version>.zip` (portable) and `EndlessSky-<version>-win64-setup.exe` (NSIS installer) |
| MacOS x64, MacOS ARM | Intermediate app bundles, not published |
| MacOS Universal | `Endless-Sky-<version>.dmg` (one app for Apple Silicon and Intel) |
| GitHub Release | Publishes the four files plus `SHA256SUMS`, with generated notes |

Every build job runs `patches/apply.sh apply` right after checkout when patches are used. A patch that no longer applies fails the run before anything is published. Refresh it as described in [`patches/README.md`](../patches/README.md), or release with `apply_patches=false`.

The release notes list the patches applied, the mod API version (`ES_API_VERSION` from `source/modding/es_api.h`), the bundled loader's version (or where to get a loader), Argosy's config folder, and how to open the unsigned macOS build.

## The bundled mod loader

Every package ships the Dado loader from [Anaxarchus/es-modloader](https://github.com/Anaxarchus/es-modloader), so mods run without setup. The version is pinned in [`.github/modloader-version`](../.github/modloader-version), as `<repository> <tag>`:

```
Anaxarchus/es-modloader v0.1.0
```

To ship another loader version, publish it first (in es-modloader, `tools/release.sh <version> --publish`, which makes the release `v<version>` with one archive per platform and `SHA256SUMS`), then change the tag in that file and commit. The loader's `es_api.h` must match the game's API (`ES_API_VERSION`), or the game refuses it.

The [`fetch-modloader`](../.github/actions/fetch-modloader/action.yml) action, used by each packaging job, downloads the platform's archive and `SHA256SUMS` with `gh release download`, checks the checksum, and puts the library in a `modloaders/` folder, with the loader's README, licences and a `VERSION.txt` in `modloaders/esdado/` (the game only opens library files directly in `modloaders/`). It fails the run, before anything is published, if the release, the archive or its checksum line is missing or the checksum differs. The game finds the library through `Files::Resources()/modloaders`:

| Package | Where the loader goes | How |
|---|---|---|
| AppImage | `modloaders/libesdado.so` at the AppImage's root, next to `data/` and `credits.txt` | Placed in `AppDir/usr/share/games/endless-sky/modloaders/` before `utils/build_appimage.sh`, which moves that folder's contents to the AppDir root. A check step extracts the AppImage and fails if the loader is not there. |
| Windows zip | `modloaders\esdado.dll` next to `Endless Sky.exe` and `data\` | Copied into `install/release/` before zipping. |
| Windows installer | the same, in the install folder | `cpack -D CPACK_INSTALLED_DIRECTORIES=<stage>;/` adds the staged folder, so `CMakeLists.txt` is unchanged. The uninstaller removes it too. A check step lists both archives with `7z`. |
| macOS dmg | `Endless Sky.app/Contents/Resources/modloaders/libesdado.dylib` (universal) | Added to the universal app before the dmg is made. |

**macOS signing.** The loader's universal dylib is signed ad hoc by its release. This workflow does not sign the app: its binaries carry only the linker's ad hoc signature, with no sealed resources, so adding a file to `Resources/` breaks no signature. The check step verifies the dylib's signature and both architectures. If the app ever has a sealed signature at that point (`Contents/_CodeSignature/CodeResources`), the step re-signs the app ad hoc. If real signing and notarization are added later, put them after the loader step, sign `libesdado.dylib` with the same identity (`codesign --force --options runtime --timestamp -s "$IDENTITY"`) before signing the app, and keep in mind that a hardened-runtime app loads only libraries signed by its own team unless it has the `com.apple.security.cs.disable-library-validation` entitlement.

Players can override the bundled loader: a library of the same name in `<config>/modloaders/` replaces it, and any other loader there is opened beside it (see [modding-api.md](modding-api.md)).

The build jobs are copies of upstream's `cd_release.yaml` jobs, with the patch step added and the Steam and 32-bit Windows jobs left out. Each change from upstream is marked `Fork:` in the workflow. When upstream changes `cd_release.yaml` (a new vcpkg commit, new action versions or new packaging steps), make the same change in `fork_release.yaml`.

## Downloading

Open the fork's **Releases** page (`https://github.com/Anaxarchus/endless-sky/releases`), or use the CLI:

```sh
gh release download v0.11.4-mods.1 -R Anaxarchus/endless-sky -p '*.dmg' -p SHA256SUMS
shasum -a 256 -c SHA256SUMS --ignore-missing
```

- **Linux:** `chmod +x Endless_Sky-*.AppImage`, then run it.
- **Windows:** unzip and run `Endless Sky.exe`, or run the installer.
- **macOS:** open the dmg and drag the app to Applications. The build is not signed, so the first time right-click (or Control-click) the app and choose **Open**. On macOS 15 and later, go to **System Settings > Privacy & Security** and click **Open Anyway**.

Argosy keeps its own config folder: `~/Library/Application Support/argosy/` (macOS), `~/.local/share/argosy/` (Linux) or `%APPDATA%\argosy\` (Windows), not Endless Sky's `endless-sky` folder in the same place. It copies nothing from Endless Sky. To bring saves over, copy `saves/` from the old folder into the new one.

## Upstream workflows on the fork

Some upstream automation is guarded with `if: github.repository == 'endless-sky/endless-sky'`, so it is skipped on the fork and unchanged upstream:

- `cd.yaml`: the **Upload** job, which moves the `continuous` tag and publishes the "Continuous Build" release. Its three build jobs still run on pushes to `master`. They also save the build caches that the release builds restore.
- `projects_check.yml`: the `changed` job, so every job that depends on it is skipped as well.
- `cd_wiki.yaml` was already guarded upstream.

`ci.yml` still runs. Its **Data Files** job downloads a `continuous` release to parse data-only changes with; the fork has none, so on a fork it downloads upstream's (`endless-sky/endless-sky`) instead. That is a one-line change in `ci.yml`, marked `Argosy:`.

The fork's own workflow [`upstream_check.yaml`](../.github/workflows/upstream_check.yaml) checks every week that upstream still merges; see [upstream.md](upstream.md). GitHub turns off scheduled workflows in a fork until they are enabled: if it shows as disabled, open **Actions > Upstream Check** and click **Enable workflow**.
