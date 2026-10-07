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
	NEW_HORIZONS_BATTLE_MEDITATION, // per-round Battle Meditation recovery state
	NEW_HORIZONS_HERO_ACTION_ALLOWANCES, // authoritative typed Hero/Spell/Order action grants
	NEW_HORIZONS_RANDOM_ARTIFACT_POOL, // saved generated-random artifact pool exclusions
	NEW_HORIZONS_SPELL_POINTS, // Normal/Buffer pools, typed mutations, battle snapshots and Buffer rewards
	NEW_HORIZONS_METAMAGIC_REWARDS, // once-combat Spell Buffer use and sequence-closure rewards
	NEW_HORIZONS_MAGE_GUILD_SLOTS, // actual fixed-school visible counts and assigned schools
	NEW_HORIZONS_MASTER_GATE, // per-side once-per-combat Master Gate activation continuation
	NEW_HORIZONS_PURSUIT, // saved movement-only continuation after a lethal melee attack
	NEW_HORIZONS_CLEAVE, // per-stack once-per-activation Cleave expenditure
	NEW_HORIZONS_RELENTLESS_ASSAULT, // per-side target streak and current activation snapshot
	NEW_HORIZONS_NO_QUARTER, // round retaliation block and next-activation morale penalty
	NEW_HORIZONS_SHIELD_MASTER, // exact Protect interception count, including Shield Master's second use
	NEW_HORIZONS_IRON_DISCIPLINE, // saved Hold the Line magical reduction snapshot
	NEW_HORIZONS_ARCHERY_SKIRMISHER, // explicit player/AI-selected move-and-shoot action metadata
	NEW_HORIZONS_ADVENTURE_SPELL_UNLOCKS, // persistent per-town Adventure Spell Guild unlocks
	NEW_HORIZONS_SIEGE_RATING, // timed Stronghold Ballista Yard Siege rating bonus
	NEW_HORIZONS_QUICKSAND, // player-selected canonical Quicksand action vectors
	NEW_HORIZONS_SHADOW_GIFT, // player-selected Shadow Gift sacrifice tier and battle health cap
	NEW_HORIZONS_PURIFY, // player-selected negative effect source groups carried by Purify actions
	NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION, // timed fractional Magical Damage Reduction bonus type
	BATTLE_COMPLETED_HERO_SPELL, // per-side battle-long completion gate for the first hero spell discount
	BATTLE_COMPLETED_HERO_SPELL_LEVELS, // per-side accepted hero spell levels completed during battle
	NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS, // timed final Luck ceiling and favorable creature proc multiplier
	NEW_HORIZONS_SHIELD_OF_CHAOS_PHYSICAL_REDUCTION, // timed fractional physical damage reduction bonus type
	BATTLE_UNIT_FORM_STATE, // battle-local creature forms and original-species HP provenance
	BATTLE_HERO_MANA_EXPENDITURE, // accepted hero spell and Counterspell costs spent during combat
	NEW_HORIZONS_LEARNING_MENTOR, // replicated weekly Mentor meeting usage
	NEW_HORIZONS_ARMORER_VETERAN, // replicated physical damage interval for Veteran recovery
	NEW_HORIZONS_SECOND_CHANCE, // battle-long negative Luck suppression
	NEW_HORIZONS_GAMBLER, // round-long first attack Luck window
	NEW_HORIZONS_CHAIN_OF_FORTUNE, // pending different-stack Luck gift and round trigger limit
	NEW_HORIZONS_ADVERSE_COMBAT_REROLL, // side-owned once-per-battle adverse stochastic reroll
	NEW_HORIZONS_RALLY, // side-owned once-per-battle cancellation of a negative Morale trigger
	NEW_HORIZONS_RESERVE, // per-stack activation-scoped movement bonus after Waiting
	NEW_HORIZONS_CREATURE_ACTIVATION_DURATION, // generic bonus expiry when a creature's next activation begins
	NEW_HORIZONS_PHYSICAL_AFFLICTIONS, // effect-neutral physical-affliction identity and application order markers
	NEW_HORIZONS_RANGED_FOLLOW_UP, // saved same-activation selectable ranged shot continuation
	NEW_HORIZONS_REDUCED_EXTRA_ACTIVATION, // saved side allowance and reduced-output genuine activation
	NEW_HORIZONS_LAND_SURVEYOR, // per-hero weekly successful mine-capture allowance
	NEW_HORIZONS_OBSTACLE_MOVEMENT_COST, // spell-created terrain surcharge per newly entered hex
	NEW_HORIZONS_BROAD_MUSTER, // optional second destination and first-row amount in Recruitment Muster requests
	NEW_HORIZONS_UNBREAKABLE, // independent per-round negative Morale suppression
	BONUS_EFFECT_HOSTILITY, // target-relative enemy-applied bonus provenance
	NEW_HORIZONS_BLOODRAGE_CAP, // resolved per-side Bloodrage maximum including cap perks
	NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES, // resolved per-side Speed and retaliation threshold benefits
	NEW_HORIZONS_BLOOD_SCENT, // resolved attack-local low-health Bloodrage increment
	NEW_HORIZONS_MULTIPLE_ORDERS, // independent per-command Order snapshots and preservation-aware packets
	NEW_HORIZONS_DOUBLE_COMMAND, // immediate contextual Order continuation and combat usage
	NEW_HORIZONS_BATTLE_PLAN, // typed pre-combat Order opportunity before the first activation
	BATTLE_DEPLOYMENT_PHASES, // independent per-army deployment opportunities and phase progression
	BATTLE_FINAL_RELOCATION, // final one-move deployment opportunities after initial deployment
	NEW_HORIZONS_INVESTOR_INCOME, // saved weekly per-hero Investor daily-income snapshots
	BATTLE_INITIAL_ARMY_VALUE, // captured raw starting army values and wandering-army classification
	NEW_HORIZONS_RAGE_THROUGH_PAIN, // personal Bloodrage increments and battle perk snapshots
	NEW_HORIZONS_MASTER_SYNTHESIS, // battle-long first Warcasting bonus consumption history
	NEW_HORIZONS_PORTAL_SOURCE, // linked external dwelling and weekly Portal choice, explicit recruitment context
	BATTLE_INITIAL_DEPLOYMENT_ORDER, // resolved first side for the ordinary initial deployment stage
	BATTLE_CASUALTY_PROVENANCE, // ordered usable casualty causes and temporary restoration identity in state updates
	NEW_HORIZONS_NECROMANCY_WIGHTS, // explicit Soul Harvester Wight output in post-battle summaries
	NEW_HORIZONS_NECROMANCY_SKELETON_FORM, // explicit upgraded Skeleton output in mixed Necromancy summaries
	NEW_HORIZONS_NECROMANCY_SPECIAL_CASUALTIES, // captured special corpse pools and weighted conversion summaries
	NEW_HORIZONS_NECROMANCY_OSSUARY, // authoritative town destination for redirected raised armies
	NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD, // pre-conversion Bone Dragon result and defeated Champion snapshot
	NEW_HORIZONS_DIPLOMACY_ELIGIBILITY, // explicit map-authored neutral-join eligibility
	NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE, // per-hero weekly Peacemaker and Tribute usage/protection state
	NEW_HORIZONS_RECRUITMENT_PACT_STATE, // per-hero active Recruitment Pact expiry day
	BONUS_SPELL_CASTER_OWNER, // stable spell-caster owner provenance for applied bonuses
	NEW_HORIZONS_PUPPET_MASTER_CONTROL, // one-activation action controller and fixed Lucidity markers
	BONUS_STATUS_TAGS, // explicit status classification and effect identity metadata
	NEW_HORIZONS_FORCED_MARCH, // daily exhaustion allowance and next-combat first-round Morale snapshot
	NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION, // typed timed capability restriction; legacy Forgetfulness remains unchanged
	NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE, // percentage-of-next-level rewardable Experience
	NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE, // explicit element-subtyped final magical damage bonus
	NEW_HORIZONS_DIVINE_MANDATE, // typed, round-limited reciprocal Hero Action allowance and pair count
	NEW_HORIZONS_ELEMENTAL_REBIRTH, // frozen battle-start maximum aggregate HP basis for Rebirth
	NEW_HORIZONS_SACRED_COMMAND, // captured Sacred Command efficiency on issued Orders
	NEW_HORIZONS_KNIGHTLY_SEQUENCE, // captured Knightly Sequence efficiency on Orders
	NEW_HORIZONS_MANDATE_OF_HEAVEN, // Divine Mandate supports the Expert-perk fourth sequence
	NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE, // per-stack first-melee reaction round marker
	NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP, // immutable initial aggregate HP on Elemental Rebirth output stacks
	NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW, // immediate authoritative forecast on Astronomy Tower construction
	NEW_HORIZONS_HERO_ACTION_SEQUENCE, // fixed recent ordinary Spell/Order action history for battle state
	NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE, // element-subtyped incoming spell damage modifier
	NEW_HORIZONS_SPELL_RESPONSE, // round-bounded response readiness from an accepted enemy hero spell
	NEW_HORIZONS_BATTLEFIELD_MASTERY, // per-round first eligible Wait/Defend award and per-stack provenance
	NEW_HORIZONS_ARMORER_LAST_STAND, // side-wide Last Stand use and activation-ending lethal retaliation state
	NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD, // explicit rewardable primary-XP classification
	COMPONENT_HELP_REASON, // optional localized reason appended to component help

	RELEASE_170 = HOTA_MAP_STACK_COUNT,
	RELEASE_174 = CUSTOM_GARRISON_TITLE,

	MINIMAL = RELEASE_170,
	CURRENT = COMPONENT_HELP_REASON,
};

static_assert(ESerializationVersion::COMPONENT_HELP_REASON > ESerializationVersion::NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD,
	"Component help reasons must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_PRIMARY_EXPERIENCE_REWARD > ESerializationVersion::NEW_HORIZONS_ARMORER_LAST_STAND,
	"Primary Experience reward classification must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ARMORER_LAST_STAND > ESerializationVersion::NEW_HORIZONS_BATTLEFIELD_MASTERY,
	"Armorer Last Stand state must remain append-only");

static_assert(ESerializationVersion::NEW_HORIZONS_BATTLEFIELD_MASTERY > ESerializationVersion::NEW_HORIZONS_SPELL_RESPONSE,
	"Battlefield Mastery state must remain append-only");

static_assert(ESerializationVersion::NEW_HORIZONS_SPELL_RESPONSE > ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE,
	"Spell Response readiness must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_INCOMING_ELEMENTAL_SPELL_DAMAGE > ESerializationVersion::NEW_HORIZONS_HERO_ACTION_SEQUENCE,
	"Incoming elemental spell damage must remain append-only");

static_assert(ESerializationVersion::MINIMAL <= ESerializationVersion::CURRENT, "Invalid serialization version definition!");
static_assert(ESerializationVersion::NEW_HORIZONS_HERO_ACTION_SEQUENCE > ESerializationVersion::NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW,
	"Hero Action sequence state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ASTROLOGY_CONSTRUCTION_PREVIEW > ESerializationVersion::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP,
	"Construction-time Astrology preview must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE > ESerializationVersion::NEW_HORIZONS_MANDATE_OF_HEAVEN,
	"Battlecraft Pre-emptive Strike state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP > ESerializationVersion::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE,
	"Rebirth output original HP state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE > ESerializationVersion::NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE,
	"Elemental spell damage must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_DIVINE_MANDATE > ESerializationVersion::NEW_HORIZONS_ELEMENTAL_SPELL_DAMAGE,
	"Divine Mandate combat state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ELEMENTAL_REBIRTH > ESerializationVersion::NEW_HORIZONS_DIVINE_MANDATE,
	"Elemental Rebirth battle HP basis must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_SACRED_COMMAND > ESerializationVersion::NEW_HORIZONS_ELEMENTAL_REBIRTH,
	"Sacred Command Order snapshots must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_KNIGHTLY_SEQUENCE > ESerializationVersion::NEW_HORIZONS_SACRED_COMMAND,
	"Knightly Sequence Order snapshots must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_MANDATE_OF_HEAVEN > ESerializationVersion::NEW_HORIZONS_KNIGHTLY_SEQUENCE,
	"Mandate of Heaven extended pair counts must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_REWARDABLE_NEXT_LEVEL_EXPERIENCE > ESerializationVersion::NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION,
	"Rewardable next-level Experience must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_CREATURE_ABILITY_SUPPRESSION > ESerializationVersion::NEW_HORIZONS_FORCED_MARCH,
	"Creature ability suppression must remain absent from older snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_FORCED_MARCH > ESerializationVersion::BONUS_STATUS_TAGS,
	"Forced March state must remain append-only");
static_assert(ESerializationVersion::BONUS_STATUS_TAGS > ESerializationVersion::NEW_HORIZONS_PUPPET_MASTER_CONTROL,
	"Bonus status metadata must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_PUPPET_MASTER_CONTROL > ESerializationVersion::BONUS_SPELL_CASTER_OWNER,
	"Puppet Master control markers must remain append-only");
static_assert(ESerializationVersion::BATTLE_CASUALTY_PROVENANCE > ESerializationVersion::BATTLE_INITIAL_DEPLOYMENT_ORDER,
	"Casualty provenance must remain append-only");
static_assert(ESerializationVersion::BATTLE_INITIAL_DEPLOYMENT_ORDER > ESerializationVersion::NEW_HORIZONS_PORTAL_SOURCE,
	"Initial deployment ordering must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_PORTAL_SOURCE > ESerializationVersion::NEW_HORIZONS_MASTER_SYNTHESIS,
	"Portal source state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_MASTER_SYNTHESIS > ESerializationVersion::NEW_HORIZONS_RAGE_THROUGH_PAIN,
	"Warcasting consumption history must remain append-only");
static_assert(ESerializationVersion::BATTLE_INITIAL_ARMY_VALUE > ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME,
	"Initial army snapshots must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_RAGE_THROUGH_PAIN > ESerializationVersion::BATTLE_INITIAL_ARMY_VALUE,
	"Rage Through Pain state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_INVESTOR_INCOME > ESerializationVersion::BATTLE_FINAL_RELOCATION,
	"Investor income snapshots must remain append-only");
static_assert(ESerializationVersion::BATTLE_FINAL_RELOCATION > ESerializationVersion::BATTLE_DEPLOYMENT_PHASES,
	"Final relocation state must remain append-only");
static_assert(ESerializationVersion::BATTLE_DEPLOYMENT_PHASES > ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN,
	"Independent deployment state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BATTLE_PLAN > ESerializationVersion::NEW_HORIZONS_DOUBLE_COMMAND,
	"Pre-combat Order state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_DOUBLE_COMMAND > ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS,
	"Immediate Order continuation state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS > ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT,
	"Multiple Order snapshots must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT > ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES,
	"Blood Scent snapshots must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BLOODRAGE_THRESHOLD_BONUSES > ESerializationVersion::NEW_HORIZONS_BLOODRAGE_CAP,
	"Bloodrage threshold benefits must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BLOODRAGE_CAP > ESerializationVersion::BONUS_EFFECT_HOSTILITY,
	"Bloodrage cap snapshots must remain append-only");
static_assert(ESerializationVersion::BONUS_EFFECT_HOSTILITY > ESerializationVersion::NEW_HORIZONS_UNBREAKABLE,
	"Bonus effect hostility must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_OBSTACLE_MOVEMENT_COST > ESerializationVersion::NEW_HORIZONS_LAND_SURVEYOR,
	"Obstacle movement cost must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BROAD_MUSTER > ESerializationVersion::NEW_HORIZONS_OBSTACLE_MOVEMENT_COST,
	"Broad Muster request parameters must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_UNBREAKABLE > ESerializationVersion::NEW_HORIZONS_BROAD_MUSTER,
	"Unbreakable state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE > ESerializationVersion::NEW_HORIZONS_GAMBLER,
	"Chain of Fortune state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ADVERSE_COMBAT_REROLL > ESerializationVersion::NEW_HORIZONS_CHAIN_OF_FORTUNE,
	"Adverse combat reroll state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_RALLY > ESerializationVersion::NEW_HORIZONS_ADVERSE_COMBAT_REROLL,
	"Rally Morale suppression state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_GAMBLER > ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE,
	"Gambler expenditure must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_SECOND_CHANCE > ESerializationVersion::NEW_HORIZONS_ARMORER_VETERAN,
	"Second Chance expenditure must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_ARMORER_VETERAN > ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR,
	"Veteran damage history must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_LEARNING_MENTOR > ESerializationVersion::BATTLE_HERO_MANA_EXPENDITURE,
	"Mentor weekly state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD > ESerializationVersion::NEW_HORIZONS_NECROMANCY_OSSUARY,
	"Lord of the Dead results must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY > ESerializationVersion::NEW_HORIZONS_NECROMANCY_LORD_OF_DEAD,
	"Diplomacy eligibility must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE
	> ESerializationVersion::NEW_HORIZONS_DIPLOMACY_ELIGIBILITY,
	"Diplomacy weekly state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_RECRUITMENT_PACT_STATE
	> ESerializationVersion::NEW_HORIZONS_DIPLOMACY_WEEKLY_STATE,
	"Recruitment Pact state must remain append-only");
static_assert(ESerializationVersion::CURRENT >= ESerializationVersion::NEW_HORIZONS_MASTERIES);
static_assert(ESerializationVersion::NEW_HORIZONS_CASTLE_GATE > ESerializationVersion::NEW_HORIZONS_HOUSE_OF_WISDOM);
static_assert(ESerializationVersion::NEW_HORIZONS_MUSTER > ESerializationVersion::NEW_HORIZONS_CASTLE_GATE);
static_assert(ESerializationVersion::NEW_HORIZONS_MUSTER_PERKS > ESerializationVersion::NEW_HORIZONS_MUSTER);
static_assert(ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE > ESerializationVersion::NEW_HORIZONS_MUSTER_PERKS);
static_assert(ESerializationVersion::NEW_HORIZONS_CHAIN_GATE > ESerializationVersion::NEW_HORIZONS_DEMONIC_RESERVE);
static_assert(ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION > ESerializationVersion::NEW_HORIZONS_CHAIN_GATE);
static_assert(ESerializationVersion::NEW_HORIZONS_WARCASTING > ESerializationVersion::NEW_HORIZONS_CURE_AFFLICTION);
static_assert(ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL > ESerializationVersion::NEW_HORIZONS_HERO_ACTION_ALLOWANCES);
static_assert(ESerializationVersion::NEW_HORIZONS_SPELL_POINTS > ESerializationVersion::NEW_HORIZONS_RANDOM_ARTIFACT_POOL);
static_assert(ESerializationVersion::NEW_HORIZONS_METAMAGIC_REWARDS > ESerializationVersion::NEW_HORIZONS_SPELL_POINTS);
static_assert(ESerializationVersion::NEW_HORIZONS_MAGE_GUILD_SLOTS > ESerializationVersion::NEW_HORIZONS_METAMAGIC_REWARDS);
static_assert(ESerializationVersion::NEW_HORIZONS_MASTER_GATE > ESerializationVersion::NEW_HORIZONS_MAGE_GUILD_SLOTS);
static_assert(ESerializationVersion::NEW_HORIZONS_PURSUIT > ESerializationVersion::NEW_HORIZONS_MASTER_GATE);
static_assert(ESerializationVersion::NEW_HORIZONS_CLEAVE > ESerializationVersion::NEW_HORIZONS_PURSUIT);
static_assert(ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT > ESerializationVersion::NEW_HORIZONS_CLEAVE);
static_assert(ESerializationVersion::NEW_HORIZONS_NO_QUARTER > ESerializationVersion::NEW_HORIZONS_RELENTLESS_ASSAULT);
static_assert(ESerializationVersion::NEW_HORIZONS_SHIELD_MASTER > ESerializationVersion::NEW_HORIZONS_NO_QUARTER);
static_assert(ESerializationVersion::NEW_HORIZONS_IRON_DISCIPLINE > ESerializationVersion::NEW_HORIZONS_SHIELD_MASTER);
static_assert(ESerializationVersion::NEW_HORIZONS_ARCHERY_SKIRMISHER > ESerializationVersion::NEW_HORIZONS_IRON_DISCIPLINE);
static_assert(ESerializationVersion::NEW_HORIZONS_ADVENTURE_SPELL_UNLOCKS > ESerializationVersion::NEW_HORIZONS_ARCHERY_SKIRMISHER);
static_assert(ESerializationVersion::NEW_HORIZONS_SIEGE_RATING > ESerializationVersion::NEW_HORIZONS_ADVENTURE_SPELL_UNLOCKS);
static_assert(ESerializationVersion::NEW_HORIZONS_QUICKSAND > ESerializationVersion::NEW_HORIZONS_SIEGE_RATING,
	"New Horizons Quicksand action vectors must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_SHADOW_GIFT > ESerializationVersion::NEW_HORIZONS_QUICKSAND,
	"New Horizons Shadow Gift state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_PURIFY > ESerializationVersion::NEW_HORIZONS_SHADOW_GIFT,
	"New Horizons Purify action choices must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION > ESerializationVersion::NEW_HORIZONS_PURIFY,
	"New Horizons Crusade magical reduction must remain append-only");
static_assert(ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL > ESerializationVersion::NEW_HORIZONS_CRUSADE_MAGIC_REDUCTION,
	"Completed hero spell state must remain append-only");
static_assert(ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL_LEVELS > ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL,
	"Completed hero spell level state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS > ESerializationVersion::BATTLE_COMPLETED_HERO_SPELL_LEVELS,
	"Creature probability modifier state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_SHIELD_OF_CHAOS_PHYSICAL_REDUCTION > ESerializationVersion::NEW_HORIZONS_CREATURE_PROBABILITY_MODIFIERS,
	"Shield of Chaos physical reduction state must remain append-only");
static_assert(ESerializationVersion::BATTLE_UNIT_FORM_STATE > ESerializationVersion::NEW_HORIZONS_SHIELD_OF_CHAOS_PHYSICAL_REDUCTION,
	"Battle form state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_BATTLE_MEDITATION > ESerializationVersion::NEW_HORIZONS_WARCASTING);
static_assert(ESerializationVersion::NEW_HORIZONS_HERO_ACTION_ALLOWANCES > ESerializationVersion::NEW_HORIZONS_BATTLE_MEDITATION);
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
static_assert(ESerializationVersion::NEW_HORIZONS_RESERVE > ESerializationVersion::NEW_HORIZONS_RALLY,
	"Reserve movement state must remain absent from older New Horizons snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_CREATURE_ACTIVATION_DURATION > ESerializationVersion::NEW_HORIZONS_RESERVE,
	"Creature activation bonus durations must remain absent from older New Horizons snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_PHYSICAL_AFFLICTIONS > ESerializationVersion::NEW_HORIZONS_CREATURE_ACTIVATION_DURATION,
	"Physical-affliction markers must remain absent from older New Horizons snapshots");
static_assert(ESerializationVersion::NEW_HORIZONS_RANGED_FOLLOW_UP > ESerializationVersion::NEW_HORIZONS_PHYSICAL_AFFLICTIONS,
	"Ranged follow-up state must remain append-only");
static_assert(ESerializationVersion::NEW_HORIZONS_REDUCED_EXTRA_ACTIVATION > ESerializationVersion::NEW_HORIZONS_RANGED_FOLLOW_UP,
	"Reduced extra activation state must remain append-only");
