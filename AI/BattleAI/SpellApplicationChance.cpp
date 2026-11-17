/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicCore.h"

float SpellTargetEvaluator::applicationChance(const spells::Mechanics * mechanics, const battle::Unit * unit)
{
	if(!mechanics || !unit) return 1;
	if((!mechanics->isNegativeSpell() || !mechanics->isMagicalEffect())
		&& !newHorizonsMetamagic::hasResistibleCreatureAffliction(*mechanics)) return 1;
	if(mechanics->getSpellId().toSpell()->getJsonKey() == "new-horizons:forceLance") return 1;
	float chance = static_cast<float>(1 - newHorizonsMetamagic::effectiveResistanceFailureProbability(*mechanics, unit));
	const auto * battle = mechanics->battle();
	if(!battle || !battle->getBattle() || chance <= 0 || chance >= 1) return chance;
	const auto recipientSide = battle->playerToSide(battle->battleGetOwner(unit));
	const auto casterSide = mechanics->isMagicMirror()
		? battle->otherSide(mechanics->getCasterSide()) : mechanics->getCasterSide();
	if((recipientSide == BattleSide::ATTACKER || recipientSide == BattleSide::DEFENDER)
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER)
		&& recipientSide != casterSide
		&& battle->getBattle()->getAdverseCombatRerollState(recipientSide).available())
	{
		const int resistance = newHorizonsMetamagic::normalResistanceBasisPoints(*mechanics, unit);
		const double ordinarySuccess = 1 - resistance / 10000.0;
		// Adverse Luck affects the first normal check. Metamagic's one fresh
		// normal retry never acquires another adverse-roll allowance.
		chance = static_cast<float>(1 - newHorizonsMetamagic::resistanceFailureProbabilityBasisPoints(resistance,
			newHorizonsMetamagic::debuffRetryChance(*mechanics), 1 - ordinarySuccess * ordinarySuccess));
	}
	return chance;
}
