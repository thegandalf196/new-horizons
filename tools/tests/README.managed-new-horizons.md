# Private managed70 test fixture

This is an opt-in **test-data** recipe, not a default activation or player package.
It installs no executable, invokes no compiler/game, and never copies purchaser
assets. Its pinned historical input module remains 0.5.1 with 69 common spells;
the live canonical module may advance independently.

## Inputs and rights

`new-horizons-managed-inputs.json` pins an explicit keyset of 945 common resources
and 18 VCMI test resources from commit
`d90c2ea7ae80456eae06f17f7286cf8debd8e705`. The composer pins that manifest's hash
and rejects missing, changed, symlinked or additional selected inputs. It does
not discover arbitrary dirty/untracked files by recursively copying `config`.
In particular, it does **not** read `config/newHorizonsSpells.json`.

`fixtures/new-horizons-magic-missile.json` is the separately pinned, original
GPL-2.0-or-later test definition. It references existing core game resources;
it contains no new image pixels or purchaser files. These references are
provisional, not an assertion of original artwork or third-party redistribution
rights. Existing checked-in resource licensing remains unchanged.

Composition explicitly adds the tracked partial-Conflux category rules/texts,
then the private Missile definition and saved v2 formula (20 + 20 times scaled
power, cost 5, Sorcery level 1). Exactly two common resource bodies change:
`Mods/new-horizons/mod.json` and the v1-or-v2 field in
`config/schemas/gameSettings.json`. All other common resource bytes are pinned.
The generated identity records the actual composer hash separately from the
pinned input-resource commit; it does not imply that the new fixture/composer
already existed in d90. A newer source baseline needs an explicit reviewed
pin/transformation update, not automatic acceptance of changed inputs.

## Linux setup (not executed by the composer)

First install the normal Linux build dependencies and initialize required source
submodules; see `docs/developers/Building_Linux.md`. Configure the real opt-in graph:

```sh
cmake --preset new-horizons-linux -DENABLE_TEST=ON -DENABLE_NEW_HORIZONS_MANAGED_TESTS=ON
cmake --build build/new-horizons-linux --target vcmitest
```

Choose a **new absolute** output path below this checkout's `build/` and an
existing absolute purchaser `Data` directory. No source/output symlink escape or
overlap with purchaser Data is permitted. Directory-symlink support is required.
Generated identities contain local paths: keep the entire profile private/ignored.

```sh
python3 tools/tests/prepare_new_horizons_managed_profile.py \
  --out "$PWD/build/managed70-reproduction" \
  --data /absolute/path/to/purchaser/Data \
  --permit-private-test-import
```

The directory must not already exist, even if empty. Failures preserve any partial
output; inspect it and use a new output identity after repair. Do not retry in
place, edit the default module, or import somebody else's saves/settings.

The following are separate, explicit native-test actions, **not composer behavior**:

```sh
build="$PWD/build/new-horizons-linux"
profile="$PWD/build/managed70-reproduction"
cp -- "$build/bin/vcmitest" "$profile/runner/vcmitest"
sha256sum "$profile/runner/vcmitest" "$build/bin/libvcmi.so"
(
  cd "$profile/runner"
  XDG_DATA_HOME="$profile/data" XDG_CONFIG_HOME="$profile/config" \
  XDG_CACHE_HOME="$profile/cache" LD_LIBRARY_PATH="$build/bin" \
  NH_REQUIRE_MANAGED_MISSILE_PROFILE=1 NH_REQUIRE_MASTERY_TEXTS=1 \
  timeout 180s ./vcmitest --gtest_filter='*NewHorizons*:*HeroCommand*' \
    --gtest_output="xml:$profile/results.xml" > "$profile/native.log" 2>&1
)
```

Record the actual CMake cache/graph, command exit, executable/facade hashes, source
and data identities and XML. The d90 opt-in baseline is **264 tests: 253 pass,
11 existing skips**, including all 17 managed cases, 2 AI cases and 11 authored-map
cases without skips. A zero-test result, additional skip or failure is not success.
The default-OFF graph has 234 cases; do not silently substitute that graph.
Restore the declared default with `-UENABLE_NEW_HORIZONS_MANAGED_TESTS` when done.

## Bootstrap boundaries

The regular `CVcmiTestConfig` uses the TEST preset. `ModManager` deliberately
force-activates `vcmi-test`, regardless of persisted preset contents, and the
Library mounts the test fixture search path. This recipe includes that mod on
purpose. Removing its directory/preset entry is **not** a normal-player test.

A separate native harness can use a reviewed private environment calling
`initializeFilesystem(false, false)`, replacing the original test-environment
object exactly once. That is normal **Library** initialization within a native
harness, not normal client startup, rendering, ordinary acquisition or gameplay.
This tool neither creates that overlay nor authorizes a GUI run.

The Hero Luck/Morale save exporter has a narrower maintained opt-in:
`NH_EXPORT_HERO_PROC_ORDINARY_BOOTSTRAP=1` selects ordinary library initialization
only with the exact test filter
`NewHorizonsHeroProcFixtureExportTest.ExportLegalOfferOrderProcAdventureSave`,
`NH_EXPORT_HERO_PROC_FAMILY=attacker` or `defender`, and an explicit
`NH_EXPORT_HERO_PROC_SAVE_ROOT` matching the isolated runtime save directory.
Supply the ordinary `config/vcmi/modSettings.json` preset with only `core`,
`vcmi`, and `new-horizons`. The exporter rejects other resolved mods. Without
this opt-in, normal tests still use the TEST preset. A save exported with that
test preset can reference test-only hero-pool entities and is not a player
fixture. Native export/reload remains distinct from client listing and loading.

Offline composer tests, byte-equivalence inspection, registration/native tests,
normal client bootstrap and GUI/save/AI journeys are separate evidence. Merely
checking in this recipe or seeing its output does not prove checkout-native
reproduction or player acceptance.

## Guarded Windows test profiles

The separate `nh-private-launch.py --wine-fullgame` route requires a
witness-capable player executable, pinned Wine/DLL/input-helper providers, an
owned private Xvfb guard, and a fresh private root outside the checkout. Its
caller must independently bound the entire run and own display cleanup. It is
not a replacement for the normal Windows launcher or evidence of gameplay
acceptance.

`--wine-content-manifest` and `--wine-content-manifest-sha256` select private
test inputs. The version-1 JSON has these exact top-level fields:

- `version`: integer `1`.
- `original`: `data` and `music`, each with an absolute directory `path` and
  a nonempty `files` object mapping relative filenames to SHA-256 hashes.
- `map`: absolute `path`, `sha256`, and a plain `.vmap` or `.h3m` `filename`.
- `settings` and `modPreset`: absolute `path` and `sha256` for each JSON file.

The preset is the curated default with `vcmi`, `core`, and `new-horizons`, and
an empty preset settings object. Admission is read-only. Before launch, staging
links external Data/Mp3 directories and copies only the selected map and small
configuration files into the disposable profile. Links are resource references,
not an OS-enforced read-only sandbox. Only declared original files are verified;
the manifest must list every original resource used by the test.

Original bytes are verified before launch and after cleanup, outside the timed
owner window. Map/link checks remain active during input; configuration files
may legitimately be rewritten by the engine. Keep manifests, profiles, saves,
logs, and proprietary inputs private and outside disposable build directories.

A game can exit while the helper performs its post-gesture check. When the
same retained game process has been reaped with status zero and its pidfd
confirms exit, the helper stops input and records `terminal_input` without an
`input_done` acknowledgement. This records process exit only; visual Quit
acceptance remains separate. The original bootstrap must still exit successfully.
If that post-gesture check loses access to the selected game's process files,
the helper allows at most two seconds for this exit proof, within the existing
input deadline. A live, unknown, or unsuccessful exit still fails verification.

Set `NH_PYTHON` to a verified absolute interpreter path, then run these
process-free regressions:

```sh
"${NH_PYTHON:?Set NH_PYTHON to a verified absolute interpreter}" tools/tests/test_nh_private_launch.py
"$NH_PYTHON" tools/tests/test_nh_wine_fullgame_owner.py
"$NH_PYTHON" tools/tests/test_nh_wine_input_completion.py
"$NH_PYTHON" -m unittest discover -s tools/tests -p test_nh_wine_profile.py
```
