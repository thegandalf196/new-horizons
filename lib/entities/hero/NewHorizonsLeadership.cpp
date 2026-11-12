/*
 * NewHorizonsLeadership.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../battle/NewHorizonsCommandResources.h"
#include "NewHorizonsLeadership.h"
#include "HeroStartingProjection.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "../../battle/NewHorizonsElementalAttunement.h"
#include "../../CCreatureHandler.h"
#include "../../GameLibrary.h"
#include "../../entities/creature/NewHorizonsCreatureCategoryRules.h"

#include "../../mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace newHorizonsHeroes
{
namespace
{
CreatureID findCreatureLine(CreatureID creature)
{
	const auto * cre = creature.toCreature();
	if(!cre || !LIBRARY || !LIBRARY->creatures())
		return creature;
	const CCreature * current = cre;
	bool found = true;
	while(found)
	{
		found = false;
		LIBRARY->creatures()->forEach([&current, &found](const Creature * candidate, bool & stop)
		{
			const auto * cc = dynamic_cast<const CCreature *>(candidate);
			if(cc && cc->upgrades.contains(current->getId()))
			{
				current = cc;
				found = true;
				stop = true;
			}
		});
	}
	return current->getId();
}

struct ArmyCompositionSnapshot
{
	bool regimentalOrganization = false;
	bool combinedArms = false;
};

ArmyCompositionSnapshot analyzeArmyComposition(const CGHeroInstance & hero, const CCreatureSet * armyContext)
{
	const CCreatureSet * army = armyContext ? armyContext : hero.getArmy();
	if(!army)
		return {};

	std::set<CreatureID> lines;
	bool hasRepeatedLine = false;
	bool hasCore = false;
	bool hasElite = false;
	bool hasChampion = false;

	for(const auto & [slot, stack] : army->Slots())
	{
		if(!stack || stack->getCount() <= 0 || !stack->getCreatureID().hasValue())
			continue;

		const CreatureID cid = stack->getCreatureID();
		const CreatureID line = findCreatureLine(cid);
		if(!lines.insert(line).second)
			hasRepeatedLine = true;

		const auto traits = hero.getCommandCreatureTraits(cid);
		if(traits.category)
		{
			if(*traits.category == newHorizonsCreatures::CreatureCategory::CORE)
				hasCore = true;
			else if(*traits.category == newHorizonsCreatures::CreatureCategory::ELITE)
				hasElite = true;
			else if(*traits.category == newHorizonsCreatures::CreatureCategory::CHAMPION)
				hasChampion = true;
		}
	}

	ArmyCompositionSnapshot snapshot;
	snapshot.regimentalOrganization = (!hasRepeatedLine && lines.size() >= 3);
	snapshot.combinedArms = (hasCore && hasElite && hasChampion);
	return snapshot;
}
}
LeadershipCapacity leadershipCapacity(int classBase, int classPerLevel,
	int level, int skillBonusPercent, uint64_t used, int minimumMovementPercent)
{
	constexpr int maximumProfileValue = 1000000;
	constexpr int maximumSkillBonusPercent = 1000;
	if(classBase <= 0 || classBase > maximumProfileValue
		|| classPerLevel < 0 || classPerLevel > maximumProfileValue
		|| level <= 0 || level > maximumProfileValue
		|| skillBonusPercent < 0 || skillBonusPercent > maximumSkillBonusPercent
		|| minimumMovementPercent <= 0 || minimumMovementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership capacity inputs");

	const int64_t base = classBase + int64_t(level - 1) * classPerLevel;
	const int64_t capacity = base * (100 + skillBonusPercent) / 100;
	int movementPercent = 100;
	if(used > static_cast<uint64_t>(capacity))
		movementPercent = std::max(minimumMovementPercent,
			static_cast<int>(static_cast<uint64_t>(capacity) * 100 / used));
	return {capacity, used, movementPercent};
}

int leadershipMovement(int unscaledMovement, int movementPercent)
{
	if(unscaledMovement < 0 || movementPercent <= 0 || movementPercent > 100)
		throw std::invalid_argument("Invalid New Horizons leadership movement inputs");
	return static_cast<int>(int64_t(unscaledMovement) * movementPercent / 100);
}

int64_t stackEffectiveLeadership(const CGHeroInstance & hero, CreatureID creature,
	const CCreatureSet * armyContext)
{
	const auto & rules = hero.getCapabilityRules();
	const int baseLeadership = capabilityLeadershipRating(rules, hero.level);
	const int trainedLeadership = std::max(0, hero.valOfBonuses(BonusType::LEADERSHIP));
	const int64_t L_hero = static_cast<int64_t>(baseLeadership) + trainedLeadership;

	if(L_hero <= 0)
		return 0;

	// Authority rank bonus
	const int authorityRank = hero.getPerkSkillRank("new-horizons:authority");
	double A = 0.0;
	if(authorityRank == 1) A = 0.15;
	else if(authorityRank == 2) A = 0.30;
	else if(authorityRank >= 3) A = 0.45;

	// Authority perks
	double P_B = 0.0;
	double P_A = 0.0;
	double P_E = 0.0;

	const auto traits = hero.getCommandCreatureTraits(creature);
	const auto * cre = creature.toCreature();

	// Basic perks
	if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.massedRanks"))
	{
		if(traits.category && *traits.category == newHorizonsCreatures::CreatureCategory::CORE)
			P_B = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.veteranCadre"))
	{
		if(traits.category && (*traits.category == newHorizonsCreatures::CreatureCategory::ELITE
			|| *traits.category == newHorizonsCreatures::CreatureCategory::CHAMPION))
			P_B = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.familiarStandards"))
	{
		if(cre && cre->getFactionID() == hero.getFactionID())
			P_B = 0.20;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.foreignLegion"))
	{
		if(cre && cre->getFactionID() != hero.getFactionID())
			P_B = 0.30;
	}

	// Advanced perks
	if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.officerCorps"))
	{
		if(cre && !cre->hasBonusOfType(BonusType::SHOOTER))
			P_A = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.missileCommand"))
	{
		if(cre && cre->hasBonusOfType(BonusType::SHOOTER))
			P_A = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.regimentalOrganization"))
	{
		const auto comp = analyzeArmyComposition(hero, armyContext);
		if(comp.regimentalOrganization)
			P_A = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.combinedArms"))
	{
		const auto comp = analyzeArmyComposition(hero, armyContext);
		if(comp.combinedArms)
			P_A = 0.35;
	}

	// Expert perks
	if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.grandMarshal"))
	{
		P_E = 0.25;
	}
	else if(hero.hasActivePerk("new-horizons:authority", "new-horizons:authority.masterOfLegions"))
	{
		if(traits.category && *traits.category == newHorizonsCreatures::CreatureCategory::CORE)
			P_E = 0.50;
	}

	const double totalMultiplier = 1.0 + A + P_B + P_A + P_E;
	return static_cast<int64_t>(std::floor(static_cast<double>(L_hero) * totalMultiplier));
}

int64_t stackEffectiveRequirement(const CGHeroInstance * hero, CreatureID creature)
{
	if(!creature.hasValue() || !creature.toCreature())
		return 1;

	const JsonNode & rules = hero ? hero->getCapabilityRules() : JsonNode();
	int baseRequirement = capabilityCreatureLeadershipRequirement(rules, creature);
	if(baseRequirement <= 0)
		baseRequirement = 50;

	if(!hero)
		return baseRequirement;

	if(newHorizonsNecromancy::isSkeletonFamily(creature))
	{
		if(hero->hasActivePerk("new-horizons:necromancy", "new-horizons:necromancy.lordOfTheDead"))
		{
			return std::max<int64_t>(1, static_cast<int64_t>(std::ceil(baseRequirement * 0.10)));
		}
		const int necroRank = hero->getPerkSkillRank("new-horizons:necromancy");
		double discount = 0.0;
		if(necroRank == 1) discount = 0.50;
		else if(necroRank == 2) discount = 0.60;
		else if(necroRank >= 3) discount = 0.70;

		if(discount > 0.0)
		{
			return std::max<int64_t>(1, static_cast<int64_t>(std::ceil(baseRequirement * (1.0 - discount))));
		}
	}

	return baseRequirement;
}

int64_t computeFullEfficiencyCapacity(int64_t effectiveLeadership, int64_t effectiveRequirement)
{
	if(effectiveRequirement <= 0)
		return std::max<int64_t>(1, effectiveLeadership);
	return std::max<int64_t>(1, effectiveLeadership / effectiveRequirement);
}

int64_t stackFullEfficiencyCapacity(const CGHeroInstance * hero, CreatureID creature,
	const CCreatureSet * armyContext)
{
	if(!hero)
		return std::numeric_limits<int64_t>::max();

	const int64_t effectiveLeadership = stackEffectiveLeadership(*hero, creature, armyContext);
	const int64_t effectiveRequirement = stackEffectiveRequirement(hero, creature);

	return computeFullEfficiencyCapacity(effectiveLeadership, effectiveRequirement);
}

double squareRootLeadershipEfficiency(int64_t count, int64_t capacity)
{
	if(count <= 0)
		return 1.0;
	const int64_t cap = std::max<int64_t>(1, capacity);
	if(count <= cap)
		return 1.0;

	const double c = static_cast<double>(cap);
	const double n = static_cast<double>(count);
	const double effectiveCount = c + (n - c) * std::sqrt(c / n);
	return effectiveCount / n;
}

double stackLeadershipEfficiency(const CGHeroInstance * hero, CreatureID creature,
	int64_t count, const CCreatureSet * armyContext)
{
	if(!hero || count <= 0)
		return 1.0;

	const int64_t capacity = stackFullEfficiencyCapacity(hero, creature, armyContext);
	return squareRootLeadershipEfficiency(count, capacity);
}

std::optional<LeadershipSlotCapacity> heroLeadershipSlotCapacity(
	const CGHeroInstance & hero, CreatureID creature)
{
	const auto & rules = hero.getCapabilityRules();
	if(!usesRules(rules) || (rules["rulesetVersion"].Integer() < 2 && rules["rulesetVersion"].Integer() != 6))
		return std::nullopt;

	const int64_t effectiveLeadership = stackEffectiveLeadership(hero, creature, hero.getArmy());
	const int64_t effectiveRequirement = stackEffectiveRequirement(&hero, creature);
	const int64_t maximum = std::max<int64_t>(1, effectiveLeadership / effectiveRequirement);

	auto result = capabilityLeadershipSlot(rules, hero.level, creature);
	if(result)
	{
		result->leadership = static_cast<int>(effectiveLeadership);
		result->requirement = static_cast<int>(effectiveRequirement);
		result->maximum = static_cast<int>(maximum);
		newHorizonsNecromancy::applyLeadershipPolicy(*result, hero, creature);
		newHorizonsElementalAttunement::applyLeadershipPolicy(*result, hero, creature);
	}
	return result;
}

bool canMergeArmies(const CArmedInstance * source, const CArmedInstance * destination)
{
	return canMergeArmies(source, destination, dynamic_cast<const CGHeroInstance *>(destination));
}

bool canMergeArmies(const CCreatureSet * source, const CCreatureSet * destination,
	const CGHeroInstance * hero)
{
	if(!source || !destination || source == destination)
		return false;
	if(hero && hero->getArmyCommand())
		return planCommandArmyAddition(*hero, *destination, *source).has_value();

	// Keep the ordinary slot/merge rule identical to garrisonSwap/moveArmy.
	// Leadership no longer enforces a hard creature cap.
	if(!destination->canBeMergedWith(*source, true))
		return false;

	const auto acceptsProjectedStack = [](CreatureID creature, int64_t count)
	{
		return count >= 0 && count <= std::numeric_limits<TQuantity>::max();
	};

	struct ProjectedSlot
	{
		CreatureID creature;
		int64_t count = 0;
		bool occupied = false;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> projected;

	for(const auto & [slot, stack] : destination->Slots())
		projected[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), true};

	for(const auto & [sourceSlot, sourceStack] : source->Slots())
	{
		int target = -1;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		{
			if(projected[i].occupied && projected[i].creature == sourceStack->getCreatureID())
			{
				target = i;
				break;
			}
		}

		if(target < 0)
		{
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			{
				if(!projected[i].occupied)
				{
					target = i;
					break;
				}
			}
		}

		if(target < 0)
		{
			// moveArmy frees one occupied destination slot by merging another
			// pair, then uses that slot for this source stack. Match that
			// deterministic fallback rather than merely counting creature types.
			int mergeSource = -1;
			int mergeDestination = -1;
			const int preferred = sourceSlot.getNum();
			if(sourceSlot.validSlot() && projected[preferred].occupied)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(j != preferred && projected[j].occupied
						&& projected[j].creature == projected[preferred].creature)
					{
						mergeSource = preferred;
						mergeDestination = j;
						break;
					}
			for(int i = 0; i < GameConstants::ARMY_SIZE && mergeSource < 0; ++i)
				for(int j = 0; j < GameConstants::ARMY_SIZE; ++j)
					if(i != j && projected[i].occupied && projected[j].occupied
						&& projected[i].creature == projected[j].creature)
					{
						mergeSource = i;
						mergeDestination = j;
						break;
					}

			if(mergeSource >= 0)
			{
				projected[mergeDestination].count += projected[mergeSource].count;
				if(!acceptsProjectedStack(projected[mergeDestination].creature,
					projected[mergeDestination].count))
					return false;
				projected[mergeSource] = {};
				target = mergeSource;
			}
		}

		if(target < 0)
			return false;
		if(!projected[target].occupied)
			projected[target] = {sourceStack->getCreatureID(), 0, true};
		projected[target].count += sourceStack->getCount();
		if(!acceptsProjectedStack(projected[target].creature, projected[target].count))
			return false;
	}

	return true;
}

SlotID recruitmentSlot(const CArmedInstance * army, CreatureID creature, int64_t requestedAmount)
{
	if(!army || !creature.toCreature())
		return SlotID();

	const auto * hero = dynamic_cast<const CGHeroInstance *>(army);
	if(hero && hero->getArmyCommand())
		return commandRecruitmentSlot(*hero, commandArmySlots(*army), creature, requestedAmount);
	const auto capacity = hero && requestedAmount > 0
		? hero->getLeadershipSlotCapacity(creature) : std::nullopt;
	const std::optional<int64_t> maximum = capacity
		? std::optional<int64_t>(capacity->maximum) : std::nullopt;
	std::vector<RecruitmentSlot> slots;
	slots.reserve(GameConstants::ARMY_SIZE);
	for(const auto & [slot, stack] : army->Slots())
		slots.push_back({slot, stack->getCreatureID(), stack->getCount()});
	return recruitmentSlot(slots, creature, requestedAmount, maximum);
}

SlotID recruitmentSlot(std::span<const RecruitmentSlot> slots, CreatureID creature,
	int64_t requestedAmount, std::optional<int64_t> maximum)
{
	if(!creature.toCreature())
		return SlotID();

	std::array<const RecruitmentSlot *, GameConstants::ARMY_SIZE> occupied{};
	SlotID legacySlot;
	for(const auto & entry : slots)
	{
		if(!entry.slot.validSlot() || occupied[entry.slot.getNum()])
			throw std::invalid_argument("Recruitment slots must be valid and unique");
		occupied[entry.slot.getNum()] = &entry;
		if(entry.creature == creature && (!legacySlot.validSlot() || entry.slot < legacySlot))
			legacySlot = entry.slot;
	}
	for(int i = 0; i < GameConstants::ARMY_SIZE && !legacySlot.validSlot(); ++i)
		if(!occupied[i])
			legacySlot = SlotID(i);
	if(!maximum || requestedAmount <= 0)
		return legacySlot;

	const int64_t maximumStackCount = std::max<int64_t>(0, *maximum);
	SlotID bestSlot;
	int64_t bestHeadroom = -1;

	// Matching stacks take priority for a whole request. Match getCreatureSlots'
	// exclusion of unresolved/nonpositive counts, retaining physical slot order.
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		const auto * entry = occupied[i];
		if(!entry || entry->creature != creature || entry->count < 1)
			continue;
		const int64_t headroom = std::max<int64_t>(0, maximumStackCount - entry->count);
		if(headroom >= requestedAmount)
			return SlotID(i);
		if(headroom > bestHeadroom)
		{
			bestSlot = SlotID(i);
			bestHeadroom = headroom;
		}
	}

	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(occupied[i])
			continue;
		if(maximumStackCount >= requestedAmount)
			return SlotID(i);
		if(maximumStackCount > bestHeadroom)
		{
			bestSlot = SlotID(i);
			bestHeadroom = maximumStackCount;
		}
	}

	// Retain a full matching fallback so authoritative validation rejects it.
	return bestHeadroom >= 0 ? bestSlot : legacySlot;
}
}
