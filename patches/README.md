# Optional gameplay patches

Without a mod loader installed, this fork plays exactly like upstream Endless Sky. These patches hold the gameplay changes that differ from upstream, so they can be applied on top when wanted. Each one is a unified diff against the fork's source tree, and the patches apply independently of one another.

| Patch | What it does |
|---|---|
| `used-hulls.patch` | Lets you buy back used hulls. A ship you sell goes into the shipyard's stock as its base model until you take off, like the outfits sold with it. The shipyard lists such hulls even for models it doesn't sell, labels them ("2 used in stock", "In stock: 5 (2 used)"), sells used hulls first at their depreciated price, and words the name prompt as "used" or "new and used". The stock is saved with the pilot as a `ship in stock` block and dropped for models that are no longer valid. The modding API's `ship_in_stock` then returns the real count; without the patch it always returns 0. Files: `PlayerInfo.cpp/.h`, `ShipyardPanel.cpp/.h`, `ModHarness.cpp`. |
| `government-change-speedup.patch` | Skips recomputing the galaxy's neighbors, routes and seen systems when an event's change to a system only sets its government, which saves tens of milliseconds per change. Gameplay is unchanged except in timing. File: `PlayerInfo.cpp`. |
| `mission-outfits-from-storage.patch` | Lets missions use what you keep in storage. While you are landed, a mission action that takes outfits (a negative `outfit` line, as in a job's `on accept`) also counts the outfits in storage on that planet when deciding whether it can be done, and takes them from your cargo hold first, then that planet's storage, then the flagship. The message names where they came from. It only widens what counts, so nothing that worked before stops working. The Shoal mod's workshop needs it for materials too heavy to carry at once. Files: `GameAction.cpp`, `MissionAction.cpp`. |

## Using them

Run these from anywhere in the repository:

```sh
patches/apply.sh check                  # does every patch apply cleanly?
patches/apply.sh apply                  # apply them all
patches/apply.sh apply used-hulls       # or just one
patches/apply.sh status                 # which are applied?
patches/apply.sh revert                 # take them out again
```

`apply.sh` checks every patch before it changes anything. `apply` and `check` skip a patch that is already applied, and `revert` skips one that is not applied; each says so and goes on with the rest. A patch that no longer fits the source stops everything, with git's message. It uses `git apply`, which also works in a plain copy of the source.

## Keeping them current

The patches are maintained along with the fork: whenever the base moves (an upstream merge, or a change to the files they touch), they are refreshed so that they still apply cleanly to the current tree. To refresh them after an upstream merge:

1. Start from a clean tree of the merged base, and run `patches/apply.sh check`. If it passes, the patches still fit. `git apply` tolerates shifted lines, but regenerate anyway (step 4) so the line numbers stay exact.
2. If a patch no longer applies, apply it with conflict markers: `git apply --3way patches/<name>.patch` in a git checkout. Or apply what fits with `git apply --reject patches/<name>.patch` and finish the `.rej` hunks by hand.
3. Resolve the conflicts so the feature works against the new upstream code. Build the game, and play-test the shipyard or the event change.
4. Regenerate the patch from the difference between the base and the patched files, and keep the `a/` and `b/` paths relative to the repository root. In a git checkout where the base is committed:
   ```sh
   git diff -- source/PlayerInfo.cpp source/PlayerInfo.h source/ShipyardPanel.cpp \
       source/ShipyardPanel.h source/ModHarness.cpp > patches/used-hulls.patch
   git checkout -- source/        # back to the base
   ```
   Do one patch at a time, so that each patch holds only its own feature.
5. Check that each patch applies alone and that both apply together (`patches/apply.sh check`, then `apply` and `revert`), and commit the refreshed patches with the merge.
