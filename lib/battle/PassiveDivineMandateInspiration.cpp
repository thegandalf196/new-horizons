/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandateInspiration.h"
#include "PassiveDivineMandate.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/SetStackEffect.h"
#include <algorithm>
#include <stdexcept>
namespace newHorizonsPassiveDivineMandate
{
SetStackEffect inspiringSpeedEffects(const CBattleInfoCallback & battle, BattleSide side,
	spells::AutomaticHeroCastReceipt receipt, const std::vector<uint32_t> & resolvedRecipientIds)
{
	receipt.validateShape();
	SetStackEffect result;
	result.battleID = battle.getBattle()->getBattleID();
	if(receipt.inspiringDuration == 0)
		return result;
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::runtime_error("Inspiring Leadership has no casting side");
	const auto policy = capturePolicy(battle.battleGetFightingHero(side));
	const auto order = battle.getBattle()->getHeroOrderState(side, receipt.issuingOrder);
	if(!policy.has(Perk::INSPIRING_LEADERSHIP) || !order
		|| order->issuedRound != receipt.issuingRound || !order->passiveDivineMandate
		|| *order->passiveDivineMandate != policy)
		throw std::runtime_error("Inspiring Leadership has no accepted issuing Order");
	const int duration = order->revision && order->command == HeroCommand::VANGUARD
		? std::clamp(order->revision->expiresAfterRound - order->issuedRound + 1, 1, 3) : 1;
	if(duration != receipt.inspiringDuration)
		throw std::runtime_error("Inspiring Leadership differs from scheduled Order duration");
	Bonus speed(BonusDuration::N_TURNS, BonusType::STACKS_SPEED, BonusSource::SECONDARY_SKILL, inspiringSpeed(policy),
		BonusSourceID(SecondarySkill(SecondarySkill::decode(SKILL))));
	speed.turnsRemain = receipt.inspiringDuration;
	speed.stacking = "new-horizons:divineMandate.inspiringLeadership";
	speed.description.appendRawString("Inspiring Leadership: +2 Speed");
	auto recipients = resolvedRecipientIds;
	std::sort(recipients.begin(), recipients.end());
	recipients.erase(std::unique(recipients.begin(), recipients.end()), recipients.end());
	for(const auto id : recipients)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(unit && unit->alive() && !unit->isGhost()
			&& battle.battleGetOwner(unit) == battle.sideToPlayer(side))
			result.toAdd.emplace_back(id, std::vector<Bonus>{speed});
	}
	return result;
}
}
