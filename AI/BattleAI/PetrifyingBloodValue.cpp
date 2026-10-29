/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "PetrifyingBloodValue.h"
#include "AttackPossibility.h"
#include <map>
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsPetrifyingBlood.h"
namespace petrifyingBloodAI
{
namespace
{
float valueAfterSequence(const std::shared_ptr<CBattleInfoCallback> & battle,
	const Pending & pending, DamageCache & cache, float & chanceProbability)
{
	const auto & attacker = *pending.attacker;
	const auto & recipient = *pending.recipient;
	if(!attacker.alive() || !recipient.alive() || attacker.hasBonusOfType(BonusType::NOT_ACTIVE)
		|| !newHorizonsPetrifyingBlood::eligiblePair(*battle, &attacker, recipient))
		return 0;
	const int chance = newHorizonsPetrifyingBlood::chanceBasisPoints(*battle, recipient, attacker);
	if(chance <= 0)
		return 0;
	const float opportunity = AttackPossibility::calculateDamageReduce(&recipient, &attacker,
		attacker.getAvailableHealth(), cache, battle);
	chanceProbability = chance / 10000.0f;
	return opportunity * chanceProbability;
}
}
void capture(const std::shared_ptr<CBattleInfoCallback> & battle,
	battle::CUnitState & attacker, battle::CUnitState & recipient,
	int64_t damage, bool ranged, bool physical, std::vector<Pending> & pending)
{
	if(!battle || !newHorizonsPetrifyingBlood::qualifies(*battle, &attacker, recipient, damage, ranged, physical))
		return;
	recipient.petrifyingBloodRound = battle->battleGetRound();
	pending.push_back({&attacker, &recipient});
}
float resolve(const std::shared_ptr<CBattleInfoCallback> & battle,
	const battle::CUnitState & reference, const std::vector<Pending> & pending, DamageCache & cache)
{
	float result = 0;
	// Native resolves in capture order and skips subsequent attempts after the
	// target is petrified. This candidate-local probability is not unit state.
	std::map<uint32_t, float> remainingUnpetrified;
	for(const auto & attempt : pending)
	{
		float chance = 0;
		const float value = valueAfterSequence(battle, attempt, cache, chance);
		auto entry = remainingUnpetrified.try_emplace(attempt.attacker->unitId(), 1.0f).first;
		const float weighted = entry->second * value;
		entry->second *= 1.0f - chance;
		result += battle->battleMatchActionController(&reference, attempt.recipient, true) ? weighted : -weighted;
	}
	return result;
}
void complete(AttackPossibility & preview, const std::shared_ptr<CBattleInfoCallback> & battle,
	const std::vector<Pending> & pending, bool defer, DamageCache & cache)
{
	if(defer)
	{
		for(const auto & attempt : pending)
			preview.deferredBloodAttempts.push_back({attempt.attacker->unitId(), attempt.recipient->unitId()});
		return;
	}
	// Nested previews own different copies. Rebind only their typed IDs after
	// all shots/reactions finish; never retain borrowed child-state pointers.
	auto finalBattle = preview.effectPreview ? preview.effectPreview : battle;
	auto finalPending = pending;
	for(const auto & attempt : preview.deferredBloodAttempts)
	{
		const auto * attacker = dynamic_cast<const battle::CUnitState *>(finalBattle->battleGetUnitByID(attempt.attacker));
		const auto * recipient = dynamic_cast<const battle::CUnitState *>(finalBattle->battleGetUnitByID(attempt.recipient));
		if(attacker && recipient && recipient->petrifyingBloodRound == finalBattle->battleGetRound())
			finalPending.push_back({attacker, recipient});
	}
	preview.petrifyingBloodControlValue = resolve(finalBattle, *preview.attackerState, finalPending, cache);
}
float controlValue(const std::shared_ptr<CBattleInfoCallback> & battle,
	battle::CUnitState & attacker, battle::CUnitState & recipient,
	int64_t damage, bool ranged, bool physical, DamageCache & cache)
{
	std::vector<Pending> pending;
	capture(battle, attacker, recipient, damage, ranged, physical, pending);
	float chance = 0;
	return pending.empty() ? 0 : valueAfterSequence(battle, pending.front(), cache, chance);
}
}
