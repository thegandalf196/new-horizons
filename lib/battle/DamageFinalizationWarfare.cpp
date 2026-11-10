/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsWarfare.h"
#include "CBattleInfoCallback.h"
#include "BattleAttackInfo.h"
#include "CommandArmyBonuses.h"
#include "NewHorizonsCombatSkills.h"
#include "../combatScripts/IDamageCalculatorScript.h"

void newHorizonsWarfare::appendAttackModifiers(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, DamageAttackInfo & payload)
{
	const int outgoing = directAttackBonus(battle, attack);
	const auto army = newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		? commandArmyBonuses::directAttackMultipliers(*attack.attacker) : std::vector<int>{};
	if(outgoing != 0 || !army.empty())
	{
		if(payload.heroOrderFinalDamageMultipliers.empty() && payload.heroOrderFinalDamageMultiplier != 100)
			payload.heroOrderFinalDamageMultipliers.push_back(payload.heroOrderFinalDamageMultiplier);
		if(outgoing != 0) payload.heroOrderFinalDamageMultipliers.push_back(100 + outgoing);
		payload.heroOrderFinalDamageMultipliers.insert(payload.heroOrderFinalDamageMultipliers.end(), army.begin(), army.end());
	}
	if(enabled(attack.defender ? battle.battleGetOwnerHero(attack.defender) : nullptr))
		payload.commandBeneficiaryDamageMultiplierPercent = 100 - incomingReduction(battle, attack.defender);
}
