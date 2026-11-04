/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/PassiveDivineMandateSpellPool.h"
#include "../../lib/battle/HeroOrderRecipients.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/CRandomGenerator.h"

void BattleActionProcessor::castPassiveDivineMandate(const CBattleInfoCallback & battle,
	BattleSide side, const HeroOrderState & order)
{
	if(!order.passiveDivineMandate)
		return;
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero)
		return;
	const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(*order.passiveDivineMandate,
		{battle.getBattle()->getPassiveDivineMandateUses(side)});
	if(!next)
		return;
	spells::AutomaticHeroCastReceipt receipt{
		spells::AutomaticHeroCastKind::DIVINE_MANDATE, next->completedCasts};
	receipt.issuingOrder = order.command;
	receipt.issuingRound = order.issuedRound;
	if(order.passiveDivineMandate->has(newHorizonsPassiveDivineMandate::Perk::INSPIRING_LEADERSHIP))
		receipt.inspiringDuration = order.revision && order.command == HeroCommand::VANGUARD
			? std::clamp(order.revision->expiresAfterRound - order.issuedRound + 1, 1, 3) : 1;
	const auto recipients = heroCommands::recipientIds(battle, side, order);
	const auto pool = newHorizonsPassiveDivineMandate::buildSpellPool(battle, *hero, receipt, recipients);
	if(pool.empty())
		return; // No draw and no activation spent without a useful legal spell.
	auto & rng = gameHandler->getRandomGenerator();
	const size_t spellIndex = pool.size() == 1 ? 0 : rng.nextInt(static_cast<int>(pool.size()) - 1);
	const auto & row = pool.at(spellIndex);
	const size_t targetIndex = row.targets.size() == 1 ? 0 : rng.nextInt(static_cast<int>(row.targets.size()) - 1);
	const auto selected = newHorizonsPassiveDivineMandate::selectSpell(pool, spellIndex, targetIndex);
	const auto action = newHorizonsPassiveDivineMandate::automaticSpellAction(battle,
		side, selected.spell, selected.target.aimUnitId);
	doHeroSpellAction(battle, action, receipt);
}

void BattleActionProcessor::castDivineFury(const CBattleInfoCallback & battle,
	const battle::Unit * source, const battle::Unit * target, BattleSide side, bool ordinaryPrimaryCreatureAttack,
	bool underOrderAtStrike)
{
	if(!source || !target || !ordinaryPrimaryCreatureAttack || !underOrderAtStrike
		|| !target->alive() || target->isGhost()
		|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| battle.sideToPlayer(side) == battle.battleGetOwner(target))
		return;
	const auto * hero = battle.battleGetFightingHero(side);
	const auto policy = newHorizonsPassiveDivineMandate::capturePolicy(hero);
	const auto holyWrathNumber = SpellID::decode("new-horizons:holyWrath");
	if(!hero || !policy.has(newHorizonsPassiveDivineMandate::Perk::DIVINE_FURY) || holyWrathNumber < 0)
		return;
	const auto holyWrath = SpellID(holyWrathNumber);
	const auto * spell = holyWrath.toSpell();
	const spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_FURY, 0};
	spells::BattleCast cast(&battle, hero, spells::Mode::HERO, spell);
	cast.setAutomaticHeroCast(receipt);
	const battle::Target aim{battle::Destination(target)};
	if(!newHorizonsPassiveDivineMandate::furyEligible(policy, underOrderAtStrike,
		ordinaryPrimaryCreatureAttack, target->alive() && battle.sideToPlayer(side) != battle.battleGetOwner(target),
		cast.mechanicsForTarget(aim)->canBeCastAt(aim)))
		return;
	if(gameHandler->getRandomGenerator().nextInt(99) >= newHorizonsPassiveDivineMandate::FURY_CHANCE_PERCENT)
		return;
	const auto action = newHorizonsPassiveDivineMandate::automaticSpellAction(battle, side, holyWrath, target->unitId());
	doHeroSpellAction(battle, action, receipt);
}
