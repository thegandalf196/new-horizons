/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineFuryValue.h"
#include "AttackPossibility.h"
#include "SpellTargetsEvaluator.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
namespace passiveDivineFuryAI
{
bool admitted(const CBattleInfoCallback & state, const BattleAttackInfo & attack)
{
	return attack.physicalDamage && !attack.secondaryAttack && !attack.bracePreemptive
		&& attack.preemptiveDamagePercent == 0 && attack.cleaveDamagePercent == 0
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		&& newHorizonsPassiveDivineMandate::activeOrderRiders(state, attack.attacker).fury;
}
float expectedValue(const std::shared_ptr<CBattleInfoCallback> & state, const battle::Unit & victim,
	const BattleAttackInfo & attack, bool underOrderBeforeStrike, DamageCache & cache)
{
	if(!underOrderBeforeStrike || !victim.alive() || victim.isGhost()) return 0;
	const auto side = state->playerToSide(state->battleGetOwner(attack.attacker));
	if(side == BattleSide::NONE || state->battleGetOwner(&victim) == state->sideToPlayer(side)) return 0;
	const auto * hero = state->battleGetFightingHero(side);
	const int number = SpellID::decode("new-horizons:holyWrath");
	if(!hero || number < 0) return 0;
	const auto * spell = SpellID(number).toSpell();
	const spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_FURY, 0};
	spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, spell);
	cast.setAutomaticHeroCast(receipt);
	const battle::Target target{battle::Destination(&victim)};
	const auto mechanics = cast.mechanicsForTarget(target);
	if(!mechanics->canBeCastAt(target)) return 0;
	if(const auto * projected = dynamic_cast<const HypotheticBattle *>(state.get()))
	{
		const auto ward = projected->resolveProjectedCounterspell(side, spell);
		// An unresolved hidden ward grants no speculative damage credit. This is
		// a lower-bound proc heuristic, independent of native strike legality.
		if(!ward.resolutionKnown || !ward.negated || *ward.negated) return 0;
	}
	const auto damage = std::clamp<int64_t>(mechanics->adjustEffectValue(&victim),
		0, victim.getAvailableHealth());
	const auto success = SpellTargetEvaluator::applicationChance(mechanics.get(), &victim);
	return newHorizonsPassiveDivineMandate::FURY_CHANCE_PERCENT / 100.0f * success
		* AttackPossibility::calculateDamageReduce(nullptr, &victim, damage, cache, state);
}
}
