/*
 * ESerializationVersion.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/// This enumeration controls save compatibility support.
/// - 'MINIMAL' represents the oldest supported version counter. A saved game can be loaded if its version is at least 'MINIMAL'.
/// - 'CURRENT' represents the current save version. Saved games are created using the 'CURRENT' version.
///
/// To make a save-breaking change:
/// - change 'MINIMAL' to a value higher than 'CURRENT'
/// - remove all keys in enumeration between 'MINIMAL' and 'CURRENT' as well as all their usage (will be detected by compiler)
/// - change 'CURRENT' to 'CURRENT = MINIMAL'
///
/// To make a non-breaking change:
/// - add new enumeration value before 'CURRENT'
/// - change 'CURRENT' to 'CURRENT = NEW_TEST_KEY'.
///
/// To check for version in serialize() call use form
/// if (h.hasFeature(Handler::Version::NEW_TEST_KEY))
///     h & newKey; // loading/saving save of a new version
/// else
///     newKey = saneDefaultValue; // loading of old save
enum class ESerializationVersion : int32_t
{
	NONE = 0,

	HOTA_MAP_STACK_COUNT = 893, // support Hota 1.7 stack count feature

	HOTA_MAP_FORMAT_EXTENSIONS, // support multiple Hota 1.7 map format features
	SPELL_RESEARCH_IMPROVEMENTS, // support counting past spell rerolls
	NAME_MAP_LAYERS, // name map layers
	HOTA_MAP_FORMAT_EXTENSIONS_2, // more Hota 1.7 map format features
	TIMER_MOVEMENT_POINTS, // movement points for timer
	DISABLE_TACTICS, // disable tactics
	REWARDABLE_EXTENSIONS_2, // movement points limiter for rewardables
	BONUS_TRIGGER, // bonus that allows triggered effects in combat
	CUSTOM_GARRISON_TITLE, // GarrisonDialog pack now has custom title parameter
	LUA_SCRIPTS,
	REWARDABLE_RESET_CALENDAR, // rewardable reset period split into days/weeks/months
	CONTROL_LOSS_TRACKING, // track when players ever controlled special defeat-condition objects
	QUEST_REWORK, // quest objects reshape: persist requiredKeys / allowedDifficulties limiter fields, quest-log identity (object or keymaster-colour type)
	HERO_SPECIALTY_ROUNDING, // DivideStackLevelUpdater carries hero level for H3-correct specialty rounding
	MAP_GEN_LEVEL_MAP_LAYERS, // CMapGenOptions per-level map layer IDs
	RETREAT_PERMISSION_BONUSES, // BATTLE_NO_FLEEING replaced by BATTLE_CAN_FLEE, escape tunnel provides bonus instead of hardcoded effect
	SCRIPT_VARIABLES, // per-map script variable storage (mod-namespaced key/value store)
	GAME_REPLAY_RECORDING, // recording of the game (and the battle ID counter it needs), stored in the savegame
	COMBAT_ABILITY_SCRIPTS, // combat abilities that became combat scripts are converted on load; spell effects and combat scripts share one registry, so bonus subtype saves the script as a string
	GAME_SESSION_DIRECTORY, // persistent per-game save directory and campaign start time
	SCENARIO_EVENT_JOURNAL, // per-player history of triggered scenario event messages and components
	MUTARE_DRAKE_OVERRIDE, // campaign header stores hero type override used for Mutare Drake crossover bonus targeting
	TOWN_NAME_TEXT_ID, // renaming a town registers the new name in the map text container instead of storing free-form text
	RECORD_TEXTS_METASTRING, // highscore scenario name and statistics map name are stored unresolved, to be rendered by the reader

	TOWN_CUSTOM_INITIAL_GARRISON, // preserve map-authored initial town armies, including explicit empty armies
	HERO_COMMANDS, // versioned per-game combat rules, action budget and battle Doctrines
	NEW_HORIZONS_MAGIC, // saved school/membership rules and stable school serialization
	NEW_HORIZONS_HERO_GROWTH, // saved world/hero profiles, scaled ratings and primary gains
	NEW_HORIZONS_CAPABILITIES, // independent saved leadership/siege world and hero identity
	NEW_HORIZONS_MASTERIES, // saved post-Expert eligibility, pending offers and chosen effects
	NEW_HORIZONS_CREATURE_CATEGORIES, // independent explicit world/battle category snapshots

	NEW_HORIZONS_LOGISTICS_MASTERIES, // two-family pre-gain eligibility and pending choices

	NEW_HORIZONS_TARGETED_COMMANDS, // exact target/cohort/premium and v2 combat rules
	NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE, // spell-specific Magic Arrow cast parameter
	NEW_HORIZONS_PERKS, // saved generic New Horizons skill/perk registry and hero selections
	NEW_HORIZONS_PERK_OFFERS, // combined level-up perk candidates and replicated selections
	NEW_HORIZONS_SELECTIVE_DISPEL, // optional Sorcery Dispel mode carried by battle actions
	NEW_HORIZONS_TEMPORAL_FIELD, // optional once-per-combat Sorcery Mass Slow state and action data
	NEW_HORIZONS_COUNTERSPELL, // optional reusable Sorcery Counterspell ward state and cast result
	NEW_HORIZONS_CANONICAL_ORDERS, // authoritative state for the eight canonical Orders
	NEW_HORIZONS_NECROMANCY, // count-based faction conversion and post-battle result summary
	NEW_HORIZONS_LAND_MINE, // player-selected canonical Land Mine action vectors
	NEW_HORIZONS_FIRE_WALL, // player-selected Fire Wall orientation and per-activation trigger state
	NEW_HORIZONS_METAMAGIC, // authoritative Tower Metamagic sequence state and cast metadata
	NEW_HORIZONS_TIME_STOP, // authoritative Time Stop stasis marker and expiry semantics
	NEW_HORIZONS_TIME_STOP_ORIGINS, // simultaneous caster-side Time Stop expiry state
	NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS, // replicated server-authored stopped-stack Hero Action pass
	NEW_HORIZONS_BLOODRAGE, // battle-long per-side Bloodrage creature damage counter
	NEW_HORIZONS_SYLVAN_LUCK, // authoritative per-stack fortune history and round protection
	NEW_HORIZONS_SYLVAN_FORTUNE_EFFECTS, // activation-scoped fortune gifts and lucky recovery
	NEW_HORIZONS_PERFECT_MOMENT, // explicit one-strike declaration and saved expenditure
	NEW_HORIZONS_MYSTIC_POND_RESULTS, // authoritative weekly Mystic Pond resource results
	NEW_HORIZONS_ADVENTURE_MAGIC, // saved neutral adventure-spell roster usage state
	NEW_HORIZONS_CATAPULT_STRUCTURAL_DAMAGE, // absolute Siege-scaled Catapult packet damage
	NEW_HORIZONS_ASTROLOGY_PREVIEW, // server-authored next Astrology Week result
	NEW_HORIZONS_HOUSE_OF_WISDOM, // deterministic per-town New Horizons scroll storefront stock
	NEW_HORIZONS_CASTLE_GATE, // per-hero daily Castle Gate usage state
	NEW_HORIZONS_MUSTER, // per-hero and per-dwelling weekly Recruitment Muster state
	NEW_HORIZONS_MUSTER_PERKS, // per-hero weekly Muster use count for Recruitment perks
	NEW_HORIZONS_DEMONIC_RESERVE, // persistent owned Inferno troops outside the seven active army slots
	NEW_HORIZONS_CHAIN_GATE, // per-side Chain Gate kill token and accelerated pending Gates
	NEW_HORIZONS_CURE_AFFLICTION, // player-selected physical affliction identity carried by Cure battle actions
	NEW_HORIZONS_WARCASTING, // alternating hero-action readiness and consumed Order snapshot

	RELEASE_170 = HOTA_MAP_STACK_COUNT,
	RELEASE_174 = CUSTOM_GARRISON_TITLE,

	MINIMAL = RELEASE_170,
	CURRENT = NEW_HORIZONS_WARCASTING,
};

static_assert(ESerializationVersion::MINIMAL <= ESerializationVersion::CURRENT, "Invalid serialization version definition!");
static_assert(ESerializationVersion::CURRENT >= ESerializationVersion::NEW_HORIZONS_MASTERIES);
static_assert(ESerializationVersion::NEW_HORIZONS_CASTLE_GATE > ESerializationVersion::NEW_HORIZONS_HOUSE_OF_WISDOM);
static_assert(ESerializationVersion::NEW_HORIZONS_MUSTER > ESerializationVersion::NEW_HORIZONS_CASTLE_GATE);
static_assert(ESerializationVersion::NEW_HORIZONS_MUSTER_PERKS > ESerializationVersion::NEW_HORIZONS_MUSTER);
static_assert(ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE > ESerializationVersion::NEW_HORIZONS_MUSTER_PERKS);
static_assert(ESerializationVersion::NEW_HORIZONS_CHAIN_GATE > ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE);
static_assert(ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION > ESerializationVersion::NEW_HORIZONS_CHAIN_GATE);
static_assert(ESerializationVersion::NEW_HORIZONS_WARCASTING > ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION);
static_assert(ESerializationVersion::NEW_HORIZONS_MASTERIES > ESerializationVersion::NEW_HORIZONS_CAPABILITIES);
static_assert(ESerializationVersion::NEW_HORIZONS_CAPABILITIES > ESerializationVersion::NEW_HORIZONS_HERO_GROWTH);
static_assert(ESerializationVersion::NEW_HORIZONS_HERO_GROWTH > ESerializationVersion::NEW_HORIZONS_MAGIC);
static_assert(ESerializationVersion::NEW_HORIZONS_MAGIC > ESerializationVersion::HERO_COMMANDS);
static_assert(ESerializationVersion::HERO_COMMANDS > ESerializationVersion::TOWN_CUSTOM_INITIAL_GARRISON,
	"Append new serialization features before release aliases; never regress existing feature gates");
static_assert(ESerializationVersion::NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE > ESerializationVersion::NEW_HORIZONS_TARGETED_COMMANDS,
	"New spell action fields must remain absent from older targeted-command snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_PERKS > ESerializationVersion::NEW_HORIZONS_MAGIC_ARROW_OVERCHARGE,
	"New Horizons perk state must remain absent from older Magic Arrow snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_PERK_OFFERS > ESerializationVersion::NEW_HORIZONS_PERKS,
	"Combined perk offers must remain absent from older perk-state snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL > ESerializationVersion::NEW_HORIZONS_PERK_OFFERS,
	"New spell action fields must remain absent from older perk-offer snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD > ESerializationVersion::NEW_HORIZONS_SELECTIVE_DISPEL,
	"Temporal Field state must remain absent from older Selective Dispel snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_COUNTERSPELL > ESerializationVersion::NEW_HORIZONS_TEMPORAL_FIELD,
	"Counterspell state must remain absent from older Temporal Field snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_CANONICAL_ORDERS > ESerializationVersion::NEW_HORIZONS_COUNTERSPELL,
	"Canonical Order state must remain absent from older Counterspell snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_NECROMANCY > ESerializationVersion::NEW_HORIZONS_CANONICAL_ORDERS,
	"Necromancy state must remain absent from older canonical Order snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_LAND_MINE > ESerializationVersion::NEW_HORIZONS_NECROMANCY,
	"Land Mine action vectors must remain absent from older New Horizons snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_FIRE_WALL > ESerializationVersion::NEW_HORIZONS_LAND_MINE,
	"Fire Wall action metadata and trigger state must remain absent from older New Horizons snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_METAMAGIC > ESerializationVersion::NEW_HORIZONS_FIRE_WALL,
	"Metamagic state and cast metadata must remain absent from older Fire Wall snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_TIME_STOP > ESerializationVersion::NEW_HORIZONS_METAMAGIC,
	"Time Stop stasis state must remain absent from older Metamagic snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_TIME_STOP_ORIGINS > ESerializationVersion::NEW_HORIZONS_TIME_STOP,
	"Multiple Time Stop origin state must remain absent from older Time Stop snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS > ESerializationVersion::NEW_HORIZONS_TIME_STOP_ORIGINS,
	"Time Stop Hero Action pass metadata must remain absent from older Time Stop snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_BLOODRAGE > ESerializationVersion::NEW_HORIZONS_TIME_STOP_HERO_ACTION_PASS,
	"Bloodrage battle state must remain absent from older Time Stop snapshots");
