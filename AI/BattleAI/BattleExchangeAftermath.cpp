/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "BattleExchangeVariant.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsPlague.h"
void BattleExchangeVariant::accountForNewElementalRebirthSpawns(
	const std::set<uint32_t> & idsBefore, const battle::Unit * referenceActor,
	const bool referenceActorIsOurs, DamageCache & damageCache,
	const std::shared_ptr<HypotheticBattle> & hb)
{
	if(!referenceActor)
		return;

	for(const auto unitId : hb->getElementalRebirthSpawnUnitIds())
	{
		if(idsBefore.contains(unitId) || !scoredElementalRebirthSpawnUnitIds.insert(unitId).second)
			continue;

		const auto * spawned = hb->battleGetUnitByID(unitId);
		if(!spawned || !spawned->alive() || !spawned->unitType() || spawned->getAvailableHealth() <= 0)
			continue;

		const auto enemies = hb->battleGetUnitsIf([&](const battle::Unit * candidate)
		{
			return candidate && candidate->alive() && candidate->unitType()
				&& candidate->getPosition().isValid()
				&& !hb->battleMatchOwner(spawned, candidate);
		});
		if(enemies.empty())
			continue;

		const auto fullHealth = static_cast<int64_t>(spawned->getCount()) * spawned->getMaxHealth();
		if(fullHealth <= 0)
			continue;

		// Keep this in the exchange's damage-reduction/DPS units: scale one ordinary
		// projected attack by the exact surviving HP fraction. This is a bounded
		// representative-target approximation, not a full initiative-branch forecast.
		const auto projectedDamage = std::max<int64_t>(0,
			damageCache.getOriginalDamage(spawned, enemies.front(), hb));
		const float spawnValue = static_cast<float>(projectedDamage)
			* static_cast<float>(spawned->getAvailableHealth()) / static_cast<float>(fullHealth);
		const bool sameSideAsReference = spawned->unitSide() == referenceActor->unitSide();
		const bool spawnIsOurs = referenceActorIsOurs ? sameSideAsReference : !sameSideAsReference;
		if(spawnIsOurs)
			dpsScore.enemyDamageReduce += spawnValue;
		else
			dpsScore.ourDamageReduce += spawnValue;
	}
}

void BattleExchangeVariant::accountForNewPrimalBurstHits(const size_t firstNewHit,
	const PlayerColor referenceController, const bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto & hits = hb->getProjectedPrimalBurstHits();
	for(size_t index = firstNewHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		if(!hit.preHitTarget || hit.actualDamage <= 0)
			continue;
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.actualDamage), damageCache, hb);
		const bool targetSharesReferenceController = hit.targetController == referenceController;
		const bool targetIsOurs = targetSharesReferenceController == referenceActorIsOurs;
		if(targetIsOurs)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	}
}

void BattleExchangeVariant::completePlagueActivation(uint32_t unitId, PlayerColor referenceController,
	bool referenceActorIsOurs, DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & hb)
{
	const auto firstHit = hb->getProjectedPlagueHits().size();
	const auto rebirthBefore = hb->getElementalRebirthSpawnUnitIds();
	const auto firstPrimalBurstHit = hb->getProjectedPrimalBurstHits().size();
	const auto actor = hb->getForUpdate(unitId);
	newHorizonsPlague::completeActivation(*hb, *hb->getServerCallback(), unitId);
	const auto & hits = hb->getProjectedPlagueHits();
	for(size_t index = firstHit; index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.preHitTarget.get(),
			static_cast<uint64_t>(hit.finalizedDamage), damageCache, hb);
		const bool targetIsOurs = (hit.targetController == referenceController) == referenceActorIsOurs;
		if(targetIsOurs)
			dpsScore.ourDamageReduce += value;
		else
			dpsScore.enemyDamageReduce += value;
	}
	accountForNewElementalRebirthSpawns(rebirthBefore, actor.get(), referenceActorIsOurs, damageCache, hb);
	accountForNewPrimalBurstHits(firstPrimalBurstHit, referenceController, referenceActorIsOurs, damageCache, hb);
}

void BattleExchangeVariant::completeFrozenActivation(const std::shared_ptr<StackWithBonuses> & attacker, bool referenceActorIsOurs,
	DamageCache & damageCache, const std::shared_ptr<HypotheticBattle> & exchangeBattle)
{
	const auto frozen = newHorizonsFrozen::removalPlan(*attacker);
	attacker->removeUnitBonus(CSelector([](const Bonus * bonus)
	{
		return Bonus::UntilActivationEnds(bonus) && !newHorizonsOffense::isNoQuarterMoralePenalty(bonus);
	}));
	attacker->consumeNoQuarterActivation();
	completePlagueActivation(attacker->unitId(), exchangeBattle->battleGetOwner(attacker.get()),
		referenceActorIsOurs, damageCache, exchangeBattle);
	const auto current = exchangeBattle->getForUpdate(attacker->unitId());
	if(current->alive() && !current->isGhost() && !current->isTimeStopped()
		&& newHorizonsFrozen::isFrozen(*current))
		current->removeUnitBonus(frozen);
}
