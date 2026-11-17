/*
 * SelectedAttackReceipt.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "GnollHarriersProjection.h"
#include "BattleShroudStrike.h"
#include "SelectedAttackReceipt.h"
#include "PhysicalStrikeProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "VisibleCandidateWorld.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsSoulHarvest.h"
#include "../../lib/battle/NewHorizonsOpportunist.h"

SelectedAttackReceipt::SelectedAttackReceipt(const AttackPossibility & preview)
	: queryOwner(preview.queryWorld), effectOwner(preview.effectPreview),
	actorSnapshot(preview.attackerState), recipients(preview.affectedUnits),
	soulHarvestHealthGained(preview.soulHarvestHealthGained), returnLandingHealthLoss(preview.returnLandingHealthLoss)
{
}

void SelectedAttackReceipt::captureDamage(HypotheticBattle & branch)
{
	recipients.push_back(actorSnapshot);
	for(const auto & affectedUnit : recipients)
	{
		auto unitToUpdate = branch.getForUpdate(affectedUnit->unitId());
		int64_t before, projected;
		uint32_t id;
		// Retain the original subtraction's operand/query sequencing contract.
		finalDamage[id = unitToUpdate->unitId()] = std::max<int64_t>(0,
			(before = unitToUpdate->getAvailableHealth()) - (projected = affectedUnit->getAvailableHealth())
			+ (unitToUpdate->unitId() == actorSnapshot->unitId() ? soulHarvestHealthGained : 0));
		if(const auto landing = returnLandingHealthLoss.find(id); landing != returnLandingHealthLoss.end())
			finalDamage[id] = std::max<int64_t>(0, finalDamage[id] - landing->second);
		observations.push_back({id, before, projected});
	}

}

BattleHex selectedAttackReceipt::commitMovement(const AttackPossibility & ap, battle::CUnitState & actor,
	HypotheticBattle & branch)
{
	if(const auto original = forecast::NativeAdapter::selectedReceiptMovement(branch, ap, actor)) return *original;
	return replayMovementNative(ap, actor, branch);
}

void selectedAttackReceipt::restorePosition(const AttackPossibility & ap, battle::CUnitState & actor,
	const HypotheticBattle & branch)
{
	if(!ap.from.isValid() || !actor.alive())
		return;
	if(ap.attack.shooting)
	{
		if(actor.getPosition() == ap.from)
			return;
		if(!ap.attackerState || !ap.attack.attacker || !ap.attack.defender
			|| ap.attack.archeryRangedDamageMultiplierPercent != newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT
			|| ap.attackerState->unitId() != actor.unitId() || ap.attackerState->getPosition() != ap.from
			|| ap.attack.attacker->unitId() != actor.unitId()
			|| branch.battleGetUnitByID(actor.unitId()) != &actor)
			return;
		if(ap.queryWorld)
		{
			const auto & source = ap.queryWorld->callback();
			if(source->battleGetUnitByID(actor.unitId()) != ap.attack.attacker
				|| source->battleGetUnitByID(ap.attack.defender->unitId()) != ap.attack.defender)
				return;
		}
		const auto * target = branch.battleGetUnitByID(ap.attack.defender->unitId());
		if(!target || !branch.battleCanSkirmisherAttackFromHex(&actor, target->getPosition(), ap.from))
			return;
	}
	actor.setPosition(ap.from);
}

void selectedAttackReceipt::restoreResources(const AttackPossibility & ap, const battle::CUnitState & captured,
	const std::shared_ptr<StackWithBonuses> & recipient,
	const std::shared_ptr<StackWithBonuses> & originalActor, HypotheticBattle & branch)
{
	if(forecast::NativeAdapter::selectedReceiptResources(branch, ap, captured, recipient, originalActor)) return;
	replayResourcesNative(ap, captured, recipient, originalActor, branch);
}

void selectedAttackReceipt::applyCapturedDamage(HypotheticBattle & branch,
	const std::shared_ptr<StackWithBonuses> & recipient, int64_t damage)
{
	const bool wasAlive = recipient->alive();
	const auto destroyed = PhysicalStrikeProjection::captureForecastDamage(branch, *recipient);
	if(!forecast::NativeAdapter::damage(branch, *recipient, damage, battle::DamageProvenance::OTHER, nullptr))
		recipient->damage(damage);
	if(wasAlive && !recipient->alive())
		branch.releaseSerpentBindingsForDeath(*recipient);
	branch.recordBloodrageTransition(recipient, wasAlive);
	if(destroyed) PhysicalStrikeProjection::completeDamagePacket(branch, std::span(&*destroyed, 1));
}

int64_t selectedAttackReceipt::grantSoulHarvest(battle::CUnitState & actor, int64_t killedCreatures)
{
	const auto before = actor.getAvailableHealth();
	return newHorizonsSoulHarvest::grant(actor, killedCreatures)
		? actor.getAvailableHealth() - before : 0;
}

void SelectedAttackReceipt::applyFallbackDamage(HypotheticBattle & branch)
{
	for(const auto & affectedUnit : recipients)
	{
		auto unitToUpdate = branch.getForUpdate(affectedUnit->unitId());
		auto damageDealt = finalDamage[unitToUpdate->unitId()];
		if(damageDealt > 0)
			selectedAttackReceipt::applyCapturedDamage(branch, unitToUpdate, damageDealt);
	}
}

std::unique_ptr<AttackPossibility> selectedAttackReceipt::refreshForestAmbush(
	const AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & branch,
	DamageCache & damageCache)
{
	const auto * captured = dynamic_cast<const battle::CUnitState *>(preview.attack.attacker);
	const auto * actor = branch->battleGetUnitByID(preview.attack.attacker->unitId());
	const auto * current = dynamic_cast<const battle::CUnitState *>(actor);
	if(!captured || !current || captured->forestAmbush == current->forestAmbush)
		return {};
	const auto * target = branch->battleGetUnitByID(preview.attack.defender->unitId());
	if(!actor->alive() || !target || !target->alive())
		return {};
	auto attack = preview.attack;
	attack.attacker = actor;
	attack.defender = target;
	const auto owner = std::make_shared<const VisibleCandidateWorld>(branch);
	return std::make_unique<AttackPossibility>(VisibleCandidateWorld::evaluateRefreshed(
		owner, attack, preview.from, damageCache,
		gnollHarriersAI::replaysSameApproach(preview, *actor), preview.validatedApproachOrigin));
}

BattleAttackInfo selectedAttackReceipt::attackForStrike(const FortuneStrikeProjection & strike,
	const battle::Unit * actor, const battle::Unit * target)
{
	BattleAttackInfo attack(actor, target, 0, strike.shooting);
	attack.physicalDamage = strike.damageProvenance == battle::DamageProvenance::PHYSICAL_CREATURE;
	attack.retaliation = strike.retaliation;
	attack.dreadCavalryDamagePercent = strike.dreadCavalryDamagePercent;
	attack.bracePreemptive = strike.bracePreemptive;
	attack.preemptiveDamagePercent = strike.preemptiveDamagePercent;
	attack.cleaveDamagePercent = strike.cleaveDamagePercent;
	attack.attackerPos = actor->getPosition();
	attack.defenderPos = target->getPosition();
	return attack;
}
