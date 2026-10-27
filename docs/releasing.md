# Releasing this fork

Releases are built by GitHub Actions with [`.github/workflows/fork_release.yaml`](../.github/workflows/fork_release.yaml). Push a version tag and, once the builds finish, a GitHub Release appears with builds for Linux, Windows and macOS. The first run takes longest (an hour or more) because the vcpkg and ccache caches start empty.

## One-time setup

1. On GitHub, open the fork's **Actions** tab and click **I understand my workflows, go ahead and enable them**. Forks start with Actions turned off.
2. Leave **Settings > Actions > General > Workflow permissions** at the default (read). The release job asks for `contents: write` itself.
3. Commit and push `.github/workflows/fork_release.yaml` to `master`. GitHub only offers the manual **Run workflow** button for workflows on the default branch.

## Making a release

Upstream's version is in `CMakeLists.txt` (`project("Endless Sky" VERSION 0.11.4`) and in `credits.txt` (`version 0.11.4-alpha`). Name releases after it, with a fork counter:

```sh
git tag v0.11.4-mods.1
git push origin v0.11.4-mods.1
```

A tag that contains `-` is published as a **prerelease**. That fits while upstream's version is still an alpha. For a full release, use `+` instead, for example `v0.11.4+mods.1`.

The tag must start with `v`. The files drop the `v`: `Endless_Sky-0.11.4-mods.1-x86_64.AppImage`.

A tag push always applies the gameplay patches. To build without them, or to release without making the tag yourself, start the workflow by hand (needs the [GitHub CLI](https://cli.github.com/)):

```sh
gh workflow run fork_release.yaml -f version=v0.11.4-mods.2 -f apply_patches=false
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

The release notes list the patches applied, the mod API version (`ES_API_VERSION` from `source/modding/es_api.h`), where to get a mod loader ([Anaxarchus/es-modloader](https://github.com/Anaxarchus/es-modloader)), and how to open the unsigned macOS build.

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

## Upstream workflows on the fork

Some upstream automation is guarded with `if: github.repository == 'endless-sky/endless-sky'`, so it is skipped on the fork and unchanged upstream:

- `cd.yaml`: the **Upload** job, which moves the `continuous` tag and publishes the "Continuous Build" release. Its three build jobs still run on pushes to `master`. They also save the build caches that the release builds restore.
- `projects_check.yml`: the `changed` job, so every job that depends on it is skipped as well.
- `cd_wiki.yaml` was already guarded upstream, and scheduled workflows don't run on forks anyway.

`ci.yml` still runs. One catch: its **Data Files** job downloads the `continuous` release when a push changes only data files, and the fork has no `continuous` release, so that job fails on data-only pushes.
