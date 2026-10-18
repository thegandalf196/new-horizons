# Native save comparison

`nhSaveParity` is an optional local inspection tool, not a game launcher or save
converter. It has no install rule and is not included in player packages.

## Build and resources

Use the ordinary [Linux build dependencies](../../docs/developers/Building_Linux.md).
From the source root, configure a separate full-library build:

```sh
cmake -S . -B build/save-parity -DENABLE_SAVE_PARITY_TOOL=ON \
  -DENABLE_MINIMAL_LIB=OFF -DENABLE_CLIENT=OFF -DENABLE_SERVER=OFF \
  -DENABLE_TEST=OFF -DENABLE_LAUNCHER=OFF -DENABLE_EDITOR=OFF \
  -DENABLE_LOBBY=OFF -DENABLE_TRANSLATIONS=OFF
cmake --build build/save-parity --target nhSaveParity --parallel 10
```

The option defaults to OFF. Minimal-library builds cannot inspect full saves.
The target links the ordinary engine facade and server-state implementation.

Use a separate ordinary profile with the shipping `core`, `vcmi` and
`new-horizons` content and required original game resources available through the
normal engine resource paths. Do not activate TEST or additional mods. On Linux,
one supported resource layout is the complete ordinary staged client root: run
with that directory as the working directory, retaining its real client executable,
`config` and `Mods`. `VCMIDirs` recognizes this existing layout and uses its resources.
A directory containing only `nhSaveParity`, `config` and `Mods` does not qualify;
otherwise the ordinary installed/XDG data paths apply. Merely changing the working
directory does not select a profile or supply missing original resources.

## Compare

Invoke the built executable by its absolute path, using three trusted save paths:

```text
nhSaveParity START.vsgm1 LEFT.vsgm1 RIGHT.vsgm1
```

START is the common starting human-turn boundary. LEFT and RIGHT must each be
exactly the next day with the same sole active human player. Both must retain the
exact starting replay snapshot/packet prefix, completed starting turn and resumed
next human turn. Missing or expired coverage is refused, not treated as equality.
Timed games and active battles are unsupported. Each input must be a regular,
non-symlink save of at most 128 MiB, with a supported native replay-bearing format
and no trailing payload. This is for trusted bounded inputs, not a security sandbox
for hostile save files. It does not run turns, execute map script chunks or publish
gameplay packets; ordinary loading retains its separate script initialization.

Exit codes are `0` for equal compared channels, `1` for a difference, and `2` for
usage errors or refused/invalid inputs. Successful comparisons emit schema-1 JSON
with `equal`, `first_difference`, format versions and channel byte lengths; errors
are reported on standard error. A reported difference is not an accepted parity run.

Comparison retains native full-world and full-server state, recorded replay effects,
all serialized GameRandomizer streams and HeroPool randomizers. Only statistics
timestamps are normalized, with restoration on the detached inspection owner;
statistics outcomes and other fields remain compared. Independent decodes and
repeated captures must be stable. The tool compares saved representation and
recorded effects, not every internal AI choice, unrecorded decision or turn cost.
Representative gameplay parity and performance require separately matched runs.
