/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/constants/NumericConstants.h"
void HypotheticBattle::captureHeroActionState(const CBattleInfoCallback & source, BattleSide side)
{
	rapidResponseStates[side] = source.getBattle()->getRapidResponseState(side);
	reducedExtraActivationStates[side] = source.getBattle()->getReducedExtraActivationState(side);
	crossSchoolFormulaStates[side] = source.getBattle()->getCrossSchoolFormulaState(side);
	extendSpellRounds[side] = source.getBattle()->getExtendSpellLastRound(side);
	if(const auto lease = source.preparedHeroOrderStates(side))
		heroOrderStates[side] = lease;
	else
	{
		auto captured = source.getBattle()->getHeroOrderStates(side);
		SideInBattle::validateOrderCollection(captured);
		heroOrderStates[side] = std::make_shared<const std::vector<HeroOrderState>>(std::move(captured));
	}
	relentlessAssaultStates[side] = source.getBattle()->getRelentlessAssaultState(side);
	battlecraftMasteryAwardRounds[side] = source.getBattle()->getBattlecraftMasteryAwardRound(side);
	armorerLastStandUsedStates[side] = source.getBattle()->armorerLastStandUsed(side);
	armorerDefiantStates[side] = source.getBattle()->getArmorerDefiantState(side);
	if(const auto * parent = dynamic_cast<const HypotheticBattle *>(&source))
		commandResources[side] = parent->commandResources.at(side);
	else
	{
		const auto knowledge = source.battleGetVisibleHeroPolicy(side).kind();
		if(knowledge == VisibleHeroPolicy::Knowledge::VISIBLE_HERO
			|| knowledge == VisibleHeroPolicy::Knowledge::KNOWN_ABSENT)
			commandResources[side] = source.getBattle()->getCommandResources(side);
	}
	doubleCommandStates[side] = source.getBattle()->getDoubleCommandState(side);
	preCombatOrderStates[side] = source.getBattle()->getPreCombatOrderState(side);
	chainGateArmedStates[side] = source.getBattle()->getChainGateArmed(side);
	masterGateUsedStates[side] = source.getBattle()->getMasterGateUsed(side);
	passiveDivineMandateUses[side] = source.getBattle()->getPassiveDivineMandateUses(side);
}

bool HypotheticBattle::hasCanonicalDamageQuerySources(size_t remaining, const BattleInfo * expected) const
{
	if(remaining == 0 || ownedFrameBatch || !subject || !sourceBinding.isCurrentSource(subject)
		|| subject.get() == this || stackStates.size() > 256)
		return false;
	for(const auto & [id, unit] : stackStates)
		if(!unit || typeid(*unit) != typeid(StackWithBonuses))
			return false;
	if(typeid(*subject) == typeid(HypotheticBattle))
		return subject->hasCanonicalDamageQuerySources(remaining - 1, expected);
	// Qualified dispatch prevents an unknown callback from certifying itself.
	return subject->CBattleInfoCallback::hasCanonicalDamageQuerySources(remaining - 1, expected);
}
