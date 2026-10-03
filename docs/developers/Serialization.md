# Serialization

## Necromancy Ossuary destination

`NEW_HORIZONS_NECROMANCY_OSSUARY` appends the result's `ossuaryTown` object ID
after special-casualty summaries. NONE denotes normal Hero delivery; a populated
town ID requires an active, successfully applied, positive raised army and no
capacity-block marker. Older records reset NONE; unsupported populated direct
and enclosing writes reject before payload bytes. Actual destination validation
and whole-batch army admission happen at post-battle authority time; the UI
consumes the selected destination without recalculating nearest towns.

## Special Necromancy casualty pools

`NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES` appends captured nonliving and
Undead eligible casualty maps and an explicit capture flag to `BattleResult`.
Older records reset these to empty/false; raw casualty maps cannot supply the
new perk inputs. The same boundary appends four nonnegative summary counts:
Death Lord and Grave Knowledge eligible inputs and their generated base
Skeleton-equivalent contributions, before category conversions. Unsupported
populated direct/enclosing writes reject before payload bytes. This does not
add ongoing-battle save/resume.

## Master of Bones output form

`NEW_HORIZONS_NECROMANCY_SKELETON_FORM` appends `skeletonCreature` after the
Wight count. NONE denotes the base/legacy Skeleton output; a populated form
identifies the actual configured upgraded creature, even in a mixed reward.
Older records reset the field to NONE. Direct and enclosing result writers
reject a populated form under unsupported versions before their payloads.
An explicit form requires a nonnegative creature ID and positive Skeleton
output count. Army and UI consumers use the authoritative form, not a separate
client-side perk or town-availability calculation.

## Soul Harvester result payload

`NEW_HORIZONS_NECROMANCY_WIGHTS` appends `wightsRaised` to the existing
Necromancy result without reordering older fields. Older summaries read with
zero Wights. Nonzero Wight outputs cannot be written in an older format:
both the result and enclosing `BattleResultsApplied` reject the write before
their payload. Negative Wight counts are rejected on write and read. Mixed
outputs use the explicit counts, not the legacy single-stack descriptor.

## Ordered usable casualty provenance

`BATTLE_CASUALTY_PROVENANCE` identifies state updates retaining ordered usable
casualty cohorts in each health JSON snapshot. Cohorts carry count, actual damage
nature and temporary-restoration identity. Resurrection selects newest usable
casualties first; temporary expiry preserves the prior death cause, while a new
death takes its new cause. Destroyed remains stay separate and non-restorable.
The original Battle Form health ledger owns provenance while a form is active.

Old snapshots with no recorded causes retain unknown prior deaths as OTHER;
they must not fabricate magical casualty history. New events record explicit
damage nature. UnitChanges, injury, attack and health-change packets reject
lossy older-format writes before their payload. Current JSON state snapshots
preserve matching health and ordered provenance together.

This does not add ongoing-battle save/resume. Binary CStack/BattleInfo/BattleStart
descriptors still omit general CUnitState; a casualty-only sidecar cannot restore
matching health or temporary resurrection. These descriptors reject nonlegacy
casualty provenance even in the current format rather than misrepresenting it.
Ordinary adventure saves do not contain ongoing battles and remain supported.

## Resolved initial deployment ordering

`BATTLE_INITIAL_DEPLOYMENT_ORDER` appends the resolved first side to the generic
`BattleDeploymentState`. Older records default to attacker-first. Initial
opportunities follow that resolved ordering, skipping sides without eligibility;
the separate final-relocation stage remains attacker-first. Live updates may
complete only the current opportunity and cannot change resolved ordering.
Non-default ordering cannot be written to older formats: the state, enclosing
BattleInfo, BattleStart and deployment-change packet reject before their payloads.
Grand Tactics resolves this field during battle setup; it adds no separate action
or duplicated client-side deployment allowance.

## New Horizons Portal of Summoning source

`NEW_HORIZONS_PORTAL_SOURCE` appends the town's external source-dwelling ID and
last successful selection absolute week. Troop stock remains on the dwelling;
the town serializes only the link and quota marker. Old town records default to
no source and week -1; populated state cannot be written to an older format.
The new SelectPortalDwelling and SetPortalDwellingSource packets have appended
polymorphic type IDs. RecruitCreatures appends optional Portal-town context;
ordinary older records default to none, while a populated context fails closed
on an older writer before its payload. Authoritative recruitment validates the
owned built Portal, matching selected still-owned source, and town-associated
destination before charging resources or deducting the source's real stock.

## New Horizons immediate Double Command continuation

`NEW_HORIZONS_DOUBLE_COMMAND` appends a per-side combat-used marker and contextual
Order/Second Wind continuation, plus an ORDER-only allowance source. Accepted
`StartAction` and `BattleHeroOrderStateChanged` packets carry validated optional
transitions. Older records default to unused; writers targeting an older format
reject populated state or grants before writing bytes rather than dropping them.

Battle descriptors validate ledger, round, Order and unit references on decode.
Alive, ghost and current-controller checks require initialized unit state and run
after `BattleStart::localInit` and before writes. This does not add midbattle
save/resume: binary stack descriptors omit `CUnitState`, and ordinary game saves
do not serialize ongoing battles. Descriptor roundtrip tests must not initialize
fresh health and present that as restored combat state.

## Bonus effect hostility

`BONUS_EFFECT_HOSTILITY` appends `Bonus::appliedByEnemy`, a target-relative
application-time provenance flag. Current binary and bonus JSON snapshots
preserve it. Older readers cannot represent a populated flag, so down-saving
one is rejected before the bonus is written. Old records load false: their
unrecorded caster allegiance is not inferred. Dynamic propagated aura ownership
continues to use the existing owner updater and limiter, not this field.

## New Horizons Land Surveyor weekly allowance

`NEW_HORIZONS_LAND_SURVEYOR` appends a hero's last successful rewarded mine-
capture absolute week. Older hero records default to -1 (unused); a spent
marker cannot be down-saved to a format unable to retain it. The authoritative
capture path replicates the marker through the existing `SetObjectProperty`
packet's appended `NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK` property. That property
is rejected under an older wire version rather than silently discarded. Mine
ownership and resource grants continue through their ordinary packets; there is
no periodic allowance-reset scan and no new packet type registration.

## New Horizons battle Mana expenditure

`BATTLE_HERO_MANA_EXPENDITURE` adds accepted hero spell costs and paid opposing
Counterspell ward costs to `BattleSpellCast`, and the cumulative gross payment
ledger to each `SideInBattle`. Old records load zero; nonzero expenditure may
not be down-saved to a format that cannot represent it. Negative amounts and
inconsistent caster/ward metadata are rejected. Separate restoration, Buffer
grants and hostile drains do not reconstruct or alter this ledger.

Mana Conservation consumes the ledger at authoritative battle finalization and
restores only Normal Spell Points after ordinary result cleanup. This state
extension does not certify full battle-form binary save support; that separate
state still fails closed rather than losing creature-form provenance.

## Introduction

The serializer translates between objects living in our code (like int or CGameState\*) and stream of bytes. Having objects represented as a stream of bytes is useful. Such bytes can send through the network connection (so client and server can communicate) or written to the disk (savegames).

VCMI uses binary format. The primitive types are simply copied from memory, more complex structures are represented as a sequence of primitives.

### Typical tasks

#### Bumping a version number

Different major version of VCMI likely change the format of the save game. Every save game needs a version identifier, that loading can work properly. Backward compatibility isn't supported for now. The version identifier is a constant named version in Connection.h and should be updated every major VCMI version or development version if the format has been changed. Do not change this constant if it's not required as it leads to full rebuilds. Why should the version be updated? If VCMI cannot detect "invalid" save games the program behaviour is random and undefined. It mostly results in a crash. The reason can be anything from null pointer exceptions, index out of bounds exceptions(ok, they aren't available in c++, but you know what I mean:) or invalid objects loading(too much elements in a vector, etc...) This should be avoided at least for public VCMI releases.

#### Adding a new class

If you want your class to be serializable (eg. being storable in a savegame) you need to define a serialize method template, as described in [#User types](#user-types)

Additionally, if your class is part of one of registered object hierarchies (basically: if it derives from CGObjectInstance, IPropagator, ILimiter, CBonusSystemNode, CPack) it needs to be registered. Just add an appropriate entry in the `RegisterTypes.h` file. See polymorphic serialization for more information.

## How does it work

### Primitive types

They are simply stored in a binary form, as in memory. Compatibility is ensued through the following means:

- VCMI uses internally types that have constant, defined size (like int32_t - has 32 bits on all platforms)
- serializer stores information about its endianness

It's not "really" portable, yet it works properly across all platforms we currently support.

### Dependant types

#### Pointers

Storing pointers mechanics can be and almost always is customized. See [#Additional features](#additional-features).

In the most basic form storing pointer simply sends the object state and loading pointer allocates an object (using "new" operator) and fills its state with the stored data.

#### Arrays

Serializing array is simply serializing all its elements.

### Standard library types

#### STL Containers

First the container size is stored, then every single contained element.

Supported STL types include:

`vector`  
`array`  
`set`  
`unordered_set`  
`list`  
`string`  
`pair`  
`map`

#### Smart pointers

Smart pointers at the moment are treated as the raw C-style pointers. This is very bad and dangerous for shared_ptr and is expected to be fixed somewhen in the future.

The list of supported data types from standard library:

`shared_ptr (partial!!!)`  
`unique_ptr`

#### Boost

Additionally, a few types for Boost are supported as well:

`variant`  
`optional`

### User types

To make the user-defined type serializable, it has to provide a template method serialize. The first argument (typed as template parameter) is a reference to serializer. The second one is version number.

Serializer provides an operator& that is internally expanded to `<<` when serialziing or `>>` when deserializing.

Serializer provides a public bool field `saving`that set to true during serialization and to false for deserialization.

Typically, serializing class involves serializing all its members (given that they are serializable). Sample:

``` cpp
/// The rumor struct consists of a rumor name and text.
struct DLL_LINKAGE Rumor
{
	std::string name;
	std::string text;

	template <typename Handler>
	void serialize(Handler & h, const int version)
	{
		h & name;
		h & text;
	}
};
```

### Backwards compatibility

Serializer, before sending any data, stores its version number. It is passed as the parameter to the serialize method, so conditional code ensuring backwards compatibility can be added.

Yet, because of numerous changes to our game data structure, providing means of backwards compatibility is not feasible. The versioning feature is rarely used.

Sample:

``` cpp
/// The rumor struct consists of a rumor name and text.
struct DLL_LINKAGE Rumor
{
	std::string name; //introduced in version 1065
	std::string text;

	template <typename Handler>
	void serialize(Handler & h, const int version)
	{
		if(version >= 1065)
			h & name;
		else //when loading old savegame
			name = "no name"; //set name to a sane default value
	
		h & text;
	}
};
```

### Serializer classes

#### Common information

Serializer classes provide iostream-like interface with operator `<<` for serialization and operator `>>` for deserialization. Serializer upon creation will retrieve/store some metadata (version number, endianness), so even if no object is actually serialized, some data will be passed.

#### Serialization to file

CLoadFile/CSaveFile classes allow to read data to file and store data to file. They take filename as the first parameter in constructor and, optionally, the minimum supported version number (default to the current version). If the construction fails (no file or wrong file) the exception is thrown.

#### Networking

See [Networking](Networking.md)

### Additional features

Here is the list of additional custom features serialzier provides. Most of them can be turned on and off.

- Polymorphic serialization — no flag to control it, turned on by calls to registerType.
- Vectorized list member serialization — enabled by smartVectorMembersSerialization flag.
- Stack instance serialization — enabled by sendStackInstanceByIds flag.
- Smart pointer serialization — enabled by smartPointerSerialization flag.

#### Polymorphic serialization

Serializer is to recognize the true type of object under the pointer if classes of that hierarchy were previously registered.

This means that following will work

``` cpp
Derived *d = new Derived();
Base *basePtr = d;
CSaveFile output("test.dat");
output << b;
//
Base *basePtr = nullptr;
CLoadFile input("test.dat");
input >> basePtr; //a new Derived object will be put under the pointer
```

Class hierarchies that are now registered to benefit from this feature are mostly adventure map object (CGObjectInstance) and network packs (CPack). See the RegisterTypes.h file for the full list.

It is crucial that classes are registered in the same order in the both serializers (storing and loading).

#### Vectorized list member serialization

Both client and server store their own copies of game state and VLC (handlers with data from config). Many game logic objects are stored in the vectors and possess a unique id number that represent also their position in such vector.

The vectorised game objects are:

`CGObjectInstance`  
`CGHeroInstance`  
`CCreature`  
`CArtifact`  
`CArtifactInstance`  
`CQuest`

For this to work, serializer needs an access to gamestate library classes. This is done by calling a method `CSerializer::addStdVecItems(CGameState *gs, LibClasses *lib)`.

When the game ends (or gamestate pointer is invaldiated for another reason) this feature needs to be turned off by toggling its flag.

When vectorized member serialization is turned on, serializing pointer to such object denotes not sending an object itself but rather its identity. For example:

``` cpp
//Server code
CCreature *someCreature = ...;
connection << someCreature;
```

the last line is equivalent to

``` cpp
connection << someCreature->idNumber;
```

``` cpp
//Client code
CCreature *someCreature = nullptr;
connection >> someCreature;
```

the last line is equivalent to

``` cpp
CreatureID id;
connection >> id;
someCreature = VLC->creh->creatures[id.getNum()];
```

Important: this means that the object state is not serialized.

This feature makes sense only for server-client network communication.

#### Stack instance serialization

This feature works very much like the vectorised object serialization. It is like its special case for stack instances that are not vectorised (each hero owns its map). When this option is turned on, sending CStackInstance\* will actually send an owning object (town, hero, garrison, etc) id and the stack slot position.

For this to work, obviously, both sides of the connection need to have exactly the same copies of an armed object and its stacks.

This feature depends on vectorised member serialization being turned on. (Sending owning object by id.)

#### Smart pointer serialization

Note: name is unfortunate, this feature is not about smart pointers (like shared-ptr and unique_ptr). It is for raw C-style pointers, that happen to point to the same object.

This feature makes it that multiple pointers pointing to the same object are not stored twice.

Each time a pointer is stored, a unique id is given to it. If the same pointer is stored a second time, its contents is not serialized — serializer just stores a reference to the id.

For example:

``` cpp
Foo * a = new Foo();
Foo * b = b;

{
	CSaveFile test("test.txt");
	test << a << b;
}

Foo *loadedA, *loadedB;
{
	CLoadFile test("test.txt");
	test >> loadedA >> loadedB;
	//now both pointers point to the same object
	assert(loadedA == loadedB);
}
```

The feature recognizes pointers by addresses. Therefore it allows mixing pointers to base and derived classes. However, it does not allow serializing classes with multiple inheritance using a "non-first" base (other bases have a certain address offset from the actual object).

Pointer cycles are properly handled. This feature makes sense for savegames and is turned on for them.
