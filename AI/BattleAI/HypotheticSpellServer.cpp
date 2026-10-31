/*
 * HypotheticSpellServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ProjectedInjuryLifecycle.h"
#include "../../lib/battle/GraveDustEffectChanges.h"
#include "../../lib/battle/GraveDustBodyCleanup.h"
#include "StackWithBonuses.h"
#include "PhysicalStrikeProjection.h"

#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/constants/NumericConstants.h"
#include "../../lib/gameState/GameStatePackVisitor.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "HypotheticBattle.h"
#include "HypotheticBattleTypes.h"

HypotheticBattle::HypotheticServerCallback::HypotheticServerCallback(HypotheticBattle * owner_)
	:owner(owner_)
{
}

void HypotheticBattle::HypotheticServerCallback::complain(const std::string & problem)
{
	logAi->error(problem);
}

bool HypotheticBattle::HypotheticServerCallback::describeChanges() const
{
	return false;
}

void HypotheticBattle::HypotheticServerCallback::recordCrossSchoolFormulaCast(
	BattleSide side, const newHorizonsCrossSchoolFormula::Receipt & receipt)
{
	if(newHorizonsCrossSchoolFormula::acceptedReceipt(*owner, side, receipt.after.spell) != receipt)
		throw std::runtime_error("Stale detached Cross-School Formula receipt");
	owner->setCrossSchoolFormulaState(side, receipt.after);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side)
{
	recordCompletedHeroSpellCast(side, 0);
}

void HypotheticBattle::HypotheticServerCallback::recordCompletedHeroSpellCast(BattleSide side, int32_t spellLevel)
{
	if(side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
	{
		owner->heroSpellCastCompletedStates.at(side) = true;
		if(spellLevel >= 1 && spellLevel <= GameConstants::SPELL_LEVELS)
			owner->completedHeroSpellLevelMasks.at(side) |= static_cast<std::uint8_t>(1u << (spellLevel - 1));
	}
}

vstd::RNG * HypotheticBattle::HypotheticServerCallback::getRNG()
{
	return &rngStub;
}

bool HypotheticBattle::HypotheticServerCallback::rollCombatAbility(const IBattleInfoCallback &, const battle::Unit &, int percentageChance)
{
	// same averaging the RNG stub does - the hypothetical battle must stay deterministic
	return percentageChance >= 50;
}

bool HypotheticBattle::HypotheticServerCallback::rollHostileCombatAbility(const IBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & recipient, int percentageChance)
{
	const int basisPoints = actor.favorableCreatureAbilityChanceBasisPoints(percentageChance);
	int chance = basisPoints / 100;
	if(basisPoints % 100 && rngStub.nextInt(0, 99) < basisPoints % 100)
		++chance;
	const auto draw = [&]() { return rollCombatAbility(battle, actor, chance); };
	const auto actorSide = owner->playerToSide(owner->battleGetOwner(&actor));
	const auto recipientSide = owner->playerToSide(owner->battleGetOwner(&recipient));
	const bool hostile = (actorSide == BattleSide::ATTACKER || actorSide == BattleSide::DEFENDER)
		&& (recipientSide == BattleSide::ATTACKER || recipientSide == BattleSide::DEFENDER)
		&& actorSide != recipientSide && actor.alive() && recipient.alive()
		&& recipient.isValidTarget(false) && !recipient.isInvincible();
	if(!hostile)
		return draw();
	return resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), recipientSide,
		chance > 0 && chance < 100, true, draw);
}

bool HypotheticBattle::HypotheticServerCallback::resolveAdverseCombatRoll(const BattleID & battleID,
	BattleSide side, bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw)
{
	if(battleID != owner->getBattleID())
		throw std::runtime_error("Adverse combat projection refers to another battle");
	const bool firstResult = draw();
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return firstResult;
	auto state = owner->getAdverseCombatRerollState(side);
	if(!state.consume(stochastic, firstResult == adverseOnTrue))
		return firstResult;

	BattleAdverseRerollStateChanged packet;
	packet.battleID = battleID;
	packet.side = side;
	packet.state = state;
	BattleStatePackVisitor visitor(*owner);
	packet.visit(visitor);
	return draw();
}

void HypotheticBattle::HypotheticServerCallback::apply(CPackForClient & pack)
{
	if(dynamic_cast<SetReactiveWeaveState *>(&pack) || dynamic_cast<SetSpellResponseState *>(&pack)
		|| dynamic_cast<SetOverwhelmingFormulaState *>(&pack))
	{
		BattleStatePackVisitor visitor(*owner);
		pack.visit(visitor);
		return;
	}
	logAi->error("Package of type %s is not allowed in battle evaluation", typeid(pack).name());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleLogMessage & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleStackMoved & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleUnitsChanged & pack)
{
	const auto destroyed = PhysicalStrikeProjection::captureDamagePacket(*owner, std::span<const UnitChanges>(pack.changedStacks));
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
	PhysicalStrikeProjection::completeDamagePacket(*owner, destroyed);
}

void HypotheticBattle::HypotheticServerCallback::apply(SetStackEffect & pack)
{
	newHorizonsGraveDust::validateEffectChanges(*owner, pack);
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(StacksInjured & pack)
{
	newHorizonsGraveDust::validateBodyChanges(*owner, pack.stacks);
	const auto destroyed = PhysicalStrikeProjection::captureDamagePacket(*owner, std::span<const BattleStackAttacked>(pack.stacks));
	std::vector<ProjectedPlagueHit> plagueHits;
	static const SpellID plagueSpell(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	for(const auto & hit : pack.stacks)
	{
		if(!(hit.flags & BattleStackAttacked::SPELL_EFFECT) || hit.spellID != plagueSpell || hit.damageAmount <= 0)
			continue;
		const auto target = owner->getForUpdate(hit.stackAttacked);
		if(!target || !target->alive())
			continue;
		auto snapshot = std::make_shared<StackWithBonuses>(owner, target.get());
		snapshot->freezeDamageScoringBonusSnapshot();
		plagueHits.push_back({snapshot, owner->battleGetOwner(target.get()), hit.damageAmount});
	}
	struct PendingRebirth
	{
		newHorizonsElementalRebirth::DeathSnapshot snapshot;
		bool cloneKilled = false;
		bool nativeRebirth = false;
	};
	std::map<uint32_t, PendingRebirth> pendingRebirths;
	for(const auto & hit : pack.stacks)
	{
		auto unit = owner->getForUpdate(hit.stackAttacked);
		if(!unit || !unit->alive() || pendingRebirths.contains(hit.stackAttacked))
			continue;
		auto snapshot = owner->captureElementalRebirthSource(*unit);
		if(!snapshot)
			continue;
		// Accepted destruction flags encode the resolved same-unit result. Mere
		// readiness remains the fallback only for a flagless forecast packet.
		const bool nativeRebirth = hit.killed() || hit.cloneKilled()
			? hit.willRebirth() : owner->hasReadyNativeRebirth(unit.get());
		pendingRebirths.emplace(hit.stackAttacked, PendingRebirth{
			*snapshot, unit->isClone(), nativeRebirth});
	}

	projectedInjuryLifecycle::before(*owner, pack.stacks);
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
	projectedInjuryLifecycle::after(*owner, pack.stacks);
	PhysicalStrikeProjection::completeDamagePacket(*owner, destroyed);
	for(const auto & [unitId, pending] : pendingRebirths)
	{
		auto unit = owner->getForUpdate(unitId);
		const bool hitKilled = unit && !unit->alive();
		owner->projectElementalRebirth(unit.get(), pending.snapshot, hitKilled,
			pending.cloneKilled, pending.nativeRebirth);
	}
	// Publish only after successful state application and its existing rebirth handling.
	owner->projectedPlagueHits.insert(owner->projectedPlagueHits.end(), plagueHits.begin(), plagueHits.end());
}

void HypotheticBattle::HypotheticServerCallback::apply(BattleObstaclesChanged & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

void HypotheticBattle::HypotheticServerCallback::apply(CatapultAttack & pack)
{
	BattleStatePackVisitor visitor(*owner);
	pack.visit(visitor);
}

HypotheticBattle::HypotheticEnvironment::HypotheticEnvironment(HypotheticBattle * owner_, const Environment * upperEnvironment)
	: owner(owner_),
	env(upperEnvironment)
{

}

const Services * HypotheticBattle::HypotheticEnvironment::services() const
{
	return env->services();
}

const Environment::BattleCb * HypotheticBattle::HypotheticEnvironment::battle(const BattleID & battleID) const
{
	assert(battleID == owner->getBattleID());
	return owner;
}

const Environment::GameCb * HypotheticBattle::HypotheticEnvironment::game() const
{
	return env->game();
}
