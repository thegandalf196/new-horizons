# Fixed native rules

New Horizons executes its shipped damage, spell, combat-event and imported map
rules in C++. `GameLibrary` installs `NativeModule`; `NativeScriptFactory` retains
the authored identifiers, schemas, priorities and supported patch-chain identity.
JSON remains the authored data format. Unknown replacement scripts and changed
patch chains are rejected rather than ignored.

The factory covers 42 spell effects, 16 combat-event identities and one damage
calculator. Native damage first captures owned current facts, then runs the pure
numeric calculation. It does not cache damage by unit IDs. Movement queries have
a separate [owned-input and exact-reuse contract](Movement_Queries.md).

## Build and package contract

Production targets do not link Lua. `ENABLE_LUA_ORACLE` defaults to `OFF`; enabling
it requires tests and the full library, and builds the historical backend only for
differential tests. Old `.lua` sources stay in the checkout as reference inputs and
are excluded from native distributions. On macOS, install LuaJIT separately only
when building that optional oracle; it is not part of the default Brewfile.

Curated packaging expects a matching native binary and facade built from these
sources. Supplying an arbitrary older Lua-dependent executable to the new resource
packager is unsupported. The existing bug-report feature receipt does not attest
native-rule capability; verify the selected build identity independently. The
launcher can still link an existing legacy package's scripts directory when that
directory is supplied, without making it a requirement of a native package.

## Map and saved-rule compatibility

Saved policy parameters and existing binary field layouts remain in use. Imported
HotA events are represented as versioned JSON expressions and actions inside the
existing map script field. Native continuation frames handle questions, rewards
and combat suspension. The strict importer translates the recognized historical
generated Lua grammar without executing it; arbitrary authored or modified Lua is
not supported.

Newly saved native map-event programs require this engine. Keep pre-migration saves
when testing rollback to an older engine. Retained archive entry names and enum
labels containing `Lua` protect existing format identities and do not imply a
runtime interpreter. This change does not add serialization of suspended events.

The old interpreter's sibling-table iteration order can vary. Native Timed effects
use stable key order. Disease's physical-affliction stamp, and later absolute
stamp values, can therefore differ while fixed-rule selection order remains the
same in the valid, non-overflow domain. Do not assume byte-identical packets or
save state across this migration. `NativeTimedSiblingOrderTest` exercises the
bounded shipped families, recasts, cleanup and later affliction selection; broader
gameplay and save/load checks remain necessary before accepting a release.

## Verification boundaries

Standalone map-import, registration and event-dispatch tests require no original
game assets. The optional JSON parity test exercises the real historical Lua
conversion boundary. `test/nativeDamage` compares production arithmetic against
the old script chain with synthetic observations; its benchmark excludes live
fact capture and is not a complete damage-call or battle speed measurement.

`ENABLE_NATIVE_MIGRATION_TESTS=ON`, with tests and the oracle enabled, builds
`nhNativeMigrationTest`. It links the changed resource-backed map, combat, spell,
damage-capture and AI fixtures with normal game initialization. Original game data
is required. These fixtures also remain in `vcmitest`; the focused executable does
not replace the full regression suite or gameplay acceptance.
