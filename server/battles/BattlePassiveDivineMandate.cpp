/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include "../../lib/battle/ConsecratedArmsOrder.h"
#include "../../lib/battle/PassiveDivineMandateSpellPool.h"
#include "../../lib/battle/HeroOrderRecipients.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
void presentPrayerGrant(CGameHandler & handler, const CBattleInfoCallback & battle,
	const SetStackEffect & effects, const std::vector<consecratedArms::Prayer> & prayers,
	const consecratedArms::Policy & policy, int faith, std::optional<uint32_t> onlyRecipient)
{
	MetaString recipients;
	BattleAnimationPlayed animation;
	animation.battleID = battle.getBattle()->getBattleID();
	for(const auto & [id, bonuses] : effects.toAdd)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost() || bonuses.empty()
			|| (onlyRecipient && id != *onlyRecipient))
			continue;
		recipients.appendRawString("%d %s");
		recipients.replaceNumber(unit->getCount());
		recipients.replaceName(unit->creatureId(), unit->getCount());
		animation.targets.push_back({static_cast<int32_t>(id), unit->getPosition()});
	}
	if(animation.targets.empty())
		return;

	BattleLogMessage message;
	message.battleID = animation.battleID;
	const auto names = recipients.buildList(LIBRARY->staticTexts());
	for(const auto prayer : prayers)
	{
		auto line = MetaString::createFromTextID("new-horizons.logs.consecratedArmsGranted");
		line.replaceTextID(consecratedArms::textID(prayer));
		line.replaceNumber(consecratedArms::potencyFaith(faith, policy));
		line.replaceNumber(consecratedArms::duration(faith, policy));
		line.replaceRawString(names);
		message.lines.push_back(std::move(line));
	}
	handler.sendAndApply(message);

	// Reuse Prayer's visual metadata without casting a spell or adding its effects.
	const auto * prayer = SpellID(SpellID::PRAYER).toSpell();
	if(!prayer)
		return;
	animation.sound = prayer->getCastSound();
	for(const auto & visual : prayer->animationInfo.affect)
	{
		if(visual.resourceName.empty())
			continue;
		animation.animation = visual.resourceName;
		animation.transparency = visual.transparency;
		handler.sendAndApply(animation);
		animation.sound = AudioPath();
	}
}
}

void BattleActionProcessor::grantConsecratedPrayers(const CBattleInfoCallback & battle,
	const BattleAction & action, const HeroOrderState & order)
{
	const auto side = action.side;
	const auto * issuingHero = battle.battleGetFightingHero(side);
	const auto policy = consecratedArms::capturePolicy(issuingHero);
	const int faith = consecratedArms::actualFaith(*issuingHero);
	const auto recipients = heroCommands::recipientIds(battle, side, order);
	if(policy.chosen)
	{
		// Version 4: admitted before StartAction, so no draw. Empty only without a recipient.
		const auto prayers = consecratedArms::chosenPrayerList(action.prayerChoices);
		if(prayers.empty())
			return;
		auto effects = consecratedArms::chosenOrderEffects(battle, side, recipients,
			action.prayerRecipient, prayers, policy, faith);
		gameHandler->sendAndApply(effects);
		presentPrayerGrant(*gameHandler, battle, effects, prayers, policy, faith, action.prayerRecipient);
		return;
	}
	auto & rng = gameHandler->getRandomGenerator();
	const int first = rng.nextInt(5);
	const int second = policy.has(consecratedArms::Perk::ANTIPHONAL_PRAYER) ? rng.nextInt(4) : 0;
	const auto prayers = consecratedArms::selectPrayers(policy, first, second);
	auto effects = consecratedArms::orderEffects(battle, side, recipients, prayers, policy, faith);
	gameHandler->sendAndApply(effects);
	presentPrayerGrant(*gameHandler, battle, effects, prayers, policy, faith, std::nullopt);
}

void BattleActionProcessor::castPassiveDivineMandate(const CBattleInfoCallback & battle,
	const BattleAction & issued, const HeroOrderState & order)
{
	const auto side = issued.side;
	if(consecratedArms::capturePolicy(battle.battleGetFightingHero(side)).rank)
	{
		grantConsecratedPrayers(battle, issued, order);
		return;
	}
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
