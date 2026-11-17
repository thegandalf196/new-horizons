# Replay persistence

Replay chapters retain a coherent synchronous game-state snapshot and the ordered
packets applied after it. Snapshot creation uses the authoritative world ownership
and graph guards; no worker serializes a mutable live world. The current captured
rule table is shared across heroes inside each snapshot, and each replay payload
retains its own encoding version.

## Recording options

`server.rollingReplayEnabled` controls recent-turn recording for **new games**.
It defaults to `true`. The separate **Record Game** option always enables full-game
recording, including when rolling recording is disabled. The effective capture
policy is saved in format 1163 and retained on load; changing the local setting
never enables, disables or erases an existing game's replay history.

`server.replayRoundsKept` defaults to `1`: the current day and that many previous
days retain complete snapshots. Setting it to zero still records the current day;
it is not an off switch. Full-game recording retains the ordered history while
older intermediate snapshots may be released under the existing retention rules.

With rolling recording disabled and Record Game unselected, both authority and
client skip replay snapshot/packet encoding, including prepared battle transactions.
Command validation, prediction inspection, world commits and RNG remain on their
ordinary paths. A populated log cannot be switched to disabled capture. A disabled
log cannot accept chapters; it does not later resume across an unrecorded gap.

## Snapshot and save boundaries

World snapshots omit nested replay history to avoid recursive snapshots. Their
empty placeholder retains the saved capture opt-out. Ordinary saves retain their
complete replay log and capture policy. Standalone packet snapshots remain
self-contained and preserve their own captured rules and version provenance.
Readers validate a complete candidate replay log before replacement. Unsupported
writers reject disabled capture before an enclosing prefix.

Save compression and lazy initialization of unrecruited heroes are separate work.
The current container remains unchanged. Lazy initialization must preserve existing
hero generation, RNG order, uniqueness and recruitment semantics; a type/seed pair
alone is not an equivalent initialization receipt.
