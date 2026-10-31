/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsSerpentsEmbrace.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
struct BattleAttackInfo;
namespace battlePrimaryStrikeAftermath
{
struct Prepared
{
	uint32_t source = 0;
	uint32_t target = 0;
	bool gaze = false;
	bool hostilePrimaryMelee = false;
	std::optional<newHorizonsSerpentsEmbrace::PreparedBinding> binding;
	newHorizonsOrdinaryStrike::EffectPlan ordinary;
	newHorizonsOrdinaryStrike::EffectPlan lanceFormation;
};
Prepared prepare(const CBattleInfoCallback & battle, const BattleAttackInfo & attack,
	const std::vector<BattleStackAttacked> & hits);
void recordPrimaryMelee(const Prepared & prepared, std::optional<uint32_t> * victim);
void apply(CGameHandler & game, const CBattleInfoCallback & battle, const Prepared & prepared);
void applyOrdinaryEffect(CGameHandler & game, const CBattleInfoCallback & battle,
	const newHorizonsOrdinaryStrike::EffectPlan & effectPlan);
}
