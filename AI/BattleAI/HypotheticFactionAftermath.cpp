/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticFactionAftermath.h"
#include "HypotheticBattle.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

bool factionAftermathAI::needsProjection(const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(const auto * hero = battle.battleGetFightingHero(side); hero
			&& (hero->hasActivePerk("new-horizons:shroudOfMalassa", "new-horizons:shroudOfMalassa.exposedWeakness")
				|| hero->hasActivePerk("new-horizons:elementalRebirth", "new-horizons:elementalRebirth.elementalCataclysm")))
			return true;
	return false;
}

void HypotheticBattle::projectCataclysms(std::span<const newHorizonsFactionSpellDamage::Cataclysm> captured)
{
	for(const auto & explosion : captured)
	{
		const auto * corpse = battleGetUnitByID(explosion.source);
		if(!corpse || corpse->alive() || corpse->isClone() || corpse->unitSide() != explosion.side)
			continue;
		const auto share = newHorizonsFactionSpellDamage::cataclysmShare(
			explosion.maximumAggregateHP, explosion.targets.size());
		if(share <= 0) continue;
		StacksInjured packet;
		packet.battleID = getBattleID();
		std::vector<ProjectedPrimalBurstHit> observations;
		for(const auto id : explosion.targets)
		{
			const auto target = getForUpdate(id);
			if(!target || !target->alive() || !target->unitType()) continue;
			const auto damage = newHorizonsFactionSpellDamage::cataclysmDamage(*this, *target, share);
			if(damage <= 0) continue;
			BattleStackAttacked hit;
			hit.stackAttacked = id;
			hit.attackerID = explosion.source;
			hit.damageAmount = damage;
			auto prepared = target->acquireState();
			CStack::prepareAttacked(hit, *getServerCallback()->getRNG(), prepared,
				false, false, battle::DamageProvenance::SPELL);
			auto before = std::make_shared<StackWithBonuses>(this, target.get());
			before->freezeDamageScoringBonusSnapshot();
			observations.push_back({std::move(before), battleGetOwner(target.get()),
				std::max<int64_t>(0, target->getAvailableHealth() - prepared->getAvailableHealth()), true});
			packet.stacks.push_back(std::move(hit));
		}
		if(packet.stacks.empty()) continue;
		// The existing deterministic injury visitor owns damage, nested deaths and
		// Rebirth. No second damage evaluator or authoritative RNG is introduced.
		getServerCallback()->apply(packet);
		for(auto & observation : observations)
			if(observation.actualDamage > 0)
				projectedPrimalBurstHits.push_back(std::move(observation));
	}
}

void factionAftermathAI::restoreCataclysmRecipients(HypotheticBattle & destination, const HypotheticBattle & captured)
{
	const auto & hits = captured.getProjectedPrimalBurstHits();
	for(size_t index = destination.getProjectedPrimalBurstHits().size(); index < hits.size(); ++index)
	{
		const auto & hit = hits[index];
		if(!hit.elementalCataclysm || !hit.preHitTarget) continue;
		const auto id = hit.preHitTarget->unitId();
		const auto * final = captured.battleGetUnitByID(id);
		if(final && destination.battleGetUnitByID(id))
			*destination.getForUpdate(id) = *final->acquireState();
	}
}
