/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackPreviewMovement.h"
#include "PhysicalStrikeProjection.h"
#include "SerpentBindingDeathPreview.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include <algorithm>
namespace attackPreviewMovement
{
std::optional<AttackPossibility> project(const BattleAttackInfo & attackInfo, BattleHex hex,
	DamageCache & damageCache, const std::shared_ptr<CBattleInfoCallback> & state, bool perfectMoment,
	const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial, const dreadCavalryAI::Action & dreadAction, bool validatedVoluntaryApproach, BattleHex approachOrigin)
{
	auto attacker = attackInfo.attacker;
	// Ordinary movement is projected before the attack, not as a post-attack
	// penalty: a lethal reaction must prevent the intended strike entirely.
	if(attacker && hex.isAvailable() && hex != attacker->getPosition()
		&& !attackInfo.retaliation && (!attackInfo.shooting || attackInfo.attackerPos == hex)
		&& !attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
		&& !attacker->hasBonusOfType(BonusType::CONFUSION_PENDING)
		&& std::ranges::any_of(state->battleAliveUnits(), [&](const battle::Unit * unit)
			{ return state->battleOverwatchReady(unit); }))
	{
		if(const auto model = std::dynamic_pointer_cast<HypotheticBattle>(state))
		{
			auto projected = std::make_shared<HypotheticBattle>(model->env, state);
			auto hits = projected->projectVoluntaryMovement(attacker->unitId(), hex, validatedVoluntaryApproach);
			if(!hits.empty())
			{
				auto moved = projected->getForUpdate(attacker->unitId());
				BattleAttackInfo following = attackInfo;
				following.attacker = moved.get();
				following.defender = projected->battleGetUnitByID(attackInfo.defender->unitId());
				auto result = moved->alive()
					? AttackPossibility::evaluate(following, hex, damageCache, projected, perfectMoment, aerial, &dreadAction, validatedVoluntaryApproach, approachOrigin)
					: AttackPossibility(hex, BattleHex::INVALID, following);
				result.validatedVoluntaryApproach = validatedVoluntaryApproach && !aerial;
				result.validatedApproachOrigin = result.validatedVoluntaryApproach ? approachOrigin : BattleHex(BattleHex::INVALID);
				if(!result.effectPreview)
					result.effectPreview = projected;
				if(!result.attackerState)
					result.attackerState = moved;
				for(const auto & hit : hits)
				{
					result.attackerDamageReduce += AttackPossibility::calculateDamageReduce(
						state->battleGetUnitByID(hit.shooterId), attacker, hit.healthLoss, damageCache, state);
					result.affectedUnits.push_back(result.effectPreview->getForUpdate(hit.shooterId));
				}
				result.overwatchHits = std::move(hits);
				return result;
			}
		}
	}
	return std::nullopt;
}
void applyObstacleDamage(AttackPossibility & ap, DamageCache & damageCache,
	const std::shared_ptr<CBattleInfoCallback> & state, const std::shared_ptr<HypotheticBattle> & fortunePreview,
	bool rootMayHaveSerpents, std::vector<uint32_t> & serpentSourceDeaths)
{
	const auto mayReleaseSerpents = [&]()
	{
		return rootMayHaveSerpents || (fortunePreview && fortunePreview->mayHaveSerpentBindings());
	};
	auto obstacleDamage = damageCache.getObstacleDamage(ap.from, ap.attack.attacker);
	if(obstacleDamage <= 0)
		return;
	ap.attackerDamageReduce += AttackPossibility::calculateDamageReduce(nullptr,
		ap.attack.attacker, obstacleDamage, damageCache, state);
	const bool serpentWasAlive = mayReleaseSerpents() && ap.attackerState->alive();
	const auto destroyed = fortunePreview ? PhysicalStrikeProjection::captureForecastDamage(*fortunePreview, *ap.attackerState) : std::nullopt;
	ap.attackerState->damage(obstacleDamage);
	serpentBindingAI::observeDeath(mayReleaseSerpents(), fortunePreview, *ap.attackerState,
		serpentWasAlive, serpentSourceDeaths);
	if(destroyed) PhysicalStrikeProjection::completeDamagePacket(*fortunePreview, std::span(&*destroyed, 1));
	ap.preAttackDamage += obstacleDamage;
}

}
