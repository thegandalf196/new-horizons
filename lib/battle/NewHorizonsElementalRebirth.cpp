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

#include "../CStack.h"
#include "CBattleInfoCallback.h"
#include "../GameLibrary.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
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
constexpr int GREATER_ESSENCE_HEALTH_PERCENTAGE_POINTS = 15;
constexpr int ELEMENTAL_WARD_REDUCTION_BASIS_POINTS = 2000;

const std::array<CreatureID, 5> & canonicalElementals()
{
	static const std::array<CreatureID, 5> ids = {
		CreatureID(CreatureID::decode("core:airElemental")),
		CreatureID(CreatureID::decode("core:waterElemental")),
		CreatureID(CreatureID::decode("core:fireElemental")),
		CreatureID(CreatureID::decode("core:earthElemental")),
		CreatureID(CreatureID::decode("core:magicElemental"))
	};
	return ids;
}

bool isNormalSlot(const battle::Unit & unit)
{
	return unit.unitSlot().validSlot();
}

bool isValidProfile(const ActiveProfile & profile)
{
	return profile.rank >= 1 && profile.rank <= 3
		&& profile.healthPercent == HEALTH_PERCENTAGES[static_cast<size_t>(profile.rank - 1)]
		&& (profile.rank >= 2 || (!profile.greaterEssence && !profile.elementalWard));
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
		hero->hasActivePerk(std::string(SKILL_ID), std::string(ELEMENTAL_WARD_ID))};
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

std::optional<DeathSnapshot> captureDeathSource(const battle::Unit & unit, const CGHeroInstance * hero)
{
	if(!isEligibleSource(unit))
		return std::nullopt;
	const auto profile = activeProfile(hero);
	const auto basis = unit.getBattleStartMaximumAggregateHP();
	if(!profile || basis <= 0)
		return std::nullopt;

	return DeathSnapshot{unit.unitId(), unit.unitSide(), unit.getPosition(), basis, *profile};
}

bool stillEligibleDeath(const battle::Unit * postHitUnit, const DeathSnapshot & snapshot,
	bool hitKilled, bool cloneKilled, bool nativeRebirth)
{
	return hitKilled && !cloneKilled && !nativeRebirth
		&& postHitUnit
		&& postHitUnit->unitId() == snapshot.unitId
		&& postHitUnit->unitSide() == snapshot.side
		&& postHitUnit->getPosition() == snapshot.corpsePosition
		&& !postHitUnit->alive()
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
		&& snapshot.battleStartMaximumAggregateHP > 0
		&& isValidProfile(snapshot.profile);
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

int64_t targetHP(const DeathSnapshot & snapshot)
{
	if(snapshot.battleStartMaximumAggregateHP <= 0
		|| !isValidProfile(snapshot.profile))
		return 0;

	const auto basis = snapshot.battleStartMaximumAggregateHP;
	const auto percent = snapshot.profile.healthPercent
		+ (snapshot.profile.greaterEssence ? GREATER_ESSENCE_HEALTH_PERCENTAGE_POINTS : 0);
	const auto scaled = (basis / 100) * percent + ((basis % 100) * percent) / 100;
	return std::max<int64_t>(1, scaled);
}

int32_t effectiveSummonMaxHP(const CArmedInstance * sourceArmy,
	CreatureID creatureId, PlayerColor owner, BattleSide side)
{
	if(!creatureId.hasValue())
		return 0;
	const auto * creature = creatureId.toCreature();
	if(!creature || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
		return 0;

	CStackBasicDescriptor descriptor(creature, 1);
	CStack hypothetical(&descriptor, owner, -1, side, SlotID::SUMMONED_SLOT_PLACEHOLDER, true);
	hypothetical.summoned = true;
	hypothetical.natureSummoned = false;
	if(sourceArmy)
		hypothetical.attachToSource(*sourceArmy);
	hypothetical.attachToSource(*creature);
	const auto value = hypothetical.valOfBonuses(BonusType::STACK_HEALTH);
	if(value > std::numeric_limits<int32_t>::max())
		return 0;
	return static_cast<int32_t>(std::max<int64_t>(1, value));
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
	BattleSide side, BattleHex corpsePosition, int64_t targetAggregateHP, int32_t effectiveCreatureMaxHP)
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
	result.health = *health;
	return result;
}
}
