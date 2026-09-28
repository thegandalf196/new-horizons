# Repository Guidelines

## New Horizons fork

Read `docs/NEW_HORIZONS_MVP.md` before choosing work. It defines this fork's
single-player, original-content MVP, ownership and validation contract. This is
not the separate Reconstruction repository. Current full-scale mod implementation
is authorized by `docs/NEW_HORIZONS_DESIGN.md`: prioritize fun and working AI;
defer numerical balance. Read it alongside the MVP baseline before new feature work.
Preserve VCMI licensing and existing
gameplay; use its internal connection and simulation thread instead of a separate
single-player server process. Historical references below to the server describe
the authoritative simulation role, not a requirement for a separate executable
or socket transport in this fork. Do not bypass command validation or directly
mutate gameplay state from the frontend.

## Important Patterns and Conventions

### C++ style

Follow the project's C++ conventions in [`docs/developers/Coding_Guidelines.md`](docs/developers/Coding_Guidelines.md) (formatting, naming, and general style).

### Cross-DLL types (`DLL_LINKAGE`)

For classes and structs that are serialized or otherwise shared across compile units, use the `DLL_LINKAGE` macro on the declaration:

```cpp
class DLL_LINKAGE CAddInfo {};
struct DLL_LINKAGE Bonus {};
```

### Constants and identifiers

Prefer existing constants over magic numbers or hard-coded strings:

- **Numeric IDs** (creatures, buildings, and other entities): `lib/constants/EntityIdentifiers.h`
- **String IDs** (configs, mods): `lib/constants/StringConstants.h`
- **Sizes and counts** (gameplay-related): `lib/constants/NumericConstants.h`
- **Game-related enums / mechanic states**: `lib/constants/Enumerations.h`

### Serialization and state

- **Serialization**: Use the custom serialization framework in `lib/serializer/`. Objects implement `h & object` pattern with serialization visitors. More details in [`docs/developers/Serialization.md`](docs/developers/Serialization.md).
- **Game state modifications**: Only server can modify state. Client can only send requests to change gamestate to server, server validates requests and sends resulting changes in gamestate to clients

## Code Architecture

### Key Concepts

#### Bonus System

One of the most important systems. Every bonus (attribute, resistance, spell immunity, etc.) is stored in a bonus system that propagates through a DAG of nodes. Key files:

- `lib/bonuses/Bonus.h` - Bonus structure
- `lib/bonuses/CBonusSystemNode.h` - Node in the bonus graph
- `lib/bonuses/IBonusBearer.h` - Interface for objects that can have bonuses

Bonuses have:

- **Propagators** - Rules for which ascendants receive the bonus
- **Limiters** - Restrictions on which descendants receive the bonus (e.g., only griffins)
- **Inheritance** is automatic through the DAG; propagation requires explicit propagators

More details in [`docs/developers/Bonus_System.md`](docs/developers/Bonus_System.md).

#### Game State

Accessed through `CGameState` in `lib/gameState/CGameState.h`. Contains:

- Map and all map objects
- Player information
- Heroes and their armies
- Game settings and options

See [`docs/developers/Code_Structure.md`](docs/developers/Code_Structure.md) for an overview of how game state fits into the overall architecture.

#### Callbacks

The lib exposes game state through callback interfaces:

- `CGameInfoCallback` - Read-only game info
- `CPlayerSpecificInfoCallback` - Player-specific visible state
- `CBattleCallback` - Battle-specific state
- `CCallback` - Server callback for client requests

AI and player interfaces use these callbacks rather than directly accessing game state.

#### Networking

Network layer:

- `lib/network/NetworkConnection.h` - Low-level connection
- `lib/networkPacks/` - Serializable network packets

All changes to game state must go through server via network packets. More details in [`docs/developers/Networking.md`](docs/developers/Networking.md).

#### Threading Model

- **MainGUI** - Main thread, input processing and rendering
- **runNetwork** - Network thread, processes incoming packets, runs combat AI reactions
- **runServer** - Server thread, processes requests and game state updates
- **AI tasks** - TBB-based tasks for adventure AI (Nullkiller)

See [`docs/developers/Code_Structure.md`](docs/developers/Code_Structure.md) for detailed threading information.

### Directory Structure

- **lib/** - Core static library `vcmiMain` with core game logic and data structures.
  - Aggregated into the shipped shared library `vcmi` (`VCMI_lib.dll` / `libvcmi.so`) by the **libFacade/** target along with all AI implementations and `vcmiLua`. Consumers always link the facade target `vcmi`, not `vcmiMain` directly.
  - **battle/** - Battle system (damage calculation, pathfinding, unit state)
  - **bonuses/** - Bonus system (core mechanics for granting attributes to units/heroes)
  - **callback/** - Interfaces for accessing game state (used by AI and client)
  - **entities/** - Game objects (heroes, creatures, artifacts, spells, buildings)
  - **gameState/** - CGameState and related classes
  - **mapping/** - Map loading/saving
  - **mapObjects/** - Adventure map objects (towns, dwellings, mines, etc.)
  - **mapObjectConstructors/** - Constructors for map objects
  - **network/** - Network layer (connection management)
  - **networkPacks/** - Network packet definitions and serialization
  - **json/** - JSON parsing/validation/writing
  - **filesystem/** - Archive and file loading
  - **spells/** - Spell definitions and casting logic
  - **modding/** - Mod loading and content management
  - **pathfinder/** - Hero pathfinding on adventure map
  - **serializer/** - Serialization framework (save/load, network)
  - **rmg/** - Random map generator
  - **campaign/** - Campaign progression and scenario logic
  - **texts/** - Text handling and localization support

- **client/** - Game client (backend-agnostic UI, logic, and interfaces)
  - **adventureMap/** - Adventure mode UI and logic
  - **battle/** - Battle UI and rendering
  - **gui/** - GUI framework (CIntObject base class and components)
  - **lobby/** - Local game setup screens
  - **globalLobby/** - Online/global lobby UI and client
  - **mainmenu/** - Main menu and game selection
  - **mapView/** - Map rendering
  - **render/** - Rendering abstractions and interfaces (backend-agnostic)
  - **widgets/** - Reusable UI widget components
  - **windows/** - Game windows and dialogs

- **clientsdl2/** - SDL2 rendering backend (events/, media/, render/)
- **clientsdl3/** - SDL3/GPU rendering backend (events/, media/, render/)

- **server/** - Game server (compiled as vcmiservercommon library, shared by serverapp)
  - **battles/** - Battle flow processing
  - **queries/** - Player queries and responses
  - **processors/** - Game state processors (turns, heroes, etc.)

- **serverapp/** - Standalone server executable entry point
- **clientapp/** - Client executable entry point (also handles iOS/Android specifics)
- **lobby/** - Standalone global lobby server (SQLite-backed, separate from game server)
- **launcher/** - Qt-based game launcher
- **mapeditor/** - Qt-based map editor
- **luascript/** - Lua scripting host, built as the static library `vcmiLua` and aggregated into `vcmi` by libFacade
- **libFacade/** - Tiny aggregator target that produces the shipped `vcmi` shared library by linking `vcmiMain` + `vcmiLua` + every enabled AI
- **config/** - Game configuration file (json-with-comments format)
- **scripts/** - Lua scripts used by the game
- **AI/** - AI modules, each built as a static archive and linked into the `vcmi` facade. They are constructed by name through `AIFactory` in `lib/callback/AIFactory.h`; there is no dynamic library loading.
  - **BattleAI/** - combat AI (default)
  - **Nullkiller2/** - Modern adventure map AI (default)
  - **MMAI/** - Machine-learning-based combat AI (experimental)
  - **StupidAI/** - Minimal combat AI for neutral/passive players
  - **EmptyAI/** - Stub AI (no-op, used for testing)

## Project Overview

VCMI is an engine for the game Heroes of Might and Magic III. The project is structured around a client-server architecture with a shared library. The codebase is primarily C++ with some CMake build configuration, Qt for UI (launcher and map editor), and Lua/ERM for scripting.

**Architecture**: One server process handles game state and mechanics. One or more client processes display the game and collect player input. Both use the shared VCMI lib.

## Build System

### CMake

The project uses CMake with Conan package manager for dependency management on Windows. On other platforms, system package managers are used. See [`docs/developers/CMake.md`](docs/developers/CMake.md) for all CMake options and [`docs/developers/Conan.md`](docs/developers/Conan.md) for Conan setup.

### C++ standard

VCMI is built as **C++20**: the root `CMakeLists.txt` sets `CMAKE_CXX_STANDARD` to `20` with `CMAKE_CXX_STANDARD_REQUIRED ON`. Treat C++20 as both the minimum supported language and the dialect to prefer for new code.

For platform-specific build and test instructions see [`docs/developers/Building_Windows.md`](docs/developers/Building_Windows.md), [`docs/developers/Building_Linux.md`](docs/developers/Building_Linux.md), [`docs/developers/Building_macOS.md`](docs/developers/Building_macOS.md), [`docs/developers/Building_Android.md`](docs/developers/Building_Android.md), [`docs/developers/Building_iOS.md`](docs/developers/Building_iOS.md).

## Common Development Tasks

### Adding a New Game Mechanic

1. Add bonus type or modify `lib/bonuses/BonusEnum.h` if needed
2. Implement logic in lib (typically in entity handlers or callback implementations)
3. Add serialization support if it affects saved games
4. Add serialization compatibility for older saves
5. Update network packets if client-server communication is needed
6. Add tests in `test/`
7. Update client UI if player-visible changes needed

### Modifying Battle Logic

Battle logic is split:

- `lib/battle/` - Core rules and state
- `server/battles/` - Server-side processing
- `client/battle/` - Rendering and UI

Changes to rules should go in `lib/battle/` (especially `BattleInfo.h`, `CBattleInfoCallback.h`, etc.), except for what an attack is worth - that is a Lua script, `scripts/damage/damageCalculator.lua`. See [`docs/developers/Battlefield.md`](docs/developers/Battlefield.md) for details on the battle system.

### Working with Configuration Files

Game configuration uses JSON:

- `config` directory contains configuration of all game entities and settings. It also contains JSON schemas for entities, under `config/schemas` path.
- JSON parsing: `lib/json/JsonParser.h`, `lib/json/JsonNode.h`
- JSON validation: `lib/json/JsonValidator.h`

Configuration is loaded by handlers in `lib/entities/` (creature handler, spell handler, hero handler, etc.).

## Logging

- `lib/logging/CLogger.h` - Logger class
- Logs are written to `vcmi.log`
- Most subsystems have named loggers: `logGlobal`, `logNetwork`, `logAi`, etc.

More details in [`docs/developers/Logging_API.md`](docs/developers/Logging_API.md).

## Dependencies

Major dependencies (managed by Conan):

- SDL2 - Graphics rendering
- Qt5/Qt6 - Launcher and map editor UI
- Boost - Various utilities
- FFmpeg - Video support
- Lua/LuaJIT - Scripting (optional), see [`docs/developers/Lua_Scripting_System.md`](docs/developers/Lua_Scripting_System.md)
- FuzzyLite - Fuzzy logic for AI
- Intel TBB - Parallel algorithms for AI and map generation


# Codex project instructions

For complex coding tasks, use the `astra-orchestrator` skill when its trigger conditions match.

The root agent owns architecture, decomposition, integration, and final verification.
Prefer specialized subagents for bounded exploration, implementation, testing, review, and technical research.

Do not delegate trivial work merely for parallelism.
Do not let multiple implementation agents edit the same files without explicit ownership boundaries.
User instructions always take precedence over this orchestration policy.

## Persistent user-priority queue

Read `docs/NH_USER_PRIORITY_QUEUE.md` before choosing or resuming work, including
after context compaction or an automatic goal continuation. Immediately record
new user-assigned tasks there, with concrete requirements and acceptance evidence.
Resolve its open tasks before returning to the ordinary implementation backlog;
do not silently substitute another workstream or drop an item from memory.
If blocked, record the blocker and work on another unblocked queue item. Ask for
direction if all queue items are blocked; do not silently bypass this priority.
Preserve safely running processes and unrelated changes when switching work.
Distinguish source implementation, verification, and playable delivery. A source
edit or build alone does not close a reported visual/runtime defect. Keep resolved
entries with their evidence. This queue tracks work, not gameplay authority:
`New Horizons.md` remains canonical and design amendments belong in Pending Changes.

## Heroes III UI visual construction

Before creating or revising New Horizons UI, read
`docs/NH_HOMM3_UI_STYLE_GUIDE.md` and apply its outside-in panel-construction
and native-resolution review checklist. Treat mockups as layout/interaction
guides, not permission to reproduce pasted-together visual treatment. Preserve
gameplay behavior unless the user separately requests a functional change.


# New Horizons Development Phases

New Horizons is being developed in explicit phases.

The root orchestrator owns the current phase, task prioritization, delegation,
integration, and phase transitions.

Do not optimize for the goals of a later phase while the current phase remains
incomplete.

Current phase: PHASE 1 — IMPLEMENTATION COVERAGE

The governing priorities are:

PHASE 1:
Specification coverage > test-suite perfection.

PHASE 2:
Integration correctness > new feature development.

PHASE 3:
Playtest evidence > theoretical balance assumptions.


## Phase 1 — Implementation Coverage

Primary objective:

Implement the complete Version 1.0 specification.

During this phase, maximize specification coverage. Missing specified mechanics
and content take precedence over increasingly exhaustive verification of systems
that already function.

Track coverage explicitly.

At minimum, maintain counts/status for:

- Orders
- combat Spells
- Adventure Spells
- Skills and Skill ranks
- Skill perks
- Faction Skills
- Faction perks
- Hero Action / Creature Activation mechanics
- Leadership
- Siege and War Machines
- Luck and Morale
- creature mechanics
- town/building mechanics
- artifacts and specialties
- recruitment and Diplomacy
- required combat UI
- required hero-development UI
- required adventure-magic UI
- save-state representation
- minimum AI hooks required to exercise implemented mechanics

A missing specification item normally outranks additional integration tests for
an already functioning item.

### Phase 1 task priority

Choose work in approximately this order:

1. Missing foundational mechanic required by other Version 1.0 features
2. Missing P0 Version 1.0 feature
3. Missing P1 Version 1.0 feature
4. Missing specified spell, Skill, perk, Order, faction mechanic, creature
   mechanic, building, artifact, specialty, or other content
5. Missing UI required to exercise an implemented mechanic
6. Basic correctness defects blocking implementation
7. Deferred integration hardening
8. Optimization
9. Polish
10. Numerical balance refinement

Do not repeatedly revisit an implemented subsystem merely because more tests,
refactoring, or polish could be added.

Do not allow broad integration work to consume the implementation schedule while
substantial Version 1.0 specification coverage remains absent.


### Phase 1 feature completion

A feature is complete enough to move on when:

1. The intended mechanic exists in production code.
2. Required registration/data/configuration exists.
3. Its principal execution path works.
4. Required UI or interaction hooks exist sufficiently to exercise it.
5. It builds successfully.
6. It does not introduce an obvious crash, corrupt state, corrupt saves, or
   violate a foundational invariant.
7. Focused tests or deterministic verification establish basic correctness.
8. Known cross-system interactions that remain unverified are recorded for
   Phase 2.

Do not require exhaustive cross-system validation before moving to the next
specified feature.


### Phase 1 testing policy

Testing remains mandatory, but it is scoped to implementation.

Workers should run focused validation for their own changes.

Testers should prefer the smallest deterministic test command that establishes
whether the delegated behavior works.

Continuously enforce fast gates such as:

- build/compile success
- focused unit tests
- relevant existing tests
- registration/data validation
- basic serialization sanity
- deterministic mechanic tests
- smoke tests
- crash detection
- important invariants

Do not normally run the entire repository integration suite after every bounded
feature.

Do not block Phase 1 implementation on:

- exhaustive interaction matrices
- unrelated failing integration tests
- broad regression suites unrelated to the change
- large AI simulations
- balance assertions
- cosmetic discrepancies
- numerical tuning disagreements
- edge cases involving systems that have not themselves been implemented yet

Broad integration suites should be run periodically in batches, at meaningful
integration checkpoints, rather than mechanically after every implementation
task.

Immediately stop and repair a problem when it indicates:

- crashes
- memory/state corruption
- save corruption
- foundational architectural breakage
- pervasive deterministic failure
- an abstraction that prevents continued implementation

Otherwise, record the integration issue and continue increasing specification
coverage.


### Phase 1 delegation

Use explorers when the relevant implementation surface is genuinely unclear.

Do not repeatedly re-explore already mapped architecture without evidence that
it has changed.

Use workers aggressively for independent missing specification items when file
ownership can be separated safely.

Parallelize independent implementation workstreams.

Examples:

- separate spell implementations
- independent Skills/perk families
- unrelated UI components
- different town/building mechanics
- isolated creature mechanics

Do not assign multiple workers overlapping ownership of the same files unless
the root has explicitly partitioned responsibilities.

The tester should validate implemented behavior, not turn every feature into a
full-system certification exercise.

The reviewer should look for material correctness, regression, integrity,
compatibility, and high-value missing-test risks. Phase 1 review findings should
be classified as either:

BLOCKING:
crash, corruption, foundational regression, incorrect core mechanic, or issue
that prevents continued implementation.

DEFERRED:
cross-system edge case, broad regression coverage, optimization, polish,
non-critical compatibility concern, or balance issue suitable for Phase 2/3.

Deferred findings must be recorded but should not automatically prevent the next
coverage task.


### Phase 1 numerical policy

Most numerical values in New Horizons are prototypes or experimental balance
values.

Do not spend substantial Phase 1 time repeatedly tuning:

- Spell coefficients
- Mana costs
- Order coefficients
- Leadership progression
- creature Leadership requirements
- Skill weights
- Luck/Morale curves
- creature growth
- creature prices
- economy values
- artifact values
- AI valuation constants

unless the current value prevents meaningful functional testing.

Implement the specified mechanic faithfully first.

Balance later.


### Phase 1 progress metric

The principal project metric during this phase is implementation coverage.

Report concrete coverage rather than using test count as the primary measure of
progress.

Example:

Orders:             8 / 8
Combat Spells:     54 / 66
Adventure Spells:   5 / 5
Generic Skills:    21 / 21
Generic Perks:    176 / 210
Faction Skills:     9 / 9
Faction Perks:     61 / 90

Use actual specification-derived totals. Do not invent totals.

At the end of each substantial orchestration cycle report:

1. Current development phase
2. Specification items completed
3. Coverage changes
4. Focused validation performed
5. Deferred integration issues discovered
6. Blocking issues, if any
7. Next highest-priority missing specification item

The preferred outcome of a Phase 1 cycle is increased specification coverage,
not merely increased test count.


### Phase 1 exit condition

Do not leave Phase 1 because the test suite has become comprehensive.

Leave Phase 1 when Version 1.0 specification coverage is substantially complete.

Before transition, perform a specification audit and report:

- implemented items
- partially implemented items
- missing Version 1.0 items
- intentionally deferred non-Version-1.0 work
- deferred integration defects
- known failing broad tests
- unverified interaction classes
- provisional balance areas

Then explicitly transition to Phase 2.


## Phase 2 — Stabilization and Integration

Primary objective:

Make the implemented Version 1.0 systems work correctly together.

During this phase:

Integration correctness > new feature development.

Feature creation largely stops.

Resolve the deferred integration backlog systematically.

Construct high-value interaction coverage, including where applicable:

- Spell × Magic Resistance
- Spell × Magical Damage Reduction
- Spell × Dispel
- Spell × Spell Lock
- Spell × Time Stop
- Spell × School perk
- Spell × Spellcraft
- Spell × Wisdom
- Spell × Faction Skill

- Order × Command
- Order × perk
- Order × Warcasting
- Order × Faction Skill

- Creature ability × Spell
- Creature ability × Order
- Creature ability × status

- Creature Activation × initiative
- Creature Activation × Wait
- Creature Activation × Morale
- Creature Activation × Second Wind
- Creature Activation × Seize Initiative
- extra activation × extra activation restrictions

- death × Resurrection
- death × Re-animate
- death × Disintegrate
- death × Necromancy
- death × Elemental Rebirth
- casualty provenance × restoration

- Leadership × recruitment
- Leadership × army transfer
- Leadership × upgrades
- Leadership × Diplomacy

- Siege × War Machines
- Siege × fortifications
- Siege × repairs
- Siege × defensive towers

- save/load × permanent state
- save/load × combat state
- save/load × temporary effects
- save/load × town state

- UI prediction × actual resolution
- AI decision-making × newly implemented mechanics

During Phase 2, use the tester and reviewer substantially more aggressively.

Run broad regression suites frequently.

Fix interaction ordering, effect lifecycles, targeting legality, serialization,
initiative consistency, action economy, AI integration, and UI/result
disagreement.

Do not introduce substantial new mechanics while integration remains unstable.


### Phase 2 priority

1. Crash/corruption
2. Save/load correctness
3. Deterministically incorrect mechanics
4. Hero Action / Creature Activation integrity
5. Initiative and extra-activation integrity
6. targeting legality
7. status/effect lifecycle
8. cross-system interactions
9. AI use of implemented systems
10. UI prediction/result consistency
11. remaining regression coverage


### Phase 2 exit condition

Enter Phase 3 when:

- Version 1.0 mechanics reliably interact
- major integration matrices have useful automated coverage
- broad regression tests are consistently healthy
- save/load is reliable
- AI can exercise major new systems
- UI predictions generally match actual outcomes
- remaining issues are mainly balance, usability, polish, or isolated edge cases


## Phase 3 — Playtesting, Balance, Polish, Release Hardening

Primary objective:

Determine whether the completed game plays well.

During this phase:

Playtest evidence > theoretical balance assumptions.

Now tune:

- spell costs and coefficients
- Order strength
- Attribute conversion
- Leadership
- creature requirements
- creature stats
- growth
- recruitment costs
- dwelling costs
- Skill weights
- perks
- Faction Skills
- Luck and Morale probabilities
- War Machines and Siege
- artifacts
- specialties
- Recruitment
- Diplomacy
- buildings
- economy
- adventure Movement
- Adventure Magic
- AI valuation
- pacing

Use reproducible gameplay evidence, controlled scenarios, simulations where
appropriate, and real human playtests.

When changing a value, identify the gameplay problem the change is intended to
solve.

Prefer isolated changes.

Use:

Observe
-> Reproduce
-> Identify responsible mechanic
-> Change smallest relevant rule/value
-> Run relevant regression tests
-> Replay scenario
-> Compare
-> Keep, revise, or revert

Do not balance mechanics in isolation from the systems that determine their real
value.


### Phase 3 polish

After mechanics and balance stabilize, address:

- UI clarity
- tooltips
- targeting feedback
- animation timing
- sound
- visual consistency
- accessibility
- error reporting
- performance
- loading
- save compatibility
- edge-case handling


### Phase 3 exit condition

Version 1.0 becomes a release candidate when:

- intended specification coverage is complete
- major integration defects are resolved
- automated regression tests are healthy
- representative games can be completed normally
- AI can use the new systems
- balance is acceptable for the release target
- UI accurately communicates major mechanics
- save/load is reliable
- no known release blocker remains


## Global anti-drift rule

Before starting work, identify the current phase and ask:

"What produces the greatest progress according to this phase's governing
priority?"

During Phase 1, another missing Spell, Skill, perk, creature mechanic, building,
or required UI path is usually more valuable than another layer of tests around
a feature that already has adequate focused verification.

During Phase 2, fixing interactions is usually more valuable than adding another
feature.

During Phase 3, measured gameplay evidence is usually more valuable than
speculative numerical redesign.

Do not confuse engineering activity with progress.
