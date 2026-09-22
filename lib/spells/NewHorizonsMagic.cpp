/*
 * NewHorizonsMagic.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMagic.h"

#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "CSpellHandler.h"
#include "../constants/StringConstants.h"
#include "../GameLibrary.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include "../callback/IGameInfoCallback.h"
#include "../bonuses/BonusEnum.h"
#include "../battle/IBattleState.h"
#include "../battle/CBattleInfoCallback.h"
#include <cmath>

namespace
{
const JsonNode & battleMagicRules(const CBattleInfoCallback & callback)
{
	static const JsonNode legacy;
	return callback.getBattle() ? callback.getBattle()->getMagicRules() : legacy;
}
}

const JsonNode & IGameInfoCallback::getMagicRules() const
{
	static const JsonNode legacy;
	return legacy;
}

const JsonNode & IBattleInfo::getMagicRules() const
{
	static const JsonNode legacy;
	return legacy;
}

std::vector<SpellSchool> IGameInfoCallback::getActiveSpellSchools() const
{
	return newHorizonsMagic::activeSchools(getMagicRules());
}

std::vector<SpellSchool> IGameInfoCallback::getSpellSchools(SpellID spell) const
{
	return newHorizonsMagic::spellSchools(getMagicRules(), spell);
}

int IGameInfoCallback::getSpellLevel(SpellID spell) const
{
	return newHorizonsMagic::spellLevel(getMagicRules(), spell);
}

std::vector<SpellSchool> CBattleInfoCallback::battleGetActiveSpellSchools() const
{
	return newHorizonsMagic::activeSchools(battleMagicRules(*this));
}

std::vector<SpellSchool> CBattleInfoCallback::battleGetSpellSchools(SpellID spell) const
{
	return newHorizonsMagic::spellSchools(battleMagicRules(*this), spell);
}

int CBattleInfoCallback::battleGetSpellLevel(SpellID spell) const
{
	return newHorizonsMagic::spellLevel(battleMagicRules(*this), spell);
}

namespace newHorizonsMagic
{
namespace
{
void require(bool condition, const std::string & message)
{
	if(!condition)
		throw std::runtime_error("Unsupported New Horizons magic rules: " + message);
}

bool legacy(const JsonNode & rules)
{
	return rules.isNull() || (rules.isStruct() && rules.Struct().empty());
}

void fields(const JsonNode & node, std::initializer_list<std::string> allowed)
{
	require(node.isStruct(), "expected object");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

bool integer(const JsonNode & node, int minimum, int maximum)
{
	return node.isNumber() && std::isfinite(node.Float()) && node.Float() >= minimum
		&& node.Float() <= maximum && std::floor(node.Float()) == node.Float();
}

int resolve(const std::string & type, const std::string & name)
{
	require(name.find(':') != std::string::npos, "unscoped " + type + " " + name);
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), type, name);
	require(id.has_value(), "missing " + type + " " + name);
	return *id;
}

const JsonNode & entry(const JsonNode & rules, SpellID spell)
{
	return rules["spells"][spell.toSpell()->getJsonKey()];
}

const JsonNode & adventureEntry(const JsonNode & rules, SpellID spell)
{
	return rules["adventureSpells"][spell.toSpell()->getJsonKey()];
}

struct AdventureSpellDefinition
{
	std::string_view identity;
	int guildLevel;
	int cost;
};

constexpr std::array ADVENTURE_SPELLS = {
	AdventureSpellDefinition{"core:summonBoat", 1, 20},
	AdventureSpellDefinition{"core:waterWalk", 2, 30},
	AdventureSpellDefinition{"core:townPortal", 3, 50},
	AdventureSpellDefinition{"core:fly", 4, 60},
	AdventureSpellDefinition{"core:dimensionDoor", 5, 80},
};

bool sorceryMember(const SpellSchool school)
{
	return school.serializationKey() == "new-horizons:sorcery";
}
}

bool rulesActive(const JsonNode & rules)
{
	return !legacy(rules) && rules.isStruct()
		&& integer(rules["rulesetVersion"], RULESET_VERSION, DIRECT_DAMAGE_RULESET_VERSION);
}

int masterChainLightningRetentionPercent(int heroLevel)
{
	return std::min(90, 75 + std::max(0, heroLevel));
}

std::string spellDescriptionForHero(const CGHeroInstance * hero, const spells::Spell * spell, int schoolLevel)
{
	if(!spell)
		return {};

	const auto description = [&spell, schoolLevel]()
	{
		return spell->getDescriptionTranslated(schoolLevel);
	};

	if(!hero || !rulesActive(hero->getMagicRules())
		|| spell->getJsonKey() != "new-horizons:masterChainLightning")
		return description();

	// Keep the translated base description complete for hero-independent help
	// surfaces, then add the live value only where a hero context exists.
	return description() + "\n\nCurrent retention: "
		+ std::to_string(masterChainLightningRetentionPercent(hero->level)) + "%.";
}

void validateRules(const JsonNode & rules)
{
	if(legacy(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "schools", "adventureSpells", "spells", "factions", "factionWeights", "schoolSkills", "skillReplacements"});
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], RULESET_VERSION, DIRECT_DAMAGE_RULESET_VERSION), "rulesetVersion");
	const int version = rules["rulesetVersion"].Integer();
	if(version == DIRECT_DAMAGE_RULESET_VERSION)
	{
		require(rules["schemaVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer schemaVersion");
		require(rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer rulesetVersion");
	}
	require(rules["schools"].isVector() && rules["schools"].Vector().size() == 6, "six active schools required");
	std::set<std::string> schools;
	std::set<int> schoolIDs;
	for(const auto & school : rules["schools"].Vector())
	{
		require(school.isString(), "school identifier");
		const auto id = resolve("spellSchool", school.String());
		require(id >= 0 && schoolIDs.insert(id).second && schools.insert(school.String()).second, "duplicate/invalid school");
	}
	require(rules["schoolSkills"].isStruct() && rules["schoolSkills"].Struct().size() == schools.size(), "six school skills required");
	std::set<int> skillIDs;
	for(const auto & [school, skill] : rules["schoolSkills"].Struct())
	{
		require(schools.count(school) && skill.isString(), "school skill mapping");
		const auto id = resolve(SecondarySkill::entityType(), skill.String());
		require(id >= 0 && skillIDs.insert(id).second, "distinct school skills required");
	}
	if(!rules["skillReplacements"].isNull())
	{
		require(rules["skillReplacements"].isStruct(), "skill replacements object");
		for(const auto & [oldSkill, newSkill] : rules["skillReplacements"].Struct())
		{
			require(newSkill.isString(), "skill replacement identifier");
			const auto oldID = resolve(SecondarySkill::entityType(), oldSkill);
			const auto newID = resolve(SecondarySkill::entityType(), newSkill.String());
			require(oldID >= 0 && skillIDs.count(newID) && !skillIDs.count(oldID), "replace legacy skills with school skills only");
		}
	}
	std::set<int> adventureMapped;
	if(!rules["adventureSpells"].isNull())
	{
		require(rules["adventureSpells"].isStruct() && rules["adventureSpells"].Struct().size() == ADVENTURE_SPELLS.size(), "five adventure spells required");
		for(const auto & expected : ADVENTURE_SPELLS)
		{
			const auto found = rules["adventureSpells"].Struct().find(std::string(expected.identity));
			require(found != rules["adventureSpells"].Struct().end(), "missing adventure spell " + std::string(expected.identity));
			fields(found->second, {"guildLevel", "cost", "unlockCost"});
			require(integer(found->second["guildLevel"], 1, 5) && found->second["guildLevel"].Integer() == expected.guildLevel, "adventure guild level");
			require(integer(found->second["cost"], 0, 1000000) && found->second["cost"].Integer() == expected.cost, "adventure spell cost");
			if(!found->second["unlockCost"].isNull())
			{
				const auto & unlockCost = found->second["unlockCost"];
				fields(unlockCost, {"gold", "mercury", "sulfur", "crystal", "gems"});
				for(const auto * resource : {"gold", "mercury", "sulfur", "crystal", "gems"})
					require(integer(unlockCost[resource], 0, 1000000), "adventure spell unlock cost");
			}
			const auto id = resolve("spell", std::string(expected.identity));
			require(id >= 0 && adventureMapped.insert(id).second, "duplicate/invalid adventure spell");
			const auto * definition = SpellID(id).toSpell();
			require(definition && definition->getJsonKey() == expected.identity && definition->isCommonHeroSpell() && definition->isAdventure(), "adventure spell identity/type");
		}
		for(const auto & [name, data] : rules["adventureSpells"].Struct())
		{
			(void)data;
			require(std::any_of(ADVENTURE_SPELLS.begin(), ADVENTURE_SPELLS.end(), [&](const auto & expected) { return name == expected.identity; }), "unknown adventure spell");
		}
	}
	require(rules["spells"].isStruct(), "spell mappings required");
	std::set<int> mapped;
	for(const auto & [name, data] : rules["spells"].Struct())
	{
		if(version == RULESET_VERSION)
			fields(data, {"schools", "level", "costs"});
		else
			fields(data, {"schools", "level", "costs", "directDamage", "active"});
		if(version == DIRECT_DAMAGE_RULESET_VERSION && data.Struct().contains("active"))
			require(data["active"].isBool(), "spell active flag");
		// Strict field/type/bounds checks, including rejection of present-null.
		(void)directDamageFormula(data, version);
		const auto id = resolve("spell", name);
		require(id >= 0 && mapped.insert(id).second, "duplicate/invalid spell");
		require(adventureMapped.count(id) == 0, "adventure spell cannot be an ordinary school spell");
		require(static_cast<size_t>(id) < LIBRARY->spellh->objects.size() && LIBRARY->spellh->objects.at(id), "missing spell definition");
		const auto * definition = SpellID(id).toSpell();
		require(definition->getJsonKey() == name, "canonical spell identity required");
		require(definition->isCommonHeroSpell(), "ability cannot be reclassified as hero spell");
		if(name.starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':'))
			require(version == DIRECT_DAMAGE_RULESET_VERSION, "NH common spells require ruleset version 2");
		if(version == DIRECT_DAMAGE_RULESET_VERSION && name == "core:magicArrow")
			require(directDamageFormula(data, version).has_value(), "Magic Arrow requires saved directDamage in ruleset version 2");
		require(data["schools"].isVector() && !data["schools"].Vector().empty(), "spell school list");
		std::set<std::string> membership;
		for(const auto & school : data["schools"].Vector())
		{
			require(school.isString() && schools.count(school.String()) != 0, "inactive spell school");
			require(membership.insert(school.String()).second, "duplicate spell membership");
		}
		if(!data["level"].isNull())
			require(integer(data["level"], 1, 5), "spell level");
		if(!data["costs"].isNull())
		{
			require(data["costs"].isVector() && data["costs"].Vector().size() == 4, "four mastery costs required");
			for(const auto & cost : data["costs"].Vector())
				require(integer(cost, 0, 1000000), "spell cost");
		}
	}
	for(const auto & spell : LIBRARY->spellh->objects)
		if(spell && spell->isCommonHeroSpell()
			&& !spell->getJsonKey().starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':')
			&& adventureMapped.count(spell->getId().getNum()) == 0)
			require(mapped.count(spell->getId().getNum()) != 0, "unclassified hero spell " + spell->getJsonKey());
	if(!rules["factionWeights"].isNull())
	{
		fields(rules["factionWeights"], {"major", "minor"});
		require(integer(rules["factionWeights"]["major"], 1, 1000)
			&& integer(rules["factionWeights"]["minor"], 1, 1000), "positive faction weights");
	}
	if(!rules["factions"].isNull())
	{
		require(rules["factions"].isStruct(), "factions object");
		for(const auto & [name, data] : rules["factions"].Struct())
		{
			resolve("faction", name);
			fields(data, {"major", "minor", "provisional"});
			require(data["major"].isString() && data["minor"].isString(), "faction school identifiers");
			require(schools.count(data["major"].String()) && schools.count(data["minor"].String()), "inactive faction school");
			require(data["major"].String() != data["minor"].String(), "distinct major/minor required");
			require(data["provisional"].isNull() || data["provisional"].isBool(), "provisional flag");
		}
	}
}

std::optional<DirectDamageFormula> spellDirectDamage(const JsonNode & rules, const std::string & scopedIdentity)
{
	if(legacy(rules))
		return std::nullopt;
	const auto separator = scopedIdentity.find(':');
	require(separator != std::string::npos && separator > 0 && separator + 1 < scopedIdentity.size()
		&& scopedIdentity.find(':', separator + 1) == std::string::npos, "canonical scoped spell identity required");
	require(rules.isStruct() && rules["spells"].isStruct(), "saved spell roster required");
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], RULESET_VERSION, DIRECT_DAMAGE_RULESET_VERSION), "rulesetVersion");
	const int version = rules["rulesetVersion"].Integer();
	if(version == DIRECT_DAMAGE_RULESET_VERSION)
	{
		require(rules["schemaVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer schemaVersion");
		require(rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer rulesetVersion");
	}
	const auto found = rules["spells"].Struct().find(scopedIdentity);
	if(found == rules["spells"].Struct().end())
		return std::nullopt;
	return directDamageFormula(found->second, version);
}

std::optional<int64_t> directDamageValue(const JsonNode & rules, const std::string & scopedIdentity, int32_t effectPower, int32_t divisor)
{
	const auto formula = spellDirectDamage(rules, scopedIdentity);
	if(!formula)
		return std::nullopt;
	return formula->evaluate(effectPower, divisor);
}

bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell)
{
	if(spell != SpellID(SpellID::MAGIC_ARROW) || legacy(rules))
		return false;
	if(!rules.isStruct() || !integer(rules["rulesetVersion"], DIRECT_DAMAGE_RULESET_VERSION, DIRECT_DAMAGE_RULESET_VERSION))
		return false;

	// Read the saved roster, rather than installed content.  This keeps old
	// v1 saves on their old Magic Arrow semantics even when a newer module is
	// installed.  The explicit v2 formula is the compatibility marker for the
	// Sorcery overcharge contract; a v2 roster without it is not activated.
	if(!spellAllowedBySavedRoster(rules, spell))
		return false;
	if(!spellDirectDamage(rules, spell.toSpell()->getJsonKey()))
		return false;

	return vstd::contains_if(spellSchools(rules, spell), sorceryMember);
}

MagicArrowOverchargeModifiers magicArrowOverchargeModifiers(const CGHeroInstance * hero)
{
	MagicArrowOverchargeModifiers result;
	if(hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.overcharger"))
	{
		result.maximumBonus = 1;
		result.damagePercentTenths = 175;
	}
	return result;
}

int spellDurationBonus(const CGHeroInstance * hero, SpellID spell)
{
	if(spell == SpellID(SpellID::SLOW) && hero
		&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalist"))
		return 1;
	return 0;
}

int magicArrowMaxOvercharge(const JsonNode & rules, SpellID spell, int32_t spellPower,
	MagicArrowOverchargeModifiers modifiers)
{
	if(!magicArrowOverchargeEnabled(rules, spell))
		return 0;
	if(spellPower < 0)
		throw std::runtime_error("Magic Arrow spell power cannot be negative");

	// Base maximum = min(5, 2 + floor(SP / 50)); active saved perks may
	// extend it. Spell Power is the primary rating; the divisor is applied only
	// to fixed-point coefficients when the damage value is evaluated.
	return std::min(5, 2 + spellPower / 50) + modifiers.maximumBonus;
}

std::optional<int64_t> magicArrowDamage(const JsonNode & rules, SpellID spell,
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers)
{
	if(!magicArrowOverchargeEnabled(rules, spell))
		return std::nullopt;
	if(spellPower < 0 || divisor <= 0)
		return std::nullopt;

	const int maxOvercharge = magicArrowMaxOvercharge(rules, spell, spellPower, modifiers);
	if(overcharge < 0 || overcharge > maxOvercharge)
		return std::nullopt;

	// Base Damage = 20 + 2 * SP.  The v2 saved directDamage row is explicit and
	// authoritative (including a saved zero).  Both the authored coefficients
	// and the fallback below use the same fixed-point scale, so hero rating
	// divisors remain deterministic if this helper is reused by tooling.
	const auto savedFormula = spellDirectDamage(rules, spell.toSpell()->getJsonKey());
	const int64_t baseDamage = savedFormula
		? savedFormula->evaluate(spellPower, divisor)
		: DirectDamageFormula{20, 20}.evaluate(spellPower, divisor);
	return baseDamage * (1000 + modifiers.damagePercentTenths * overcharge) / 1000;
}

std::vector<SpellSchool> activeSchools(const JsonNode & rules)
{
	if(legacy(rules))
		return {SpellSchool::AIR, SpellSchool::FIRE, SpellSchool::WATER, SpellSchool::EARTH};
	std::vector<SpellSchool> result;
	for(const auto & school : rules["schools"].Vector())
		result.emplace_back(resolve("spellSchool", school.String()));
	return result;
}

std::vector<SpellSchool> spellSchools(const JsonNode & rules, SpellID spell)
{
	if(!spellAllowedBySavedRoster(rules, spell))
		return {};
	const auto * definition = spell.toSpell();
	if(isAdventureSpell(rules, spell))
		return {};
	if(legacy(rules) || !definition->isCommonHeroSpell())
		return {definition->schools.begin(), definition->schools.end()};
	std::vector<SpellSchool> result;
	for(const auto & school : entry(rules, spell)["schools"].Vector())
		result.emplace_back(resolve("spellSchool", school.String()));
	require(!result.empty(), "unclassified hero spell " + definition->getJsonKey());
	return result;
}

std::vector<SecondarySkill> schoolSkills(const JsonNode & rules)
{
	if(legacy(rules))
		return {};

	std::vector<SecondarySkill> result;
	result.reserve(rules["schoolSkills"].Struct().size());
	for(const auto & [school, skill] : rules["schoolSkills"].Struct())
	{
		(void)school;
		result.emplace_back(resolve(SecondarySkill::entityType(), skill.String()));
	}
	return result;
}

int requiredSchoolRank(const JsonNode & rules, SpellID spell)
{
	if(legacy(rules) || !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return 0;
	return std::clamp(spellLevel(rules, spell) - 2, 0, 3);
}

std::vector<SecondarySkill> spellSchoolSkills(const JsonNode & rules, SpellID spell)
{
	if(legacy(rules) || !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return {};
	std::vector<SecondarySkill> result;
	for(const auto school : spellSchools(rules, spell))
	{
		for(const auto & [schoolName, skillNode] : rules["schoolSkills"].Struct())
		{
			if(resolve("spellSchool", schoolName) == school.getNum())
			{
				result.emplace_back(resolve(SecondarySkill::entityType(), skillNode.String()));
				break;
			}
		}
	}
	return result;
}

bool hasSchoolProficiency(const CGHeroInstance * hero, SpellID spell)
{
	if(!hero)
		return false;
	const auto & rules = hero->getMagicRules();
	if(!legacy(rules) && !spellAllowedBySavedRoster(rules, spell))
		return false;
	const int required = requiredSchoolRank(rules, spell);
	if(required == 0)
		return true;

	for(const auto skill : spellSchoolSkills(rules, spell))
	{
		if(static_cast<int>(hero->getSecSkillLevel(skill)) >= required)
			return true;
	}
	return false;
}

int spellLevel(const JsonNode & rules, SpellID spell)
{
	if(!spellAllowedBySavedRoster(rules, spell))
		return 0;
	if(isAdventureSpell(rules, spell))
		return 0;
	if(legacy(rules) || entry(rules, spell)["level"].isNull())
		return spell.toSpell()->getLevel();
	return entry(rules, spell)["level"].Integer();
}

SecondarySkill replacementSkill(const JsonNode & rules, SecondarySkill skill)
{
	if(legacy(rules) || skill == SecondarySkill::NONE)
		return skill;
	const auto & replacement = rules["skillReplacements"][SecondarySkill::encode(skill.getNum())];
	return replacement.isNull() ? skill : SecondarySkill(resolve(SecondarySkill::entityType(), replacement.String()));
}

bool skillAllowed(const JsonNode & rules, SecondarySkill skill, const std::set<SecondarySkill> & mapAllowed)
{
	if(!mapAllowed.count(skill))
		return false;
	if(legacy(rules))
		return !SecondarySkill::encode(skill.getNum()).starts_with(GameConstants::NEW_HORIZONS_MOD_SCOPE + ':');
	if(replacementSkill(rules, skill) != skill)
		return false;
	// Preserve map-authored bans on a legacy skill when its replacement is offered.
	for(const auto & [oldSkill, newSkill] : rules["skillReplacements"].Struct())
		if(resolve(SecondarySkill::entityType(), newSkill.String()) == skill.getNum()
			&& !mapAllowed.count(SecondarySkill(resolve(SecondarySkill::entityType(), oldSkill))))
			return false;
	return true;
}

int factionSpellWeight(const JsonNode & rules, FactionID faction, SpellID spell)
{
	if(!spellAllowedBySavedRoster(rules, spell))
		return 0;
	if(isAdventureSpell(rules, spell))
		return 0;
	const auto & identity = rules["factions"][FactionID::encode(faction.getNum())];
	if(legacy(rules) || identity.isNull())
		return spell.toSpell()->getProbability(faction);
	const auto & membership = entry(rules, spell)["schools"];
	for(const auto & rank : {"major", "minor"})
	{
		for(const auto & school : membership.Vector())
		{
			if(school.String() == identity[rank].String())
			{
				const auto & weight = rules["factionWeights"][rank];
				return weight.isNull() ? (std::string(rank) == "major" ? 3 : 1) : weight.Integer();
			}
		}
	}
	return 0;
}

int spellCost(const JsonNode & rules, SpellID spell, int mastery)
{
	require(spellAllowedBySavedRoster(rules, spell), "spell cost requested outside saved roster");
	if(isAdventureSpell(rules, spell))
		return adventureSpellCost(rules, spell);
	mastery = std::clamp(mastery, 0, 3);
	if(legacy(rules) || entry(rules, spell)["costs"].isNull())
		return spell.toSpell()->getCost(mastery);
	return entry(rules, spell)["costs"].Vector().at(mastery).Integer();
}

int wisdomAdjustedCost(int listedCost, int listedCostMultiplier, int rank)
{
	require(listedCost >= 0, "negative listed spell cost");
	require(listedCostMultiplier >= 1, "spell cost multiplier must be positive");
	require(rank >= MasteryLevel::NONE && rank <= MasteryLevel::EXPERT, "invalid Wisdom rank");
	const int64_t multiplied = static_cast<int64_t>(listedCost) * listedCostMultiplier;
	const int discountPercent = 10 * rank;
	return static_cast<int>(std::max<int64_t>(1, (multiplied * (100 - discountPercent) + 99) / 100));
}

int wisdomRank(const CGHeroInstance * hero)
{
	if(!hero || !rulesActive(hero->getMagicRules()))
		return MasteryLevel::NONE;
	const int decoded = SecondarySkill::decode("new-horizons:wisdom");
	if(decoded < 0)
		return MasteryLevel::NONE;
	return std::clamp(static_cast<int>(hero->getSecSkillLevel(SecondarySkill(decoded))),
		static_cast<int>(MasteryLevel::NONE), static_cast<int>(MasteryLevel::EXPERT));
}

bool isAdventureSpell(const JsonNode & rules, SpellID spell)
{
	if(legacy(rules) || !spell.hasValue() || !spell.toSpell() || !rules["adventureSpells"].isStruct())
		return false;
	return rules["adventureSpells"].Struct().contains(spell.toSpell()->getJsonKey());
}

int adventureSpellCost(const JsonNode & rules, SpellID spell)
{
	require(isAdventureSpell(rules, spell), "adventure spell cost requested outside saved roster");
	const auto & cost = adventureEntry(rules, spell)["cost"];
	require(integer(cost, 0, 1000000), "adventure spell cost");
	return cost.Integer();
}

bool adventureSpellRulesActive(const JsonNode & rules)
{
	return rulesActive(rules) && rules["adventureSpells"].isStruct();
}

bool isLandMine(SpellID spell)
{
	return spell == SpellID(SpellID::LAND_MINE);
}

bool isFireWall(SpellID spell)
{
	return spell == SpellID(SpellID::FIRE_WALL);
}

int landMineHexCount(int32_t spellPower)
{
	if(spellPower < 0)
		throw std::invalid_argument("Land Mine spell power cannot be negative");
	if(spellPower < LAND_MINE_THREE_HEX_POWER)
		return 2;
	if(spellPower < LAND_MINE_FOUR_HEX_POWER)
		return 3;
	return 4;
}

bool isCounterspell(const spells::Spell * spell)
{
	return spell && spell->getJsonKey() == GameConstants::NEW_HORIZONS_COUNTERSPELL;
}

int counterspellCost(int listedCost, bool countermage, bool countersequence)
{
	if(listedCost < 0)
		throw std::invalid_argument("Counterspell requires a non-negative listed spell cost");
	if(!countermage && !countersequence)
		return listedCost * 2;
	return (listedCost * 7 + 3) / 4;
}

int metamagicRank(const CGHeroInstance * hero)
{
	if(!hero)
		return 0;
	const int skillRank = hero->getPerkSkillRank(std::string(METAMAGIC_SKILL));
	const int rankBonus = hero->valOfBonuses(BonusType::METAMAGIC_USES_PER_COMBAT);
	return std::clamp(std::max(skillRank, rankBonus), 0, 3);
}

bool hasMetamagicPerk(const CGHeroInstance * hero, std::string_view perkId)
{
	return hero && hero->hasActivePerk(std::string(METAMAGIC_SKILL), std::string(perkId));
}

bool hasStormcallerPerk(const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(!hero || !spell || !rulesActive(hero->getMagicRules())
		|| !hero->hasActivePerk("new-horizons:havocMagic", std::string(HAVOC_STORMCALLER)))
		return false;
	const auto & key = spell->getJsonKey();
	return key == "core:lightningBolt"
		|| key == "core:chainLightning"
		|| key == "new-horizons:masterChainLightning";
}

bool hasAnnihilatorPerk(const CGHeroInstance * hero, const spells::Spell * spell)
{
	return hero && spell && rulesActive(hero->getMagicRules())
		&& hero->hasActivePerk("new-horizons:havocMagic", std::string(HAVOC_ANNIHILATOR))
		&& spell->getJsonKey() == "new-horizons:disintegrate";
}
}
