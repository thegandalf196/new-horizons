/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneInspirationProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/bonuses/BonusList.h"
namespace arcaneInspirationAI
{
namespace
{
bool changesStrikeState(const battle::Unit * unit)
{
	if(!unit) return false;
	return unit->hasBonus(CSelector([](const Bonus * bonus)
	{
		return arcaneInspiration::isRetained(bonus)
			|| (arcaneInspiration::isActive(bonus)
				&& (bonus->subtype.getNum() & arcaneInspiration::REPRISAL));
	}));
}
}
bool needsStrikeProjection(const battle::Unit * actor, std::span<const battle::Unit * const> targets)
{
	return changesStrikeState(actor) || std::ranges::any_of(targets, changesStrikeState);
}
std::optional<arcaneInspiration::Reprisal> StrikeProjection::prepare(const BattleAttackInfo & attack, int64_t beforeShieldDamage)
{
	if(branch && attack.attacker && !attack.secondaryAttack)
		struckActors.insert(attack.attacker->unitId());
	// Only entitlement and pre-casualty identities are captured here. The native
	// health stage below supplies actual post-shield damage, including overkill.
	return branch ? arcaneInspiration::captureReprisal(*branch, attack, beforeShieldDamage) : std::nullopt;
}
void StrikeProjection::record(std::optional<arcaneInspiration::Reprisal> receipt, int64_t postShieldDamage)
{
	if(!receipt) return;
	receipt->rawDamage = arcaneInspiration::reprisalRawDamage(postShieldDamage);
	if(receipt->rawDamage > 0) pending.push_back({receipt->target, *receipt});
}
bool needsReprisalProjection(const battle::Unit * actor, const battle::Unit * target)
{
	const auto responds = [](const battle::Unit * unit)
	{
		return unit && unit->hasBonus(CSelector([](const Bonus * bonus)
		{
			return arcaneInspiration::isActive(bonus) && (bonus->subtype.getNum() & arcaneInspiration::REPRISAL);
		}));
	};
	return responds(actor) || responds(target);
}
void StrikeProjection::finish(uint32_t actor)
{
	if(!branch) return;
	const auto * striker = branch->battleGetUnitByID(actor);
	if(struckActors.erase(actor) > 0 && striker && !striker->isTimeStopped())
		branch->getForUpdate(actor)->removeUnitBonus(CSelector(arcaneInspiration::isRetained));
	for(const auto & candidate : pending)
	{
		if(candidate.actor != actor) continue;
		const auto * target = branch->battleGetUnitByID(candidate.receipt.target);
		if(!target || !target->alive()) continue;
		auto before = std::make_shared<StackWithBonuses>(branch, target);
		before->freezeDamageScoringBonusSnapshot();
		const auto owner = branch->battleGetOwner(target);
		arcaneInspiration::publishReprisals(*branch->getServerCallback(), *branch, std::span(&candidate.receipt, 1));
		const auto * after = branch->battleGetUnitByID(candidate.receipt.target);
		const auto damage = after ? std::max<int64_t>(0, before->getAvailableHealth() - after->getAvailableHealth()) : 0;
		if(damage > 0) observations.push_back({std::move(before), owner, damage});
	}
	std::erase_if(pending, [actor](const Pending & value) { return value.actor == actor; });
}
std::pair<float, float> StrikeProjection::value(DamageCache & cache,
	const std::shared_ptr<CBattleInfoCallback> & source, PlayerColor reference) const
{
	std::pair<float, float> result{};
	for(const auto & hit : observations)
	{
		const auto amount = AttackPossibility::calculateDamageReduce(nullptr, hit.before.get(), hit.damage, cache, source);
		(hit.owner == reference ? result.first : result.second) += amount;
	}
	return result;
}
}
