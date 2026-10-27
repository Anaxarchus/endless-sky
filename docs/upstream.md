# Merging upstream Endless Sky

Argosy follows upstream Endless Sky ([endless-sky/endless-sky](https://github.com/endless-sky/endless-sky), the `upstream` remote) by merging its `master` from time to time. To keep those merges easy, the fork changes upstream files as little as it can: the mod API lives in new files (`source/modding/`, `source/ModHarness.*`, `tests/modloader/`), gameplay changes are kept as optional patches in [`patches/`](../patches/README.md), and fork-only automation is in new files under `.github/`. Where an upstream file had to change, the change is small and usually marked with a `Fork:` or `Argosy:` comment.

## The weekly check

[`.github/workflows/upstream_check.yaml`](../.github/workflows/upstream_check.yaml) runs every Monday, and by hand from **Actions > Upstream Check > Run workflow**. It never runs in upstream's repository, and it never commits or pushes anything.

1. **Merge and check.** It checks out the fork with full history, fetches upstream `master`, and runs [`.github/upstream-check.sh`](../.github/upstream-check.sh), which tries `git merge --no-commit --no-ff upstream/master` in the runner. If that conflicts, it records the conflicting files and stops. If it merges (or there is nothing new), it runs, on the merged tree:
   - `patches/apply.sh check` (do the gameplay patches still apply?)
   - `tests/modloader/run.sh` (the mod loader host test, with `libsdl2-dev`)
   - `utils/check_code_style.py`
   - `utils/check_cmake.sh`
2. **Build** (optional; on the weekly run, and on a manual run unless `build` is unchecked). If the merge succeeded, it merges again in a fresh runner, applies the patches when they fit, and builds and tests the game like `ci.yml`'s Ubuntu job (`linux-ci` preset, then `ctest`). The vcpkg and ccache caches make later runs faster than the first.
3. **Report.** It keeps one open issue labelled `upstream-check`. When anything fails, it opens that issue, or comments on it if it is already open, with the upstream commit range (and a compare link), the conflicting files, the patch output and the tail of any failing test or build. When everything passes again, it comments and closes the issue. The run's summary page shows the same report, and the logs are attached to the run as the `upstream-check-report` artifact.

The workflow needs only `contents: read` and, for the report, `issues: write` (and `actions: read` for the build's vcpkg cache, as in `ci.yml`). GitHub turns off scheduled workflows in forks until they are enabled: if the workflow shows as disabled, click **Enable workflow** on its page.

The same check runs locally in a throwaway copy (it leaves the copy mid-merge):

```sh
rsync -a --exclude /build/ --exclude /vcpkg/ ./ ../up-test/ && cd ../up-test
git add -A && git commit -qm wip   # in the copy only: the check needs a clean tree
.github/upstream-check.sh /tmp/upstream-report   # SDL_INCLUDE=<folder with SDL2/> if not /usr/include
cat /tmp/upstream-report/summary.md
```

## Doing the real merge

1. Start from a clean `master`, and update it:
   ```sh
   git fetch upstream
   git merge upstream/master
   ```
2. **Conflicts.** Resolve them keeping both sides: upstream's change, plus the fork's addition. The files the fork touches most are `source/Ship.cpp`, `source/ShipyardPanel.cpp`, `source/OutfitterPanel.cpp`, `source/PlanetPanel.cpp`, `source/PlayerInfo.cpp`, `source/Engine.cpp`, `source/CMakeLists.txt` and the workflows (`git diff --stat upstream/master HEAD` lists them all). For a workflow, take upstream's version and redo the marked fork lines.
3. **New source files.** If upstream added or renamed files, `utils/check_cmake.sh` says whether `source/CMakeLists.txt` lists them all.
4. **The patches.** Run `patches/apply.sh check`. If a patch no longer applies, refresh it as [`patches/README.md`](../patches/README.md) describes, and commit the refreshed patches with the merge.
5. **The mod API.** Build the game and run `tests/modloader/run.sh`. If upstream changed something the API uses (ships, outfits, planets, the player, panels), fix `source/ModHarness.cpp`. If `source/modding/es_api.h` changed (the fork's own change, made during the merge), bump `ES_API_VERSION` as the header describes, and in the loader's repository run `tools/sync-api.sh`, rebuild, release a new loader and update [`.github/modloader-version`](../.github/modloader-version) (see [releasing.md](releasing.md#the-bundled-mod-loader)).
6. **Workflows.** If upstream changed `cd_release.yaml` (a new vcpkg commit, action versions, packaging steps), make the same change in `fork_release.yaml` (see [releasing.md](releasing.md)). If it changed `utils/build_appimage.sh` or the Windows install rules, check that the bundled loader still lands next to `data/`: the release's check steps fail if it does not.
7. Run `utils/check_code_style.py`, commit the merge, and push. The next weekly check (or a manual run) should then close any open `upstream-check` issue.
