/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "../../lib/constants/NumericConstants.h"
void HypotheticBattle::captureHeroActionState(const CBattleInfoCallback & source, BattleSide side)
{
	rapidResponseStates[side] = source.getBattle()->getRapidResponseState(side);
	reducedExtraActivationStates[side] = source.getBattle()->getReducedExtraActivationState(side);
	crossSchoolFormulaStates[side] = source.getBattle()->getCrossSchoolFormulaState(side);
	spellResponseStates[side] = source.getBattle()->getSpellResponseState(side);
	extendSpellRounds[side] = source.getBattle()->getExtendSpellLastRound(side);
	overwhelmingFormulaStates[side] = source.getBattle()->getOverwhelmingFormulaState(side);
	heroOrderStates[side] = source.getBattle()->getHeroOrderStates(side);
	relentlessAssaultStates[side] = source.getBattle()->getRelentlessAssaultState(side);
	battlecraftMasteryAwardRounds[side] = source.getBattle()->getBattlecraftMasteryAwardRound(side);
	armorerLastStandUsedStates[side] = source.getBattle()->armorerLastStandUsed(side);
	rebirthChainUsedStates[side] = source.getBattle()->getRebirthChainUsed(side);
	phoenixSparkUsedStates[side] = source.getBattle()->getPhoenixSparkUsed(side);
	armorerDefiantStates[side] = source.getBattle()->getArmorerDefiantState(side);
	warcastingStates[side] = source.getBattle()->getWarcastingState(side);
	heroActionAllowances[side] = source.getBattle()->getHeroActionAllowances(side);
	doubleCommandStates[side] = source.getBattle()->getDoubleCommandState(side);
	preCombatOrderStates[side] = source.getBattle()->getPreCombatOrderState(side);
	heroSpellCastCompletedStates[side] = source.getBattle()->hasCompletedHeroSpellCast(side);
	completedHeroSpellLevelMasks[side] = 0;
	for(int32_t level = 1; level <= GameConstants::SPELL_LEVELS; ++level)
		if(source.getBattle()->hasCompletedHeroSpellLevel(side, level))
			completedHeroSpellLevelMasks[side] |= static_cast<std::uint8_t>(1u << (level - 1));
	counterspellArmedStates[side] = source.getBattle()->getCounterspellArmed(side);
	countersequenceArmedStates[side] = source.getBattle()->getMetamagicCountersequenceArmed(side);
	auto & meta = metamagicStates[side];
	meta.uses = source.getBattle()->getMetamagicUsesConsumed(side);
	meta.pending = source.getBattle()->getMetamagicPendingCount(side);
	meta.grandUsed = source.getBattle()->getMetamagicGrandUsed(side);
	meta.firstSpell = source.getBattle()->getMetamagicFirstSpell(side);
	meta.firstTarget = source.getBattle()->getMetamagicFirstTargetUnitId(side);
	meta.firstCountered = source.getBattle()->getMetamagicFirstCounterspellNegated(side);
	meta.sequence = source.getBattle()->getMetamagicSequenceSpells(side);
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
