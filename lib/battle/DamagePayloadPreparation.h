/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../combatScripts/IDamageCalculatorScript.h"
#include <optional>

class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;

/// Attack-local intermediate data for sequential native preparation/finalization.
/// Never retained or shared between targets, state changes or Luck calculations.
struct DamagePayloadPreparation
{
	const IBattleInfo * currentBattle = nullptr;
	DamageAttackInfo payload;
	struct OrderCauses
	{
		HeroCommand attackerOrderCause = HeroCommand::NONE;
		HeroCommand defenderOrderCause = HeroCommand::NONE;
		std::vector<HeroCommand> attackerOrderCauses;
		std::vector<HeroCommand> defenderOrderCauses;
	} causes;
	std::optional<const CGHeroInstance *> attackerHero, defenderHero;
	std::optional<PlayerColor> attackerOwner, defenderOwner;
	std::optional<bool> defended;
	const CGHeroInstance * readAttackerHero(const CBattleInfoCallback &, const BattleAttackInfo &);
	const CGHeroInstance * readDefenderHero(const CBattleInfoCallback &, const BattleAttackInfo &);
	PlayerColor readAttackerOwner(const CBattleInfoCallback &, const BattleAttackInfo &);
	PlayerColor readDefenderOwner(const CBattleInfoCallback &, const BattleAttackInfo &);
	bool defenderDefended(const BattleAttackInfo &);
};
