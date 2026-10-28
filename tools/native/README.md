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


## Offline battle-action inspection

The same optional build also provides `nhBattleActions` (no install rule). It
reuses the trusted ordinary-save loader and shipping resource preset above:

```text
nhBattleActions POSTCOMBAT.vsgm1 SOURCE_LABEL --list
nhBattleActions POSTCOMBAT.vsgm1 SOURCE_LABEL --battle 0
```

`SOURCE_LABEL` is a caller-supplied candidate/evidence label, at most 128 bytes;
it is not independently verified build identity. The output also includes the
exporter build version, saved format, input SHA-256 and each packet's original
encoding version, chapter/day/index and global ordinal. The input is opened for
reading only and hashed before and after inspection. Successful JSON goes to
standard output only after all checks; exit 2 reports refusal on standard error.
Keep outputs in private evidence storage, since saves and battle logs can contain
custom player names and other private data.

List mode enumerates retained `BattleStart` intervals. Select the zero-based list
index explicitly. Selection requires the initial battle descriptor, one result
and `BattleEnded`; cancelled or incomplete intervals are refused. Orphan result/end
boundaries are counted, never synthesized. Every recorded packet in the selected
interval retains its type ID and provenance; selected packet families additionally
expose initial armies/stacks, round transitions, activations, action starts/ends,
actual spell announcements, damage snapshots, log text and result/end data. Event
`scope` distinguishes the selected battle from interleaved other battles, genuine
interval-only acknowledgments, and unclassified ownership. Unsummarized movement,
unit changes, Order-state changes and results-applied packets retain typed battle
IDs. Other unrecognized packets remain `unclassified`; absence from the typed
summary is not evidence that a packet has no battle owner. Initial stack count and
hex are the serialized `baseAmount`/`initialPosition` descriptors, read before the
existing application owner initializes live health and position.
`StartAction.command` is exported explicitly because `BattleAction::toString`
omits it. Order snapshots expose issuance identity and targets, with the full
snapshot retained in the hashed source packet.

A published action start is separate from a spell announcement or damage effect.
Counterspell-negated, passive and creature casts remain distinct fields. A
`PackageApplied` acknowledgment can report a failed request, but lacks its action
payload and battle ID; its association is only the selected packet interval.
Unpublished/rejected requests cannot be reconstructed as a complete attempted-action
list. No recorded actions can be recovered from a precombat save for a later fight.

Inspection refuses unknown provenance, decoder errors, nonordered chapter days,
duplicate open starts/results and exceeded bounds: 200,000 total packets, 4 MiB
per packet, 1,024 intervals, 20,000 selected-interval packets and 8 MiB output JSON.
These are trusted-input resource bounds, not a hostile-save sandbox. Existing
packet decoding does not expose an exact-consumption receipt for arbitrary embedded
trailing bytes; this tool does not add or relax serialization policy.

The decoder uses one detached historical world, loaded from retained known-version
chapter snapshots and advanced through recorded client packets via the existing
`CGameState::apply` owner (the same library path used by replay fast-forward).
This preserves historical army/slot bindings needed by `BattleStart`; the final
postcombat world alone is insufficient. Missing initial context is refused.
Snapshots are limited to 128 MiB and 10,000 chapters. Recording, script dispatch,
interrupted open battle intervals and timed contexts are refused. No gameplay command,
AI, server/GUI callback, turn solver or original-save write runs. The existing
recorded state visitor is reused; no new pack-application implementation is added.
Native fixture/save validation remains required before claiming this operational.
A complete retained interval is packet evidence, not solver timing or proof of
every hidden decision.
