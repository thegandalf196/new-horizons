/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"

void HypotheticBattle::restoreProjectedNormalMana(const SetMana & packet)
{
	packet.validateOperationPayload();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battleGetFightingHero(side);
		if(!hero || hero->id != packet.hid)
			continue;
		std::optional<forecast::SideReceipt> receipt;
		if(packet.operation == SetMana::Operation::RESTORE_NORMAL)
			receipt = forecast::SideTransitions::restoreNormal(forecastSide(side), static_cast<int32_t>(packet.amount), searchBudget().get());
		else if(packet.operation == SetMana::Operation::REFUND_HERO_LUCK)
		{
			if(!spellPaymentScopes.queueRefund(side,
				{static_cast<int32_t>(packet.amount), packet.bufferAmount, packet.temporaryBufferAmount}))
				throw std::invalid_argument("Projected refund requires an explicit active manual cast scope");
			return;
		}
		if(!receipt || !commitForecastSide(side, *receipt))
			throw std::invalid_argument("Unsupported or stale projected Mana mutation");
		return;
	}
	throw std::invalid_argument("Projected Mana recovery requires a fighting hero");
}

bool HypotheticBattle::stageProjectedSpellPayment(BattleSide side, SpellID spell, int32_t exactCost)
{
	const auto & current = forecastSide(side);
	const auto & begun = begunProjectedSpellAllowances.at(side);
	if(!spellPaymentScopes.healthy() || !current->hasVisibleHero() || !current->mana || !current->knowsSpell(spell)
		|| !begun || begun->spell != spell || begun->action.epoch != projectedActionEpochs.at(side)
		|| evaluatedSpellPayments.at(side) || nativeSpellAttemptEpochs.at(side) == begun->action.epoch
		|| rejectedSpellPaymentEpochs.at(side) == begun->action.epoch || exactCost < 0 || current->mana->pools.getTotal() < exactCost) return false;
	evaluatedSpellPayments.at(side) = forecast::SpellPaymentTicket{side, spell, exactCost, begun->action.epoch, false, {}};
	return true;
}

int64_t HypotheticBattle::getResidualLifeDamage(BattleSide side) const
{
	const auto & value = forecastSide(side);
	if(value && value->ledger) return value->ledger->residualLifeDamage;
	return subject->getBattle()->getResidualLifeDamage(side);
}
