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
#include "ISpellMechanics.h"

#include "../ResourceSet.h"
#include "../CStack.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
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
#include "../battle/Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
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

const AdventureSpellDefinition * adventureSpellDefinition(std::string_view identity)
{
	const auto found = std::find_if(ADVENTURE_SPELLS.begin(), ADVENTURE_SPELLS.end(), [identity](const auto & expected)
	{
		return expected.identity == identity;
	});
	return found == ADVENTURE_SPELLS.end() ? nullptr : &*found;
}

bool sorceryMember(const SpellSchool school)
{
	return school.serializationKey() == "new-horizons:sorcery";
}

constexpr std::array<int, 4> SCHOOL_RANK_POWER_COEFFICIENT_PERCENT{100, 115, 130, 145};
constexpr std::array<int, 4> SPELLCRAFT_EFFICIENCY_PERCENT{100, 110, 120, 130};

bool hasCanonicalSchoolRankPowerCoefficientPercent(const JsonNode & rules)
{
	const auto & factors = rules["schoolRankPowerCoefficientPercent"];
	if(!factors.isVector() || factors.Vector().size() != SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.size())
		return false;
	for(size_t index = 0; index < SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.size(); ++index)
	{
		const auto & factor = factors.Vector()[index];
		if(factor.getType() != JsonNode::JsonType::DATA_INTEGER
			|| !integer(factor, SCHOOL_RANK_POWER_COEFFICIENT_PERCENT[index], SCHOOL_RANK_POWER_COEFFICIENT_PERCENT[index]))
			return false;
	}
	return true;
}

bool hasCanonicalSpellcraftEfficiencyPercent(const JsonNode & rules)
{
	const auto & factors = rules["spellcraftEfficiencyPercent"];
	if(!factors.isVector() || factors.Vector().size() != SPELLCRAFT_EFFICIENCY_PERCENT.size())
		return false;
	for(size_t index = 0; index < SPELLCRAFT_EFFICIENCY_PERCENT.size(); ++index)
	{
		const auto & factor = factors.Vector()[index];
		if(factor.getType() != JsonNode::JsonType::DATA_INTEGER
			|| !integer(factor, SPELLCRAFT_EFFICIENCY_PERCENT[index], SPELLCRAFT_EFFICIENCY_PERCENT[index]))
			return false;
	}
	return true;
}

int registeredSpellcraftRank(const CGHeroInstance * hero)
{
	if(!hero || !LIBRARY || !LIBRARY->identifiers())
		return 0;
	const auto skillId = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(),
		SecondarySkill::entityType(), std::string(newHorizonsMagic::SPELLCRAFT_SKILL));
	if(!skillId || *skillId < 0)
		return 0;
	return std::clamp<int>(hero->getSecSkillLevel(SecondarySkill(*skillId)), 0, MasteryLevel::EXPERT);
}

std::string percentFromBasisPoints(int basisPoints)
{
	const int wholePercent = basisPoints / 100;
	int hundredths = basisPoints % 100;
	if(hundredths == 0)
		return std::to_string(wholePercent) + "%";
	if(hundredths < 10)
		return std::to_string(wholePercent) + ".0" + std::to_string(hundredths) + "%";
	if(hundredths % 10 == 0)
		return std::to_string(wholePercent) + "." + std::to_string(hundredths / 10) + "%";
	return std::to_string(wholePercent) + "." + std::to_string(hundredths) + "%";
}

std::string fixedPointFromScaledValue(int64_t scaledValue, int decimalPlaces)
{
	int64_t scale = 1;
	for(int index = 0; index < decimalPlaces; ++index)
		scale *= 10;
	const int64_t whole = scaledValue / scale;
	int64_t fractional = scaledValue % scale;
	if(fractional == 0)
		return std::to_string(whole);
	std::string fractionalText = std::to_string(fractional);
	fractionalText.insert(0, static_cast<size_t>(decimalPlaces) - fractionalText.size(), '0');
	while(!fractionalText.empty() && fractionalText.back() == '0')
		fractionalText.pop_back();
	return std::to_string(whole) + "." + fractionalText;
}
}

bool rulesActive(const JsonNode & rules)
{
	return !legacy(rules) && rules.isStruct()
		&& integer(rules["rulesetVersion"], RULESET_VERSION, CURRENT_RULESET_VERSION);
}

bool berserkUsesSingleCreatureTarget(const JsonNode & rules)
{
	return rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
}

bool dispelUsesNewHorizonsRules(const JsonNode & rules)
{
	return rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
}

int chainLightningTargetCount(const JsonNode & rules, SpellID spell, int configuredTargetCount)
{
	if(configuredTargetCount <= 1 || spell != SpellID(SpellID::CHAIN_LIGHTNING)
		|| !rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| !spellAllowedBySavedRoster(rules, spell))
		return configuredTargetCount;

	return CHAIN_LIGHTNING_FIXED_TARGET_COUNT_V3;
}

bool expertRangeIsSingleTarget(const JsonNode & rules, SpellID spell)
{
	if(!rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return false;

	switch(spell.getNum())
	{
	case SpellID::CURE:
	case SpellID::BLESS:
	case SpellID::CURSE:
	case SpellID::SLOW:
	case SpellID::DISPEL:
	case SpellID::SHIELD:
	case SpellID::AIR_SHIELD:
	case SpellID::PROTECTION_FROM_AIR:
	case SpellID::PROTECTION_FROM_FIRE:
	case SpellID::PROTECTION_FROM_WATER:
	case SpellID::PROTECTION_FROM_EARTH:
	case SpellID::BLOODLUST:
	case SpellID::PRECISION:
	case SpellID::WEAKNESS:
	case SpellID::STONE_SKIN:
	case SpellID::PRAYER:
	case SpellID::MIRTH:
	case SpellID::SORROW:
	case SpellID::FORTUNE:
	case SpellID::MISFORTUNE:
	case SpellID::HASTE:
	case SpellID::COUNTERSTRIKE:
	case SpellID::FORGETFULNESS:
		return true;
	default:
		return false;
	}
}

bool mageGuildGenerationActive(const JsonNode & rules)
{
	if(!rulesActive(rules) || !rules["mageGuildGeneration"].isStruct())
		return false;
	const auto & generation = rules["mageGuildGeneration"];
	if(!integer(generation["rulesetVersion"], MAGE_GUILD_GENERATION_RULESET_VERSION,
		MAGE_GUILD_GENERATION_RULESET_VERSION)
		|| !generation["nonPreferredSlots"].isVector()
		|| generation["nonPreferredSlots"].Vector().size() != 5)
		return false;
	constexpr std::array<int, 5> expectedSlots{3, 2, 0, 0, 0};
	for(size_t level = 0; level < expectedSlots.size(); ++level)
		if(!integer(generation["nonPreferredSlots"].Vector()[level], expectedSlots[level], expectedSlots[level]))
			return false;
	return true;
}

int mageGuildSpellsAtLevel(const JsonNode & rules, int level)
{
	if(level < 1 || level > 5)
		return 0;
	if(!mageGuildGenerationActive(rules))
		return 6 - level;
	return 2 + rules["mageGuildGeneration"]["nonPreferredSlots"].Vector().at(level - 1).Integer();
}

std::vector<SpellSchool> preferredSchools(const JsonNode & rules, FactionID faction)
{
	if(!mageGuildGenerationActive(rules))
		return {};
	const auto & identity = rules["factions"][FactionID::encode(faction.getNum())];
	if(!identity.isStruct())
		return {};
	return {
		SpellSchool(resolve("spellSchool", identity["preferredA"].String())),
		SpellSchool(resolve("spellSchool", identity["preferredB"].String()))
	};
}

int masterChainLightningRetentionPercent(int heroLevel)
{
	return std::min(90, 75 + std::max(0, heroLevel));
}

int blessDurationFromPowerTerm(int64_t spellPowerTerm)
{
	if(spellPowerTerm <= 0)
		return BLESS_BASE_DURATION;
	const int64_t remainingDuration = BLESS_MAX_DURATION - BLESS_BASE_DURATION;
	if(spellPowerTerm >= remainingDuration)
		return BLESS_MAX_DURATION;
	return BLESS_BASE_DURATION + static_cast<int>(spellPowerTerm);
}

bool hasBenedictionPerk(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(std::string(LIGHT_MAGIC_SKILL), std::string(LIGHT_BENEDICTION));
}

std::string spellDescriptionForHero(const CGHeroInstance * hero, const spells::Spell * spell, int schoolLevel)
{
	if(!spell)
		return {};

	std::string result = spell->getDescriptionTranslated(schoolLevel);
	if(hero && physicalPoisonEnabled(hero->getMagicRules(), spell->getId()))
	{
		result = "Target one enemy living stack. It suffers physical Poison damage on its next three activations: "
			"20 + 0.5 x Spell Power, then 1.5x and 2x that amount. Nature rank scales only the Spell Power term. "
			"The affliction does not spread; Cure removes it, while Dispel does not.";
	}
	else if(hero && spell->getId() == SpellID::BERSERK
		&& berserkUsesSingleCreatureTarget(hero->getMagicRules()))
	{
		result = "Target one enemy stack. It attacks the nearest creature until its next attack. "
			"The ordinary cast targets only the selected stack at every mastery rank.";
	}
	else if(hero && spell->getId() == SpellID::DISPEL
		&& dispelUsesNewHorizonsRules(hero->getMagicRules()))
	{
		result = "Target one friendly or enemy stack. Removes all temporary magical buffs and debuffs. "
			"Does not remove Orders, innate creature states, poison, terrain, or summoned creatures. "
			"Sorcery rank does not make Dispel affect multiple stacks.";
	}
	else if(hero && spell->getId() == SpellID::CHAIN_LIGHTNING
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		result = "Select the first target. Lightning then jumps to the nearest eligible unstruck creature, "
			"including friendly stacks. It can strike up to five different stacks at every mastery rank; "
			"each later jump deals less damage.";
	}

	if(hero && spell->getId() == SpellID(SpellID::QUICKSAND)
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		const auto & rules = hero->getMagicRules();
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const auto patchCount = quicksandPatchCount(rules, hero, spell->getId(),
			std::max<int32_t>(0, hero->getEffectPower(spell)),
			std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)), 0, empowerBonusPercent);
		if(patchCount)
		{
			result = "Places " + std::to_string(*patchCount)
				+ " concealed Quicksand patches on legal empty ground hexes. The saved v3 base count is min(5, "
				+ "2 + floor(Spell Power / 60)); Nature rank, Spellcraft, and Empower strengthen only the Spell Power term. "
				+ "The current count excludes battle-only Warcasting, which may further strengthen the Spell Power term.";
		}
	}

	if(hero && rulesActive(hero->getMagicRules())
		&& spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const int32_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
		const bool chronomancer = hero->hasActivePerk(
			newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::CHRONOMANCER_PERK);
		const int radiusCap = newHorizonsSorcery::TIME_STOP_BASE_MAX_RADIUS
			+ (chronomancer ? newHorizonsSorcery::TIME_STOP_CHRONOMANCER_RADIUS_BONUS : 0);
		const int64_t radiusThreshold = static_cast<int64_t>(newHorizonsSorcery::TIME_STOP_POWER_PER_EXTRA_RADIUS)
			* SPELL_POWER_COEFFICIENT_BASIS_POINTS;
		const int64_t scaledPowerTerm = static_cast<int64_t>(spellPower) * coefficientBasisPoints
			* (100 + empowerBonusPercent) / (radiusThreshold * 100);
		const int radius = std::min(radiusCap,
			newHorizonsSorcery::TIME_STOP_BASE_RADIUS + static_cast<int>(scaledPowerTerm));
		const bool schoolRankRules = rules["rulesetVersion"].Integer()
			>= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;

		result = "Place the selected area outside time until the beginning of the caster's next Hero Action. ";
		if(schoolRankRules)
		{
			result += "Under saved v3 rules, Sorcery rank scales only the Spell Power term: 100% with no rank, "
				"115% at Basic, 130% at Advanced, and 145% at Expert. Spellcraft efficiency multiplies that School factor; "
				"the current combined coefficient is " + percentFromBasisPoints(coefficientBasisPoints) + ". The radius is min("
				+ std::to_string(radiusCap) + ", 1 + floor(" + std::to_string(coefficientBasisPoints)
				+ " x Spell Power / (10000 x " + std::to_string(newHorizonsSorcery::TIME_STOP_POWER_PER_EXTRA_RADIUS)
				+ "))); Chronomancer raises the cap to 3 without "
				"adding a free radius step. Current ordinary radius without battle-only Warcasting at Spell Power "
				+ std::to_string(spellPower) + ": " + std::to_string(radius) + ". Warcasting also scales only this "
				"Spell Power term when the shared cast scaler supplies its bonus. Duration and targeting do not "
				"change with Sorcery rank.";
		}
		else
		{
			result += "Saved v1/v2 rules use a 100% Spell Power coefficient at every rank. Radius is min("
				+ std::to_string(radiusCap) + ", 1 + floor(Spell Power / "
				+ std::to_string(newHorizonsSorcery::TIME_STOP_POWER_PER_EXTRA_RADIUS) + ")); Chronomancer raises "
				"the cap to 3 without adding a free radius step. Current radius at Spell Power "
				+ std::to_string(spellPower) + ": " + std::to_string(radius)
				+ ". Warcasting also scales only this Spell Power term when the saved profile and shared cast scaler "
				"supply its bonus. Duration and targeting are unchanged.";
		}
		result += " Creatures in stasis cannot act, receive damage or healing, teleport, receive new effects, "
			"or lose duration from existing effects.";
	}

	if(hero && cureEnabled(hero->getMagicRules(), spell->getId()))
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const int64_t coefficientTenThousandths = 15'000LL * coefficientBasisPoints
			* (100 + empowerBonusPercent) / (SPELL_POWER_COEFFICIENT_BASIS_POINTS * 100LL);
		const std::string spellPowerCoefficient = fixedPointFromScaledValue(coefficientTenThousandths, 4);
		result = "Targets one friendly living stack. Base healing is 25 + " + spellPowerCoefficient
			+ " \u00d7 Spell Power HP and cannot resurrect casualties. "
			"If the target has Poison or Disease, choose one physical affliction to remove. "
			"Cure does not remove magical effects.";
	}

	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spell->getId() == SpellID::BLESS)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const int64_t power = std::max<int32_t>(0, hero->getEffectPower(spell));
		const int64_t termDivisor = static_cast<int64_t>(BLESS_SPELL_POWER_DURATION_DIVISOR)
			* SPELL_POWER_COEFFICIENT_BASIS_POINTS * 100;
		const int64_t term = power * coefficientBasisPoints * (100 + empowerBonusPercent) / termDivisor;
		const int ordinaryDuration = blessDurationFromPowerTerm(term);
		const int64_t spellDurationModifier = static_cast<int64_t>(hero->valOfBonuses(
			BonusType::SPELL_DURATION, BonusSubtypeID()))
			+ hero->valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID(SpellID(SpellID::BLESS)))
			+ spellDurationBonus(hero, SpellID::BLESS);
		const bool benediction = hasBenedictionPerk(hero);
		const int64_t totalDuration = std::max<int64_t>(0, ordinaryDuration + spellDurationModifier
			+ (benediction ? 1 : 0));
		result += "\n\nCurrent ordinary duration: " + std::to_string(ordinaryDuration)
			+ (ordinaryDuration == 1 ? " round" : " rounds") + " (combined Spell Power coefficient: "
			+ percentFromBasisPoints(coefficientBasisPoints) + "). Current total before battle-only adjustments: "
			+ std::to_string(totalDuration) + (totalDuration == 1 ? " round." : " rounds.");
		if(spellDurationModifier != 0)
			result += " SPELL_DURATION modifier: " + std::to_string(spellDurationModifier) + ".";
		if(benediction)
			result += " Benediction adds 1 round.";
	}

	if(hero && rulesActive(hero->getMagicRules())
		&& spell->getJsonKey() == "new-horizons:masterChainLightning")
	{
		// Keep the translated base description complete for hero-independent help
		// surfaces, then add the live value only where a hero context exists.
		result += "\n\nCurrent retention: "
			+ std::to_string(masterChainLightningRetentionPercent(hero->level)) + "%.";
	}

	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() >= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spell->getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const bool spellbinder = hero->hasActivePerk(
			newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::SPELLBINDER_PERK);
		const int duration = newHorizonsSorcery::spellLockDurationBasisPoints(
			hero->getEffectPower(spell), spellbinder, coefficientBasisPoints, 0, empowerBonusPercent);
		result += "\n\nCurrent combined Spell Power coefficient: "
			+ percentFromBasisPoints(coefficientBasisPoints)
			+ (empowerBonusPercent > 0 ? " with Empower Spell +25%; ordinary duration at current Spell Power: "
				: "; ordinary duration at current Spell Power: ") + std::to_string(duration)
			+ (duration == 1 ? " round." : " rounds.");
	}

	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& hero->getMagicRules().Struct().contains("spellcraftEfficiencyPercent")
		&& registeredSpellcraftRank(hero) > MasteryLevel::NONE
		&& spell->isCommonHeroSpell() && spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId())
		&& !isAdventureSpell(hero->getMagicRules(), spell->getId())
		&& !spellSchoolSkills(hero->getMagicRules(), spell->getId()).empty())
	{
		const int efficiencyPercent = spellcraftEfficiencyPercent(hero->getMagicRules(), registeredSpellcraftRank(hero));
		result += "\n\nSpellcraft efficiency: " + std::to_string(efficiencyPercent)
			+ "% on Spell Power-derived terms, multiplying with School rank.";
	}

	if(!hero || !rulesActive(hero->getMagicRules()))
		return result;
	const auto & savedMagicRules = hero->getMagicRules();
	const bool focusMagicCoefficient = spell->getJsonKey() == newHorizonsSorcery::FOCUS_MAGIC_SPELL;
	const bool hasSavedSchoolRankCoefficient = savedMagicRules["rulesetVersion"].Integer()
		>= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	if((!hasSavedSchoolRankCoefficient && !focusMagicCoefficient)
		|| spellSchoolSkills(savedMagicRules, spell->getId()).empty())
		return result;

	const int coefficientPercent = spellPowerCoefficientPercent(savedMagicRules, hero, spell->getId());
	const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(savedMagicRules, hero, spell->getId());
	const bool cureCoefficient = cureEnabled(hero->getMagicRules(), spell->getId());
	const bool transfigureCoefficient = spell->getJsonKey() == "new-horizons:transfigureMatter";
	const bool phantomCoefficient = spell->getJsonKey() == "new-horizons:phantomArmy";
	const auto formula = spellDirectDamage(hero->getMagicRules(), spell->getJsonKey());
	const bool damageCoefficient = spell->isDamage()
		&& (spell->getBasePower() != 0 || (formula && formula->powerCoefficient != 0));
	if(!focusMagicCoefficient && !cureCoefficient && !transfigureCoefficient && !phantomCoefficient && !damageCoefficient)
		return result;

	constexpr std::array<std::string_view, 4> rankNames{
		"No School Rank", "Basic School", "Advanced School", "Expert School"};
	if(focusMagicCoefficient)
	{
		if(hasSavedSchoolRankCoefficient)
		{
			const auto rank = std::find(SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.begin(),
				SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.end(), coefficientPercent);
			if(rank == SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.end())
				return result;
			const auto rankIndex = static_cast<size_t>(std::distance(SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.begin(), rank));
			constexpr std::array<std::string_view, 4> sorceryRankNames{
				"No Sorcery Rank", "Basic Sorcery", "Advanced Sorcery", "Expert Sorcery"};
			result += "\n\n" + std::string(sorceryRankNames[rankIndex]) + ": "
				+ std::to_string(coefficientPercent) + "% coefficient on the Spell Power term.";
		}
		else
		{
			result += "\n\nLegacy profile: 100% coefficient on the Spell Power term; "
				"Sorcery rank does not scale Focus Magic.";
		}

		const int64_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
		const int64_t scaledTerm = spellPower * newHorizonsSorcery::ARCANE_BREACH_POWER_BASIS_POINTS
			* coefficientBasisPoints / SPELL_POWER_COEFFICIENT_BASIS_POINTS;
		const int64_t penetrationBasisPoints = std::min<int64_t>(
			newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS,
			newHorizonsSorcery::ARCANE_BREACH_BASE_BASIS_POINTS + scaledTerm);
		const auto fraction = penetrationBasisPoints % 100;
		result += "\nCurrent ordinary per-mark penetration at Spell Power " + std::to_string(spellPower)
			+ ": " + std::to_string(penetrationBasisPoints / 100) + "."
			+ (fraction < 10 ? "0" : "") + std::to_string(fraction)
			+ "% (before battle-only Warcasting).";
		return result;
	}

	const auto rank = std::find(SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.begin(),
		SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.end(), coefficientPercent);
	if(rank == SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.end())
		return result;
	const auto rankIndex = static_cast<size_t>(std::distance(SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.begin(), rank));
	result += "\n\n" + std::string(rankNames[rankIndex]) + ": "
		+ std::to_string(coefficientPercent)
		+ (transfigureCoefficient ? "% Spell Power-derived golem HP."
			: phantomCoefficient ? "% Spell Power-derived Phantom Integrity."
			: cureCoefficient ? "% Spell Power-derived healing."
			: "% Spell Power damage coefficient.");
	return result;
}

void validateRules(const JsonNode & rules)
{
	if(legacy(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "schools", "adventureSpells", "spells", "factions", "factionWeights", "schoolSkills", "skillReplacements", "warcasting", "spellPoints", "mageGuildGeneration", "physicalDamageReductionCapPercent", "schoolRankPowerCoefficientPercent", "spellcraftEfficiencyPercent"});
	require(integer(rules["schemaVersion"], 1, 1), "schemaVersion");
	require(integer(rules["rulesetVersion"], RULESET_VERSION, CURRENT_RULESET_VERSION), "rulesetVersion");
	const int version = rules["rulesetVersion"].Integer();
	if(version == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		require(hasCanonicalSchoolRankPowerCoefficientPercent(rules), "canonical school-rank Spell Power coefficient factors");
	else
		require(!rules.Struct().contains("schoolRankPowerCoefficientPercent"), "school-rank coefficient factors require magic rules v3");
	if(rules.Struct().contains("spellcraftEfficiencyPercent"))
	{
		require(version == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
			"Spellcraft efficiency factors require magic rules v3");
		require(hasCanonicalSpellcraftEfficiencyPercent(rules), "canonical Spellcraft efficiency factors");
	}
	if(rules.Struct().contains("physicalDamageReductionCapPercent"))
	{
		require(version >= DIRECT_DAMAGE_RULESET_VERSION, "Physical reduction requires magic rules v2 or later");
		const auto & cap = rules["physicalDamageReductionCapPercent"];
		require(cap.getType() == JsonNode::JsonType::DATA_INTEGER && integer(cap, 0, 100),
			"integer physicalDamageReductionCapPercent in [0,100]");
	}
	if(rules.Struct().contains("spellPoints"))
	{
		const auto & spellPoints = rules["spellPoints"];
		require(version >= DIRECT_DAMAGE_RULESET_VERSION, "Spell Points require magic rules v2 or later");
		require(spellPoints.isStruct(), "Spell Points object");
		fields(spellPoints, {"rulesetVersion", "intelligenceMaximumPercent"});
		require(spellPoints["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& integer(spellPoints["rulesetVersion"], SPELL_POINTS_RULESET_VERSION, SPELL_POINTS_RULESET_VERSION),
			"Spell Points rulesetVersion");
		require(spellPoints["intelligenceMaximumPercent"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& integer(spellPoints["intelligenceMaximumPercent"], 100, 1000),
			"Spell Points intelligenceMaximumPercent");
	}
	if(rules.Struct().contains("mageGuildGeneration"))
	{
		const auto & generation = rules["mageGuildGeneration"];
		require(version >= DIRECT_DAMAGE_RULESET_VERSION, "Mage Guild generation requires magic rules v2 or later");
		require(generation.isStruct(), "Mage Guild generation object");
		fields(generation, {"rulesetVersion", "nonPreferredSlots"});
		require(integer(generation["rulesetVersion"], MAGE_GUILD_GENERATION_RULESET_VERSION,
			MAGE_GUILD_GENERATION_RULESET_VERSION), "Mage Guild generation rulesetVersion");
		require(generation["nonPreferredSlots"].isVector()
			&& generation["nonPreferredSlots"].Vector().size() == 5, "five Mage Guild non-preferred slot counts");
		constexpr std::array<int, 5> expectedSlots{3, 2, 0, 0, 0};
		for(size_t level = 0; level < expectedSlots.size(); ++level)
			require(integer(generation["nonPreferredSlots"].Vector()[level], expectedSlots[level], expectedSlots[level]),
				"canonical Mage Guild non-preferred slot profile");
	}
	if(rules.Struct().contains("warcasting"))
	{
		require(version >= DIRECT_DAMAGE_RULESET_VERSION, "Warcasting requires magic rules v2 or later");
		require(rules["warcasting"].isBool(), "boolean Warcasting setting");
	}
	if(version >= DIRECT_DAMAGE_RULESET_VERSION)
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
			fields(data, {"schools", "level", "costs", "directDamage", "active", "cureAfflictions"});
		if(version >= DIRECT_DAMAGE_RULESET_VERSION && data.Struct().contains("active"))
			require(data["active"].isBool(), "spell active flag");
		if(version >= DIRECT_DAMAGE_RULESET_VERSION && data.Struct().contains("cureAfflictions"))
		{
			require(name == "core:cure", "cureAfflictions is only valid for core:cure");
			const auto & afflictions = data["cureAfflictions"];
			require(afflictions.isVector() && !afflictions.Vector().empty(), "non-empty Cure afflictions array");
			std::set<std::string> uniqueAfflictions;
			for(const auto & affliction : afflictions.Vector())
			{
				require(affliction.isString(), "Cure affliction identity");
				const auto & identity = affliction.String();
				require(uniqueAfflictions.insert(identity).second, "duplicate Cure affliction");
				const auto afflictionID = resolve("spell", identity);
				const SpellID sourceSpell(afflictionID);
				require((sourceSpell == SpellID::POISON || sourceSpell == SpellID::DISEASE)
					&& sourceSpell.toSpell(), "only Poison and Disease are supported Cure afflictions");
			}
		}
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
			require(version >= DIRECT_DAMAGE_RULESET_VERSION, "NH common spells require ruleset version 2 or later");
		if(version >= DIRECT_DAMAGE_RULESET_VERSION && name == "core:magicArrow")
			require(directDamageFormula(data, version).has_value(), "Magic Arrow requires saved directDamage in ruleset version 2 or later");
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
		require(!mageGuildGenerationActive(rules), "fixed Mage Guild generation does not use faction weights");
		fields(rules["factionWeights"], {"major", "minor"});
		require(integer(rules["factionWeights"]["major"], 1, 1000)
			&& integer(rules["factionWeights"]["minor"], 1, 1000), "positive faction weights");
	}
	if(!rules["factions"].isNull())
	{
		require(rules["factions"].isStruct(), "factions object");
		const bool fixedGuildGeneration = mageGuildGenerationActive(rules);
		for(const auto & [name, data] : rules["factions"].Struct())
		{
			resolve("faction", name);
			const char * first = fixedGuildGeneration ? "preferredA" : "major";
			const char * second = fixedGuildGeneration ? "preferredB" : "minor";
			fields(data, {first, second, "provisional"});
			require(data[first].isString() && data[second].isString(), "faction school identifiers");
			require(schools.count(data[first].String()) && schools.count(data[second].String()), "inactive faction school");
			require(data[first].String() != data[second].String(), "distinct preferred schools required");
			require(data["provisional"].isNull() || data["provisional"].isBool(), "provisional flag");
		}
		if(fixedGuildGeneration)
		{
			for(const auto faction : {FactionID::CASTLE, FactionID::RAMPART, FactionID::TOWER,
				FactionID::INFERNO, FactionID::NECROPOLIS, FactionID::DUNGEON,
				FactionID::STRONGHOLD, FactionID::FORTRESS, FactionID::CONFLUX})
				require(rules["factions"].Struct().contains(FactionID::encode(faction.getNum())),
					"fixed Mage Guild generation requires every core faction preference pair");
		}
	}
	else
		require(!mageGuildGenerationActive(rules), "fixed Mage Guild generation requires faction preferences");
}

int physicalDamageReductionCapPercent(const JsonNode & rules)
{
	if(!rulesActive(rules) || rules["rulesetVersion"].Integer() < DIRECT_DAMAGE_RULESET_VERSION)
		return -1;
	const auto & cap = rules["physicalDamageReductionCapPercent"];
	return cap.getType() == JsonNode::JsonType::DATA_INTEGER && integer(cap, 0, 100)
		? static_cast<int>(cap.Integer()) : -1;
}

bool spellPointRulesActive(const JsonNode & rules)
{
	if(legacy(rules) || !rules.isStruct() || !rules["spellPoints"].isStruct())
		return false;
	if(rules["rulesetVersion"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| !integer(rules["rulesetVersion"], DIRECT_DAMAGE_RULESET_VERSION, CURRENT_RULESET_VERSION))
		return false;
	const auto & spellPoints = rules["spellPoints"];
	return spellPoints["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& integer(spellPoints["rulesetVersion"], SPELL_POINTS_RULESET_VERSION, SPELL_POINTS_RULESET_VERSION)
		&& spellPoints["intelligenceMaximumPercent"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& integer(spellPoints["intelligenceMaximumPercent"], 100, 1000);
}

int32_t spellPointsIntelligenceMaximumPercent(const JsonNode & rules)
{
	return spellPointRulesActive(rules)
		? static_cast<int32_t>(rules["spellPoints"]["intelligenceMaximumPercent"].Integer())
		: SPELL_POINTS_INTELLIGENCE_MAXIMUM_PERCENT;
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
	require(integer(rules["rulesetVersion"], RULESET_VERSION, CURRENT_RULESET_VERSION), "rulesetVersion");
	const int version = rules["rulesetVersion"].Integer();
	if(version >= DIRECT_DAMAGE_RULESET_VERSION)
	{
		require(rules["schemaVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer schemaVersion");
		require(rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER, "integer rulesetVersion");
	}
	const auto found = rules["spells"].Struct().find(scopedIdentity);
	if(found == rules["spells"].Struct().end())
		return std::nullopt;
	return directDamageFormula(found->second, version);
}

std::optional<int64_t> directDamageValue(const JsonNode & rules, const std::string & scopedIdentity,
	int32_t effectPower, int32_t divisor, int coefficientPercent)
{
	const auto formula = spellDirectDamage(rules, scopedIdentity);
	if(!formula)
		return std::nullopt;
	return formula->evaluate(effectPower, divisor, coefficientPercent);
}

int schoolRankPowerCoefficientPercent(const JsonNode & rules, int schoolRank)
{
	if(schoolRank < 0 || schoolRank >= static_cast<int>(SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.size()))
		throw std::invalid_argument("Invalid New Horizons school rank");
	if(legacy(rules) || !rules.isStruct()
		|| !integer(rules["rulesetVersion"], RULESET_VERSION, CURRENT_RULESET_VERSION)
		|| rules["rulesetVersion"].Integer() < SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.front();
	require(hasCanonicalSchoolRankPowerCoefficientPercent(rules), "canonical school-rank Spell Power coefficient factors");
	return static_cast<int>(rules["schoolRankPowerCoefficientPercent"].Vector().at(static_cast<size_t>(schoolRank)).Integer());
}

int spellcraftEfficiencyPercent(const JsonNode & rules, int spellcraftRank)
{
	if(spellcraftRank < 0 || spellcraftRank >= static_cast<int>(SPELLCRAFT_EFFICIENCY_PERCENT.size()))
		throw std::invalid_argument("Invalid New Horizons Spellcraft rank");
	if(legacy(rules) || !rules.isStruct()
		|| !integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
			SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		|| !rules.Struct().contains("spellcraftEfficiencyPercent"))
		return SPELLCRAFT_EFFICIENCY_PERCENT.front();
	require(hasCanonicalSpellcraftEfficiencyPercent(rules), "canonical Spellcraft efficiency factors");
	return static_cast<int>(rules["spellcraftEfficiencyPercent"].Vector().at(static_cast<size_t>(spellcraftRank)).Integer());
}

int spellPowerCoefficientPercent(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell)
{
	if(!hero || legacy(rules) || !rules.isStruct()
		|| !integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
			SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return SCHOOL_RANK_POWER_COEFFICIENT_PERCENT.front();

	int highestSchoolRank = 0;
	for(const auto skill : spellSchoolSkills(rules, spell))
		highestSchoolRank = std::max(highestSchoolRank, static_cast<int>(hero->getSecSkillLevel(skill)));
	return schoolRankPowerCoefficientPercent(rules, highestSchoolRank);
}

int spellPowerCoefficientBasisPoints(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell)
{
	if(!hero || legacy(rules) || !rules.isStruct()
		|| !integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
			SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell))
		return SPELL_POWER_COEFFICIENT_BASIS_POINTS;

	const int schoolCoefficientPercent = spellPowerCoefficientPercent(rules, hero, spell);
	const int spellcraftCoefficientPercent = spellcraftEfficiencyPercent(rules, registeredSpellcraftRank(hero));
	return schoolCoefficientPercent * spellcraftCoefficientPercent;
}

std::optional<int> quicksandPatchCount(const JsonNode & rules, const CGHeroInstance * hero,
	const SpellID spell, const int32_t spellPower, const int32_t spellPowerDivisor,
	const int warcastingBonusPercent, const int empowerBonusPercent)
{
	if(!rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		|| spell != SpellID(SpellID::QUICKSAND)
		|| !spellAllowedBySavedRoster(rules, spell))
		return std::nullopt;

	if(spellPower < 0 || spellPowerDivisor <= 0
		|| spellPowerDivisor > std::numeric_limits<int32_t>::max() / QUICKSAND_SPELL_POWER_PER_PATCH_V3)
		throw std::invalid_argument("Invalid Quicksand Spell Power inputs");

	const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell);
	const int32_t divisor = spellPowerDivisor * QUICKSAND_SPELL_POWER_PER_PATCH_V3;
	const int64_t spellPowerPatches = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		spellPower, divisor, coefficientBasisPoints, warcastingBonusPercent, empowerBonusPercent);
	return QUICKSAND_BASE_PATCH_COUNT_V3 + static_cast<int>(std::min<int64_t>(
		QUICKSAND_MAX_PATCH_COUNT_V3 - QUICKSAND_BASE_PATCH_COUNT_V3, spellPowerPatches));
}

int empowerSpellBonusPercent(const JsonNode & rules, const CGHeroInstance * hero, SpellID spell,
	const int listedCostMultiplier)
{
	if(listedCostMultiplier < 1)
		throw std::invalid_argument("Spell cost multiplier must be positive");
	if(!hero || legacy(rules) || !rules.isStruct()
		|| !integer(rules["rulesetVersion"], SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION,
			SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		|| !spell.hasValue() || !spell.toSpell() || !spell.toSpell()->isCommonHeroSpell()
		|| !spellAllowedBySavedRoster(rules, spell) || isAdventureSpell(rules, spell)
		|| !hero->hasActivePerk(std::string(SPELLCRAFT_SKILL), std::string(SPELLCRAFT_EMPOWER_SPELL)))
		return 0;

	const int listedCost = hero->getListedSpellCost(spell.toSpell());
	const int wisdom = wisdomRank(hero);
	const int wisdomAdjustedCost = newHorizonsMagic::wisdomAdjustedCost(
		listedCost, listedCostMultiplier, wisdom);
	return wisdomAdjustedCost >= SPELLCRAFT_EMPOWER_MANA_THRESHOLD
		? SPELLCRAFT_EMPOWER_BONUS_PERCENT : 0;
}

int32_t regenerationRateMillionths(const int32_t spellPower, const int schoolRankCoefficientPercent,
	const bool herbalist, const int warcastingBonusPercent)
{
	if(spellPower < 0 || schoolRankCoefficientPercent < 0 || schoolRankCoefficientPercent > 1000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000)
		throw std::invalid_argument("Invalid Regeneration rate inputs");

	// 0.15% x Spell Power becomes 15 x Spell Power millionths of the
	// fractional rate. Keep the exact school-rank product so successive hits
	// do not lose fractional marks to per-hit rounding.
	const int64_t spellPowerTerm = static_cast<int64_t>(15) * spellPower
		* schoolRankCoefficientPercent * (100 + warcastingBonusPercent) / 100;
	const int64_t base = REGENERATION_BASE_RATE_MILLIONTHS;
	const int64_t herbalistBonus = herbalist ? REGENERATION_HERBALIST_BONUS_MILLIONTHS : 0;
	return static_cast<int32_t>(std::min<int64_t>(REGENERATION_MAX_RATE_MILLIONTHS,
		base + spellPowerTerm + herbalistBonus));
}

int32_t regenerationRateMillionthsBasisPoints(const int32_t spellPower, const int coefficientBasisPoints,
	const bool herbalist, const int warcastingBonusPercent, const int empowerSpellBonusPercent)
{
	if(spellPower < 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100'000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000)
		throw std::invalid_argument("Invalid Regeneration basis-point inputs");

	// 10000 basis points is 100%. Keep the exact School × Spellcraft product,
	// Warcasting, and Empower multipliers until the final rate floor.
	const int64_t spellPowerTerm = spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
		1500LL * spellPower, 1, coefficientBasisPoints,
		warcastingBonusPercent, empowerSpellBonusPercent);
	const int64_t base = REGENERATION_BASE_RATE_MILLIONTHS;
	const int64_t herbalistBonus = herbalist ? REGENERATION_HERBALIST_BONUS_MILLIONTHS : 0;
	return static_cast<int32_t>(std::min<int64_t>(REGENERATION_MAX_RATE_MILLIONTHS,
		base + spellPowerTerm + herbalistBonus));
}

int64_t regenerationHealAmount(const int64_t pendingMicroHealth, const int64_t survivingWounds)
{
	if(pendingMicroHealth < 0 || survivingWounds < 0)
		throw std::invalid_argument("Invalid Regeneration healing inputs");
	return std::min<int64_t>(pendingMicroHealth / REGENERATION_MARK_SCALE, survivingWounds);
}

bool magicArrowOverchargeEnabled(const JsonNode & rules, SpellID spell)
{
	if(spell != SpellID(SpellID::MAGIC_ARROW) || legacy(rules))
		return false;
	if(!rules.isStruct() || !integer(rules["rulesetVersion"], DIRECT_DAMAGE_RULESET_VERSION, CURRENT_RULESET_VERSION))
		return false;

	// Read the saved roster, rather than installed content.  This keeps old
	// v1 saves on their old Magic Arrow semantics even when a newer module is
	// installed.  The explicit v2+ formula is the compatibility marker for the
	// Sorcery overcharge contract; a saved roster without it is not activated.
	if(!spellAllowedBySavedRoster(rules, spell))
		return false;
	if(!spellDirectDamage(rules, spell.toSpell()->getJsonKey()))
		return false;

	return vstd::contains_if(spellSchools(rules, spell), sorceryMember);
}

bool cureEnabled(const JsonNode & rules, SpellID spell)
{
	return spell == SpellID::CURE && rulesActive(rules)
		&& rules["spells"].isStruct()
		&& rules["spells"].Struct().contains("core:cure")
		&& rules["spells"]["core:cure"].isStruct()
		&& rules["spells"]["core:cure"].Struct().contains("cureAfflictions")
		&& rules["spells"]["core:cure"]["cureAfflictions"].isVector();
}

bool physicalPoisonEnabled(const JsonNode & rules, SpellID spell)
{
	const auto * definition = spell.toSpell();
	return definition && definition->getJsonKey() == NATURE_POISON_SPELL && rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(rules, spell);
}

int64_t poisonBaseDamage(const int32_t spellPower, const int schoolRankCoefficientPercent)
{
	if(spellPower < 0 || schoolRankCoefficientPercent < 0 || schoolRankCoefficientPercent > 1000)
		throw std::invalid_argument("Invalid Poison formula inputs");
	return 20 + static_cast<int64_t>(spellPower) * schoolRankCoefficientPercent / 200;
}

int64_t poisonBaseDamageBasisPoints(const int32_t spellPower, const int coefficientBasisPoints,
	const int empowerSpellBonusPercent)
{
	if(spellPower < 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100'000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000)
		throw std::invalid_argument("Invalid Poison basis-point inputs");
	return 20 + static_cast<int64_t>(spellPower) * coefficientBasisPoints
		* (100 + empowerSpellBonusPercent) / 2'000'000;
}

std::vector<SpellID> cureAfflictions(const JsonNode & rules, const battle::Unit * unit)
{
	std::vector<SpellID> result;
	if(!unit || !cureEnabled(rules, SpellID(SpellID::CURE)))
		return result;

	for(const auto & savedIdentity : rules["spells"]["core:cure"]["cureAfflictions"].Vector())
	{
		if(!savedIdentity.isString())
			continue; // Rules are validated on load; fail closed for transient callers.
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", savedIdentity.String());
		if(!id)
			continue;
		const SpellID affliction(*id);
		if(!affliction.toSpell())
			continue;
		if(affliction != SpellID::POISON && affliction != SpellID::DISEASE)
			continue;
		const auto * stack = dynamic_cast<const CStack *>(unit);
		const bool hasPhysicalPoison = affliction == SpellID::POISON && stack
			&& stack->physicalPoisonActivationsRemaining > 0;
		if(hasPhysicalPoison || unit->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(affliction))))
			result.push_back(affliction);
	}
	std::sort(result.begin(), result.end(), [](const SpellID & lhs, const SpellID & rhs)
	{
		return lhs.getNum() < rhs.getNum();
	});
	result.erase(std::unique(result.begin(), result.end()), result.end());
	return result;
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
	int32_t spellPower, int32_t divisor, int overcharge, MagicArrowOverchargeModifiers modifiers,
	int coefficientPercent, int empowerSpellBonusPercent)
{
	if(!magicArrowOverchargeEnabled(rules, spell))
		return std::nullopt;
	if(spellPower < 0 || divisor <= 0 || coefficientPercent < 0 || coefficientPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000)
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
		? savedFormula->evaluateBasisPoints(spellPower, divisor, coefficientPercent * 100,
			empowerSpellBonusPercent)
		: DirectDamageFormula{20, 20}.evaluateBasisPoints(spellPower, divisor, coefficientPercent * 100,
			empowerSpellBonusPercent);
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

SpellID adventureSpellForGuildLevel(const JsonNode & rules, int guildLevel)
{
	if(!adventureSpellRulesActive(rules) || guildLevel < 1 || guildLevel > static_cast<int>(ADVENTURE_SPELLS.size())
		|| !LIBRARY || !LIBRARY->identifiers())
		return SpellID::NONE;

	const auto expected = std::find_if(ADVENTURE_SPELLS.begin(), ADVENTURE_SPELLS.end(), [guildLevel](const auto & definition)
	{
		return definition.guildLevel == guildLevel;
	});
	if(expected == ADVENTURE_SPELLS.end())
		return SpellID::NONE;

	const auto & savedEntry = rules["adventureSpells"][std::string(expected->identity)];
	if(!savedEntry.isStruct() || savedEntry["guildLevel"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| !integer(savedEntry["guildLevel"], expected->guildLevel, expected->guildLevel))
		return SpellID::NONE;

	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "spell", std::string(expected->identity), true);
	if(!id || *id < 0)
		return SpellID::NONE;
	const SpellID spell(*id);
	const auto tier = adventureSpellGuildLevel(rules, spell);
	if(!tier || *tier != guildLevel)
		return SpellID::NONE;
	return spell;
}

std::optional<int> adventureSpellGuildLevel(const JsonNode & rules, SpellID spell)
{
	const auto id = spell.getNum();
	if(!adventureSpellRulesActive(rules) || id < 0 || !LIBRARY || !LIBRARY->spellh
		|| static_cast<size_t>(id) >= LIBRARY->spellh->objects.size() || !LIBRARY->spellh->objects.at(id))
		return std::nullopt;
	const auto * spellDefinition = spell.toSpell();
	if(!spellDefinition || !spellDefinition->isCommonHeroSpell() || !spellDefinition->isAdventure())
		return std::nullopt;

	const auto * expected = adventureSpellDefinition(spellDefinition->getJsonKey());
	if(!expected)
		return std::nullopt;
	const auto & savedEntry = rules["adventureSpells"][std::string(expected->identity)];
	if(!savedEntry.isStruct() || savedEntry["guildLevel"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| !integer(savedEntry["guildLevel"], expected->guildLevel, expected->guildLevel))
		return std::nullopt;
	return expected->guildLevel;
}

ResourceSet adventureSpellUnlockCost(const JsonNode & rules, SpellID spell)
{
	require(adventureSpellGuildLevel(rules, spell).has_value(), "unlock cost requested for unavailable adventure spell");
	const auto & unlockCost = rules["adventureSpells"][spell.toSpell()->getJsonKey()]["unlockCost"];
	require(unlockCost.isStruct(), "adventure spell unlock cost is missing or not an object");
	fields(unlockCost, {"gold", "mercury", "sulfur", "crystal", "gems"});
	for(const auto * resource : {"gold", "mercury", "sulfur", "crystal", "gems"})
		require(unlockCost[resource].getType() == JsonNode::JsonType::DATA_INTEGER
			&& integer(unlockCost[resource], 0, 1000000), "adventure spell unlock cost");

	ResourceSet result;
	result[EGameResID::GOLD] = unlockCost["gold"].Integer();
	result[EGameResID::MERCURY] = unlockCost["mercury"].Integer();
	result[EGameResID::SULFUR] = unlockCost["sulfur"].Integer();
	result[EGameResID::CRYSTAL] = unlockCost["crystal"].Integer();
	result[EGameResID::GEMS] = unlockCost["gems"].Integer();
	return result;
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
