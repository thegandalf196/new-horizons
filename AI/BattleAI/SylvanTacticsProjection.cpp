/*
 * SylvanTacticsProjection.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SylvanTacticsProjection.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"

namespace sylvanTacticsAI
{
namespace
{
SylvanTacticsState stateFor(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	const auto side = unit ? battle.playerToSide(battle.battleGetOwner(unit)) : BattleSide::NONE;
	return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER
		? battle.getBattle()->getSylvanLuckState(side).tactics : SylvanTacticsState{};
}
bool changesStrikes(const SylvanTacticsState & state)
{
	return state.enabled && (state.coveringFire || state.crossfire || state.disruptiveStrike || state.wildHunt);
}
}

void replaySecondary(HypotheticBattle & battle, const SecondaryHit & hit)
{
	auto actor = battle.getForUpdate(hit.attackerId);
	auto target = battle.getForUpdate(hit.targetId);
	BattleAttackInfo attack(actor.get(), target.get(), 0, true);
	attack.secondaryAttack = true;
	StrikeProjection projection(&battle);
	auto damage = hit.requestedDamage;
	projection.damage(battle, attack, *target, damage, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(actor->unitId());
	if(!target->alive()) battle.releaseSerpentBindingsForDeath(*target);
}

bool needsProjection(const CBattleInfoCallback & battle)
{
	if(!battle.getBattle()) return false;
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(newHorizonsSylvanTactics::enabled(battle.getBattle()->getSideHero(side))
			&& changesStrikes(battle.getBattle()->getSylvanLuckState(side).tactics)) return true;
	return false;
}

bool dynamicDamage(const CBattleInfoCallback & battle,
	const battle::Unit * attacker, const battle::Unit * defender)
{
	// A provider's guard follows its recipient across effective-owner changes.
	// Keep the provider perk gate after expiry, so an ID-only cache cannot revive
	// a value recorded while that recipient was protected.
	bool policyPresent = false;
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		policyPresent |= newHorizonsSylvanTactics::enabled(battle.getBattle()->getSideHero(side));
	if(!policyPresent) return false;
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto provider = battle.getBattle()->getSylvanLuckState(side).tactics;
		if(provider.enabled && provider.coveringFire) return true;
	}
	const auto attack = stateFor(battle, attacker);
	const auto defense = stateFor(battle, defender);
	return (attack.enabled && (attack.patientHunter || attack.measuredAdvance || attack.crossfire))
		|| (defense.enabled && defense.dappledCover);
}

void StrikeProjection::damage(const CBattleInfoCallback & world, const BattleAttackInfo & attack,
	battle::CUnitState & target, int64_t & amount, battle::DamageProvenance provenance)
{
	Pending * captured = nullptr;
	bool mayRebirth = false;
	if(branch && attack.attacker && provenance == battle::DamageProvenance::PHYSICAL_CREATURE
		&& newHorizonsSylvanTactics::enabled(branch->battleGetOwnerHero(attack.attacker)))
	{
		auto found = std::ranges::find_if(pending, [&attack](const Pending & strike)
		{
			return strike.snapshot.shooter == attack.attacker->unitId();
		});
		if(found == pending.end())
		{
			const auto side = branch->playerToSide(branch->battleGetOwner(attack.attacker));
			if((side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
				&& changesStrikes(branch->getSylvanLuckState(side).tactics))
			{
				auto snapshot = newHorizonsSylvanTactics::captureStrike(*branch, attack.attacker,
					attack.secondaryAttack ? nullptr : attack.defender, {}, attack.shooting, true);
				if(snapshot.valid)
				{
					pending.push_back({std::move(snapshot), side, {}, {}});
					found = std::prev(pending.end());
				}
			}
		}
		if(found != pending.end())
		{
			if(!attack.secondaryAttack && attack.defender) found->snapshot.primary = attack.defender->unitId();
			newHorizonsSylvanTactics::captureVictim(found->snapshot, *branch, attack.attacker, &target);
			mayRebirth = branch->hasReadyNativeRebirth(&target);
			captured = &*found;
		}
	}
	newHorizonsForestAmbush::damageFromAttack(world, target, amount, provenance);
	if(!captured) return;
	captured->hits.emplace_back(target.unitId(), amount);
	if(!target.alive() && !mayRebirth) captured->destroyed.push_back(target.unitId());
}

void StrikeProjection::finish(uint32_t attackerId)
{
	if(!branch || pending.empty()) return;
	for(auto & strike : pending)
	{
		if(strike.snapshot.shooter != attackerId) continue;
		// Neighbors that died to collateral or another resolved effect cannot gain a guard.
		std::erase_if(strike.snapshot.adjacentFriends, [this](uint32_t id)
		{
			return !newHorizonsSylvanTactics::ordinary(branch->battleGetUnitByID(id));
		});
		auto fortune = branch->getSylvanLuckState(strike.side);
		newHorizonsSylvanTactics::finishStrike(strike.snapshot, strike.hits, strike.destroyed, fortune.tactics);
		branch->setSylvanLuckState(strike.side, fortune);
	}
	std::erase_if(pending, [attackerId](const Pending & strike) { return strike.snapshot.shooter == attackerId; });
}
}
