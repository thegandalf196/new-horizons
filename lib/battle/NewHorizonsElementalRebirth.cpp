/*
 * NewHorizonsElementalRebirth.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsElementalRebirth.h"
#include "SummonHealthQuery.h"
#include "ElementalConsumptionContext.h"
#include "ElementalRebirthLineage.h"

#include "../CStack.h"
#include "../bonuses/BonusList.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "../GameLibrary.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../spells/NewHorizonsElementalTerrain.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <string>
#include <string_view>

namespace newHorizonsElementalRebirth
{
namespace
{
constexpr std::string_view SKILL_ID = "new-horizons:elementalRebirth";
constexpr std::array<std::string_view, 3> RANK_IDS = {"basic", "advanced", "expert"};
constexpr std::array<int, 3> HEALTH_PERCENTAGES = {25, 40, 50};
constexpr std::string_view PRIMAL_BURST_ID = "new-horizons:elementalRebirth.primalBurst";
constexpr std::string_view GREATER_ESSENCE_ID = "new-horizons:elementalRebirth.greaterEssence";
constexpr std::string_view ELEMENTAL_WARD_ID = "new-horizons:elementalRebirth.elementalWard";
constexpr std::string_view REBIRTH_CHAIN_ID = "new-horizons:elementalRebirth.rebirthChain";
constexpr std::string_view ELEMENTAL_ATTUNEMENT_ID = "new-horizons:elementalRebirth.elementalAttunement";
constexpr std::string_view ADAPTIVE_ELEMENT_ID = "new-horizons:elementalRebirth.adaptiveElement";
constexpr std::string_view PERFECT_CONVERGENCE_ID = "new-horizons:elementalRebirth.perfectConvergence";
constexpr std::string_view ELEMENTAL_MEMORY_ID = "new-horizons:elementalRebirth.elementalMemory";
constexpr std::string_view PHOENIX_SPARK_ID = "new-horizons:elementalRebirth.phoenixSpark";
constexpr int GREATER_ESSENCE_HEALTH_PERCENTAGE_POINTS = 15;
constexpr int ELEMENTAL_WARD_REDUCTION_BASIS_POINTS = 2000;

bool isNormalSlot(const battle::Unit & unit)
{
	return unit.unitSlot().validSlot();
}

bool isValidProfile(const ActiveProfile & profile)
{
	return profile.rank >= 1 && profile.rank <= 3
		&& profile.healthPercent == HEALTH_PERCENTAGES[static_cast<size_t>(profile.rank - 1)]
		&& (profile.rank >= 2 || (!profile.greaterEssence && !profile.elementalWard
			&& !profile.rebirthChain && !profile.adaptiveElement))
		&& (profile.rank >= 3 || (!profile.perfectConvergence && !profile.phoenixSpark));
}

DeathSnapshot withElementalMemory(DeathSnapshot snapshot, const battle::Unit & unit)
{
	if(snapshot.profile.elementalMemory)
	{
		// Copy current net modifiers before immunity/cap evaluation. In particular,
		// an Elemental's normal NO_MORALE must not be silently removed or bypassed.
		snapshot.positiveMoraleModifier = std::max(0, unit.valOfBonuses(BonusType::MORALE));
		snapshot.positiveLuckModifier = std::max(0, unit.valOfBonuses(BonusType::LUCK));
	}
	return snapshot;
}
}

std::optional<ActiveProfile> activeProfile(const CGHeroInstance * hero)
{
	if(!hero || hero->getFactionID() != FactionID::CONFLUX
		|| !newHorizonsHeroes::usesRules(hero->getPrimaryGrowthRules()))
		return std::nullopt;

	static const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL_ID)));
	if(skill.getNum() < 0)
		return std::nullopt;
	const auto factionSkill = newHorizonsHeroes::factionSkill(hero->getPrimaryGrowthRules(), FactionID::CONFLUX);
	if(!factionSkill || *factionSkill != skill)
		return std::nullopt;

	const int rank = hero->getSecSkillLevel(skill);
	if(rank < 1 || rank > 3)
		return std::nullopt;

	const auto & savedPerkRules = hero->getPerkState().rules;
	if(!newHorizonsHeroes::usesPerkRules(savedPerkRules))
		return std::nullopt;
	const auto definition = newHorizonsHeroes::perkSkill(savedPerkRules, SKILL_ID);
	if(!definition)
		return std::nullopt;

	const auto rankKey = RANK_IDS[static_cast<size_t>(rank - 1)];
	const auto & status = definition->ranks[std::string(rankKey)]["effect"]["status"];
	if(!status.isString() || status.String() != "active")
		return std::nullopt;

	return ActiveProfile{rank, HEALTH_PERCENTAGES[static_cast<size_t>(rank - 1)],
		hero->hasActivePerk(std::string(SKILL_ID), std::string(PRIMAL_BURST_ID)),
		hero->hasActivePerk(std::string(SKILL_ID), std::string(GREATER_ESSENCE_ID)),
		hero->hasActivePerk(std::string(SKILL_ID), std::string(ELEMENTAL_WARD_ID)),
		rank >= MasteryLevel::ADVANCED && hero->hasActivePerk(std::string(SKILL_ID), std::string(REBIRTH_CHAIN_ID)),
		hero->hasActivePerk(std::string(SKILL_ID), std::string(ELEMENTAL_ATTUNEMENT_ID)),
		rank >= MasteryLevel::ADVANCED && hero->hasActivePerk(std::string(SKILL_ID), std::string(ADAPTIVE_ELEMENT_ID)),
		rank >= MasteryLevel::EXPERT && hero->hasActivePerk(std::string(SKILL_ID), std::string(PERFECT_CONVERGENCE_ID)),
		hero->hasActivePerk(std::string(SKILL_ID), "new-horizons:elementalRebirth.swiftRebirth"),
		hero->hasActivePerk(std::string(SKILL_ID), std::string(ELEMENTAL_MEMORY_ID)),
		rank >= MasteryLevel::EXPERT && hero->hasActivePerk(std::string(SKILL_ID), std::string(PHOENIX_SPARK_ID))};
}

int64_t primalBurstDamageBudget(int64_t rebornAggregateHP)
{
	return rebornAggregateHP > 0 ? rebornAggregateHP / 10 : 0;
}

int64_t primalBurstShare(int64_t damageBudget, size_t hostileCount)
{
	if(damageBudget <= 0 || hostileCount == 0
		|| hostileCount > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
		return 0;
	return damageBudget / static_cast<int64_t>(hostileCount);
}

std::vector<uint32_t> adjacentHostileUnitIds(const CBattleInfoCallback & battle,
	const battle::Unit & center)
{
	std::set<uint32_t> hostileIds;
	for(const auto & hex : center.getSurroundingHexes())
	{
		if(!hex.isAvailable())
			continue;
		const auto * candidate = battle.battleGetUnitByPos(hex, true);
		if(!candidate || candidate->unitId() == center.unitId()
			|| !candidate->alive() || !candidate->isValidTarget(false)
			|| battle.battleGetOwner(&center) == battle.battleGetOwner(candidate))
			continue;
		hostileIds.insert(candidate->unitId());
	}
	return {hostileIds.begin(), hostileIds.end()};
}

std::optional<Bonus> elementalWardBonus(const ActiveProfile & profile)
{
	if(!isValidProfile(profile) || !profile.elementalWard)
		return std::nullopt;

	const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL_ID)));
	if(skill.getNum() < 0)
		return std::nullopt;

	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS,
		BonusSource::SECONDARY_SKILL, ELEMENTAL_WARD_REDUCTION_BASIS_POINTS,
		BonusSourceID(skill), BonusSubtypeID(SpellSchool::ANY));
	bonus.stacking = std::string(ELEMENTAL_WARD_ID);
	bonus.description.appendRawString("Elemental Ward: 20% magical damage reduction");
	return bonus;
}

bool isEligibleSource(const battle::Unit & unit)
{
	return unit.alive()
		&& !unit.isGhost()
		&& unit.getPosition().isValid()
		&& unit.unitType()
		&& isNormalSlot(unit)
		&& !unit.isTurret()
		&& !unit.isCatapult()
		&& !unit.isBallista()
		&& !unit.isFirstAidTent()
		&& !unit.isAmmoCart()
		&& !unit.hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& !unit.isSummoned()
		&& !unit.isClone()
		&& unit.getPhantomInitialIntegrity() == 0;
}

bool isElementalMemoryBonus(const Bonus & bonus)
{
	if((bonus.type != BonusType::MORALE && bonus.type != BonusType::LUCK)
		|| bonus.source != BonusSource::SECONDARY_SKILL || bonus.duration != BonusDuration::N_TURNS
		|| bonus.valType != BonusValueType::ADDITIVE_VALUE || bonus.val <= 0
		|| bonus.parameters || bonus.hasStatusMetadata())
		return false;

	const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL_ID)));
	const auto key = std::string(ELEMENTAL_MEMORY_ID)
		+ (bonus.type == BonusType::MORALE ? ".morale" : ".luck");
	return skill.getNum() >= 0 && bonus.sid == BonusSourceID(skill) && bonus.stacking == key;
}

std::vector<Bonus> elementalMemoryBonuses(const DeathSnapshot & snapshot, const battle::Unit & reborn)
{
	std::vector<Bonus> result;
	if(!isValidProfile(snapshot.profile) || !snapshot.profile.elementalMemory)
		return result;
	const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL_ID)));
	if(skill.getNum() < 0)
		return result;
	const auto add = [&](BonusType type, int32_t captured)
	{
		if(captured <= 0)
			return;
		// Count existing positive contributions separately: subtracting its net
		// modifier would compensate a new Elemental-specific negative effect.
		// Aggregation still uses the ordinary limiter/stacking-correct bonus view.
		const auto positive = reborn.getAllBonuses(Selector::type()(type).And(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->val > 0;
		})));
		const auto increment = static_cast<int64_t>(captured) - positive->totalValue();
		if(increment <= 0)
			return;
		Bonus bonus(BonusDuration::N_TURNS, type, BonusSource::SECONDARY_SKILL,
			static_cast<int32_t>(std::min<int64_t>(increment, std::numeric_limits<int32_t>::max())), BonusSourceID(skill));
		bonus.turnsRemain = 1;
		bonus.stacking = std::string(ELEMENTAL_MEMORY_ID) + (type == BonusType::MORALE ? ".morale" : ".luck");
		bonus.description.appendRawString("Elemental Memory: inherited positive modifier until round end");
		result.push_back(std::move(bonus));
	};
	add(BonusType::MORALE, snapshot.positiveMoraleModifier);
	add(BonusType::LUCK, snapshot.positiveLuckModifier);
	return result;
}

std::optional<DeathSnapshot> captureDeathSource(const battle::Unit & unit, const CGHeroInstance * hero,
	bool chainUsed, const newHorizonsCreatures::CreatureCategoryRules * categoryRules, bool phoenixSparkUsed)
{
	const auto profile = activeProfile(hero);
	if(!profile)
		return std::nullopt;
	if(!isEligibleSource(unit))
	{
		if(chainUsed || !profile->rebirthChain || !unit.alive() || unit.isGhost()
			|| !unit.getPosition().isValid() || !isFirstGenerationOutput(unit))
			return std::nullopt;
		return withElementalMemory(DeathSnapshot{unit.unitId(), unit.unitSide(), unit.getPosition(), 0, *profile,
			true, unit.getRebirthOriginalAggregateHP()}, unit);
	}
	const auto basis = unit.getBattleStartMaximumAggregateHP();
	if(basis <= 0)
		return std::nullopt;

	auto snapshot = withElementalMemory(DeathSnapshot{unit.unitId(), unit.unitSide(), unit.getPosition(), basis, *profile}, unit);
	const auto category = categoryRules ? categoryRules->lookup(unit.unitType()->getJsonKey()) : std::nullopt;
	snapshot.phoenixSpark = profile->phoenixSpark && !phoenixSparkUsed && !unit.isHypnotized()
		&& !unit.isTimeStopped() && category
		&& category->category == newHorizonsCreatures::CreatureCategory::CHAMPION;
	return snapshot;
}

void PhoenixSparkConsumption::validateShape() const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| sourceUnitId == std::numeric_limits<uint32_t>::max())
		throw std::runtime_error("Invalid Phoenix Spark death provenance");
}

void validatePhoenixSparkConsumption(const IBattleInfo & battle, const PhoenixSparkConsumption & consumption)
{
	LiveElementalConsumptionContext context(battle);
	validatePhoenixSparkConsumption(context, consumption);
}

bool stillEligibleDeath(const battle::Unit * postHitUnit, const DeathSnapshot & snapshot,
	bool hitKilled, bool cloneKilled, bool nativeRebirth, bool chainUsed)
{
	if(!hitKilled || cloneKilled || nativeRebirth || !postHitUnit
		|| postHitUnit->unitId() != snapshot.unitId || postHitUnit->unitSide() != snapshot.side
		|| postHitUnit->getPosition() != snapshot.corpsePosition || postHitUnit->alive()
		|| !isValidProfile(snapshot.profile))
		return false;
	if(snapshot.chain)
		return !chainUsed && snapshot.profile.rebirthChain && isFirstGenerationOutput(*postHitUnit)
			&& snapshot.rebirthOriginalAggregateHP == postHitUnit->getRebirthOriginalAggregateHP();
	return snapshot.rebirthOriginalAggregateHP == 0
		&& !postHitUnit->isSummoned()
		&& !postHitUnit->isClone()
		&& postHitUnit->getPhantomInitialIntegrity() == 0
		&& !postHitUnit->isTurret()
		&& !postHitUnit->isCatapult()
		&& !postHitUnit->isBallista()
		&& !postHitUnit->isFirstAidTent()
		&& !postHitUnit->isAmmoCart()
		&& !postHitUnit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		&& isNormalSlot(*postHitUnit)
		&& snapshot.battleStartMaximumAggregateHP > 0;
}

void ChainConsumption::validateShape() const
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| sourceUnitId == std::numeric_limits<uint32_t>::max()
		|| spawnUnitId == std::numeric_limits<uint32_t>::max() || sourceUnitId == spawnUnitId)
		throw std::runtime_error("Invalid Rebirth Chain consumption provenance");
}

void validateChainConsumption(const IBattleInfo & battle, const ChainConsumption & consumption,
	const battle::UnitInfo & spawn)
{
	LiveElementalConsumptionContext context(battle);
	validateChainConsumption(context, consumption, spawn);
}

void validateChainRollback(const IBattleInfo & battle, const ChainConsumption & consumption)
{
	LiveElementalConsumptionContext context(battle);
	validateChainRollback(context, consumption);
}

std::vector<CreatureID> legalCandidatePool(const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	const AccessibilityInfo & accessibility, BattleHex corpsePosition, BattleSide side)
{
	std::vector<CreatureID> result;
	if(!corpsePosition.isValid() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return result;

	for(const auto creatureId : canonicalElementals())
	{
		if(!creatureId.hasValue())
			continue;
		const auto * creature = creatureId.toCreature();
		if(!creature)
			continue;
		const auto category = categoryRules.lookup(creature->getJsonKey());
		if(!category || category->category != newHorizonsCreatures::CreatureCategory::ELITE)
			continue;
		if(!accessibility.accessible(corpsePosition, creature->isDoubleWide(), side))
			continue;
		result.push_back(creatureId);
	}
	return result;
}

std::vector<CreatureID> legalCandidatePool(const IBattleInfo & battle,
	const AccessibilityInfo & accessibility, const DeathSnapshot & snapshot)
{
	if(!isValidProfile(snapshot.profile))
		return {};
	if(snapshot.phoenixSpark)
	{
		const CreatureID phoenix(CreatureID::decode("core:phoenix"));
		const auto * creature = phoenix.toCreature();
		const auto category = creature ? battle.getCreatureCategoryRules().lookup(creature->getJsonKey()) : std::nullopt;
		if(snapshot.chain || !snapshot.profile.phoenixSpark || !category
			|| category->category != newHorizonsCreatures::CreatureCategory::CHAMPION
			|| !accessibility.accessible(snapshot.corpsePosition, creature->isDoubleWide(), snapshot.side))
			return {};
		return {phoenix};
	}
	auto candidates = legalCandidatePool(battle.getCreatureCategoryRules(), accessibility,
		snapshot.corpsePosition, snapshot.side);
	if(!snapshot.profile.adaptiveElement && !snapshot.profile.perfectConvergence)
		return candidates;
	// The authored terrain mapping currently names one appropriate result. Do
	// not invent secondary types or bypass footprint/category legality as fallback.
	const auto primary = newHorizonsElementalTerrain::primaryElemental(battle);
	if(!primary || std::find(candidates.begin(), candidates.end(), *primary) == candidates.end())
		return {};
	return {*primary};
}

int64_t targetHP(const DeathSnapshot & snapshot)
{
	if(snapshot.phoenixSpark)
		return !snapshot.chain && isValidProfile(snapshot.profile) && snapshot.profile.phoenixSpark
			&& snapshot.battleStartMaximumAggregateHP > 0
			? std::max<int64_t>(1, snapshot.battleStartMaximumAggregateHP / 4) : 0;
	if(snapshot.chain)
		return isValidProfile(snapshot.profile) && snapshot.profile.rebirthChain && snapshot.rebirthOriginalAggregateHP > 0
			? std::max<int64_t>(1, snapshot.rebirthOriginalAggregateHP / 4) : 0;
	if(snapshot.battleStartMaximumAggregateHP <= 0
		|| !isValidProfile(snapshot.profile))
		return 0;

	const auto basis = snapshot.battleStartMaximumAggregateHP;
	const auto percent = snapshot.profile.healthPercent
		+ (snapshot.profile.greaterEssence ? GREATER_ESSENCE_HEALTH_PERCENTAGE_POINTS : 0);
	const auto scaled = (basis / 100) * percent + ((basis % 100) * percent) / 100;
	return std::max<int64_t>(1, scaled);
}

int64_t targetHP(const DeathSnapshot & snapshot, CreatureID creature, const IBattleInfo & battle)
{
	return targetHP(snapshot, creature, LiveElementalConsumptionContext(battle));
}

std::optional<SpawnHealth> spawnHealth(int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP)
{
	if(targetAggregateHP <= 0 || effectiveCreatureMaxHP <= 0)
		return std::nullopt;

	const int64_t creatureMaxHP = effectiveCreatureMaxHP;
	const int64_t count = targetAggregateHP / creatureMaxHP
		+ (targetAggregateHP % creatureMaxHP == 0 ? 0 : 1);
	if(count <= 0 || count > std::numeric_limits<int32_t>::max()
		|| count > std::numeric_limits<int64_t>::max() / creatureMaxHP)
		return std::nullopt;

	const auto fullAggregateHP = count * creatureMaxHP;
	const auto damageFromFull = fullAggregateHP - targetAggregateHP;
	const auto firstCreatureHP = creatureMaxHP - damageFromFull;
	if(firstCreatureHP <= 0 || firstCreatureHP > creatureMaxHP)
		return std::nullopt;

	return SpawnHealth{static_cast<int32_t>(count), effectiveCreatureMaxHP,
		targetAggregateHP, fullAggregateHP, damageFromFull, static_cast<int32_t>(firstCreatureHP)};
}

std::optional<SpawnDescriptor> makeSpawnDescriptor(uint32_t unitId, CreatureID creature,
	BattleSide side, BattleHex corpsePosition, int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP,
	bool chainOutput)
{
	if(!creature.hasValue() || !creature.toCreature() || !corpsePosition.isValid()
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return std::nullopt;
	const auto health = spawnHealth(targetAggregateHP, effectiveCreatureMaxHP);
	if(!health)
		return std::nullopt;

	SpawnDescriptor result;
	result.unit.id = unitId;
	result.unit.count = health->count;
	result.unit.type = creature;
	result.unit.side = side;
	result.unit.position = corpsePosition;
	result.unit.summoned = true;
	result.unit.natureSummoned = false;
	result.unit.rebirthOriginalAggregateHP = chainOutput ? 0 : targetAggregateHP;
	result.health = *health;
	return result;
}
}
