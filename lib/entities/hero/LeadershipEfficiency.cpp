/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsLeadership.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "../creature/NewHorizonsCreatureCategoryRules.h"
#include "../../CCreatureHandler.h"
#include "../../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <limits>

namespace newHorizonsHeroes
{
namespace
{
constexpr auto AUTHORITY = "new-horizons:authority";
constexpr std::array<const char *, 10> PERKS = {"massedRanks", "veteranCadre", "familiarStandards",
	"foreignLegion", "officerCorps", "missileCommand", "regimentalOrganization", "combinedArms",
	"grandMarshal", "masterOfLegions"};

int64_t baseLeadership(const CGHeroInstance & hero, int64_t flat)
{
	const int64_t natural = std::max(0, hero.compiledLeadership()->rating(hero.level));
	if(flat > std::numeric_limits<int64_t>::max() - natural) return std::numeric_limits<int64_t>::max();
	return std::max<int64_t>(0, natural + flat);
}

int authorityPercent(const CGHeroInstance & hero, CreatureID creature,
	const LeadershipModifiers & modifiers, const LeadershipComposition & composition)
{
	const auto & policy = *hero.compiledLeadership();
	int result = policy.authorityRankBonus(modifiers.authorityRank);
	if(std::all_of(modifiers.authorityPerks.begin(), modifiers.authorityPerks.end(),
		[](const auto & perk) { return perk.empty(); })) return result;
	const auto * definition = creature.toCreature();
	const auto traits = hero.getCommandCreatureTraits(creature);
	const bool core = traits.category == newHorizonsCreatures::CreatureCategory::CORE;
	const bool higher = traits.category == newHorizonsCreatures::CreatureCategory::ELITE
		|| traits.category == newHorizonsCreatures::CreatureCategory::CHAMPION;
	const bool native = definition->getFactionID() == hero.getFactionID();
	const bool shooter = definition->hasBonusOfType(BonusType::SHOOTER);
	const std::array<bool, 10> eligible = {core, higher, native, !native, !shooter, shooter,
		composition.captured && composition.regimentalOrganization,
		composition.captured && composition.combinedArms, true, core};
	for(size_t index = 0; index < PERKS.size(); ++index)
	{
		const size_t tier = index < 4 ? 0 : (index < 8 ? 1 : 2);
		if(eligible[index] && modifiers.authorityPerks[tier] == PERKS[index])
			result += policy.authorityPerkBonus(PERKS[index]);
	}
	return result;
}

int64_t requirement(const CGHeroInstance & hero, CreatureID creature, const LeadershipModifiers & modifiers)
{
	const int base = hero.compiledLeadership()->requirement(creature);
	if(base <= 0) throw std::runtime_error("Missing captured creature Leadership Requirement");
	if(modifiers.necromancyRank <= 0 && !modifiers.lordOfTheDead) return base;
	if(!newHorizonsNecromancy::isSkeletonFamily(creature)) return base;
	const auto owner = hero.getPerkState().capturedOwner();
	const int discount = owner ? owner->leadershipDiscounts().discountPercent(
		modifiers.necromancyRank, modifiers.lordOfTheDead) : 0;
	return newHorizonsNecromancy::discountedLeadershipRequirement(base, discount);
}
}

bool usesLeadershipEfficiency(const JsonNode & rules)
{
	return rules.isStruct() && rules["rulesetVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["rulesetVersion"].Integer() >= 7
		&& rules["leadership"]["efficiencyPolicy"]["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["leadership"]["efficiencyPolicy"]["version"].Integer() == 1;
}

LeadershipModifiers captureLeadershipModifiers(const CGHeroInstance & hero)
{
	LeadershipModifiers result;
	result.authorityRank = hero.getPerkSkillRank(AUTHORITY);
	result.necromancyRank = hero.getPerkSkillRank("new-horizons:necromancy");
	result.lordOfTheDead = hero.hasActivePerk("new-horizons:necromancy", "new-horizons:necromancy.lordOfTheDead");
	for(const auto & selection : hero.getPerkState().getSelected())
	{
		if(selection.skillId != AUTHORITY || !hero.hasActivePerk(selection.skillId, selection.perkId)) continue;
		const std::string_view full = selection.perkId;
		constexpr std::string_view prefix = "new-horizons:authority.";
		if(!full.starts_with(prefix)) continue;
		const auto key = full.substr(prefix.size());
		for(size_t index = 0; index < PERKS.size(); ++index)
			if(key == PERKS[index]) result.authorityPerks[index < 4 ? 0 : (index < 8 ? 1 : 2)] = PERKS[index];
	}
	return result;
}

LeadershipHeroReadout leadershipHeroReadout(const CGHeroInstance & hero)
{
	LeadershipHeroReadout result;
	const auto * rules = hero.compiledLeadership();
	if(!rules || !rules->usesEfficiency()) return result;
	result.baseLeadership = baseLeadership(hero, hero.valOfBonuses(BonusType::LEADERSHIP));
	result.authorityBonusPercent = rules->authorityRankBonus(hero.getPerkSkillRank(AUTHORITY));
	result.effectiveLeadership = leadershipWithBonusPercent(result.baseLeadership, result.authorityBonusPercent);
	return result;
}

int64_t leadershipWithBonusPercent(int64_t base, int bonusPercent)
{
	if(base <= 0) return 0;
	if(bonusPercent < 0 || bonusPercent > 10000)
		throw std::invalid_argument("Invalid Leadership percentage bonus");
	const int64_t multiplier = 100 + bonusPercent;
	const int64_t whole = base / 100;
	const int64_t remainder = base % 100 * multiplier / 100;
	if(whole > (std::numeric_limits<int64_t>::max() - remainder) / multiplier)
		return std::numeric_limits<int64_t>::max();
	return whole * multiplier + remainder;
}

LeadershipStackReadout leadershipStackReadout(const CGHeroInstance * hero, CreatureID creature,
	int64_t count, const LeadershipComposition * composition, const CCreatureSet * armyContext,
	const LeadershipModifiers * projection)
{
	LeadershipStackReadout result;
	result.count = std::max<int64_t>(0, count);
	result.capacity = std::numeric_limits<int64_t>::max();
	if(!hero || !creature.hasValue() || !creature.toCreature()
		|| (!hero->compiledLeadership() || !hero->compiledLeadership()->usesEfficiency())) return result;
	if(creature == CreatureID(CreatureID::ARROW_TOWERS)
		|| creature.toCreature()->hasBonusOfType(BonusType::SIEGE_WEAPON)) return result;
	result.active = true;
	const auto modifiers = projection ? *projection : captureLeadershipModifiers(*hero);
	// A supplied battle snapshot, including explicit absence, never falls back to
	// live adventure composition. Adventure callers may inspect their current army.
	const auto captured = composition ? *composition : captureLeadershipComposition(*hero, armyContext);
	captured.validate();
	result.baseLeadership = baseLeadership(*hero,
		modifiers.flatLeadership ? *modifiers.flatLeadership : hero->valOfBonuses(BonusType::LEADERSHIP));
	result.effectiveLeadership = leadershipWithBonusPercent(result.baseLeadership,
		authorityPercent(*hero, creature, modifiers, captured));
	result.requirement = requirement(*hero, creature, modifiers);
	result.capacity = computeFullEfficiencyCapacity(result.effectiveLeadership, result.requirement);
	result.efficiency = squareRootLeadershipEfficiency(result.count, result.capacity);
	return result;
}
}
