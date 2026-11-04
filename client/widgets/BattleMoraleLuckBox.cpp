/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MiscWidgets.h"
#include "CComponent.h"
#include "TextControls.h"
#include "Images.h"
#include "../GameInstance.h"
#include "../CPlayerInterface.h"
#include "../battle/NewHorizonsBattleStatus.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/PassiveDivineMandate.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/MetaString.h"

namespace
{
void appendOrderSource(std::vector<std::string> & descriptions,
	const CBattleInfoCallback & battle, const CStack & stack, bool morale)
{
	const auto riders = newHorizonsPassiveDivineMandate::activeOrderRiders(battle, &stack);
	const int value = morale ? riders.morale : riders.luck;
	if(value != 0)
		descriptions.push_back("Sacred Command: +" + std::to_string(value) + (morale ? " Morale" : " Luck"));
}
}

void MoraleLuckBox::set(const CStack * stack, const CBattleInfoCallback * battleCallback)
{
	set(static_cast<const AFactionMember *>(stack));
	if(!stack || !battleCallback || !battleCallback->getBattle()
		|| !newHorizonsMagic::rulesActive(battleCallback->getBattle()->getMagicRules()))
	{
		return;
	}

	if(morale)
		setBattleMorale(stack, battleCallback);
	else
		setBattleLuck(stack, battleCallback);
}

void MoraleLuckBox::setBattleLuck(const CStack * stack, const CBattleInfoCallback * battleCallback)
{
	const int32_t ordinaryAttackLuck = battleCallback->battleGetAttackLuck(stack, nullptr, false, false);
	const bool noLuck = stack->hasBonusOfType(BonusType::NO_LUCK);
	const bool maxLuck = stack->hasBonusOfType(BonusType::MAX_LUCK);
	const bool maximumLuckLimitPresent = stack->hasBonusOfType(BonusType::MAXIMUM_LUCK);
	std::vector<std::string> bonusDescriptions;
	const auto * descriptionCallback = GAME->interface() ? GAME->interface()->cb.get() : nullptr;
	if(descriptionCallback)
	{
		TConstBonusListPtr bonuses;
		stack->luckValAndBonusList(bonuses);
		if(bonuses)
		{
			for(const auto & bonus : *bonuses)
			{
				if(!bonus || bonus->val == 0)
					continue;

				auto bonusDescription = bonus->Description(descriptionCallback);
				if(!bonusDescription.empty())
					bonusDescriptions.push_back(std::move(bonusDescription));
			}
		}
	}

	appendOrderSource(bonusDescriptions, *battleCallback, *stack, false);
	const auto readback = newHorizonsBattleStatus::makeBattleLuckReadback(true, ordinaryAttackLuck,
		noLuck, maxLuck, maximumLuckLimitPresent, stack->valOfBonuses(BonusType::MAXIMUM_LUCK),
		std::move(bonusDescriptions));
	component.value = readback.ordinaryAttackLuck;
	image->setFrame(std::clamp(readback.ordinaryAttackLuck + 3, 0, 6));
	if(label)
		label->setText(std::to_string(readback.ordinaryAttackLuck));

	const int luckSign = (readback.ordinaryAttackLuck > 0) - (readback.ordinaryAttackLuck < 0);
	hoverText = GAME->translator().translate("core.heroscrn", 7 - luckSign);

	MetaString description;
	description.appendTextID("new-horizons.combat.luck.readback.value");
	description.replaceTokenNumber("%VALUE%", readback.ordinaryAttackLuck);
	if(readback.maxLuck)
	{
		description.appendEOL();
		description.appendTextID(readback.noLuck
			? "new-horizons.combat.luck.readback.maxLuckNoLuck"
			: "new-horizons.combat.luck.readback.maxLuck");
	}
	else if(readback.noLuck)
	{
		description.appendEOL();
		description.appendTextID("new-horizons.combat.luck.readback.noLuck");
	}
	if(readback.maximumLuckLimitPresent && (!readback.noLuck || readback.maxLuck))
	{
		MetaString limit;
		limit.appendTextID("new-horizons.combat.luck.readback.maximumLuckLimit");
		limit.replaceTokenNumber("%VALUE%", readback.maximumLuckLimit);
		description.appendEOL();
		description.append(limit);
	}
	if(readback.hasSources())
	{
		description.appendEOL();
		description.appendEOL();
		description.appendTextID("new-horizons.combat.luck.readback.sourceHeader");
		for(const auto & bonusDescription : readback.bonusDescriptions)
		{
			description.appendEOL();
			description.appendRawString(bonusDescription);
		}
	}
	else if(!readback.noLuck && !readback.maxLuck)
	{
		description.appendEOL();
		description.appendTextID("new-horizons.combat.luck.readback.noSources");
	}

	const auto ownerSide = battleCallback->playerToSide(battleCallback->battleGetOwner(stack));
	if((ownerSide == BattleSide::ATTACKER || ownerSide == BattleSide::DEFENDER)
		&& (!readback.noLuck || readback.maxLuck)
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(stack))
	{
		// This callback is player-scoped; the fighting-hero query returns null
		// for a hidden opposing hero rather than exposing that hero's perk state.
		const auto * hero = battleCallback->battleGetFightingHero(ownerSide);
		if(hero)
		{
			const auto & heroRules = hero->getCapabilityRules();
			if(newHorizonsHeroes::usesRules(heroRules) && heroRules["rulesetVersion"].Integer() >= 3
				&& hero->getPerkSkillRank(std::string("new-horizons:sylvanLuck")) > 0)
			{
				const auto sylvanLuck = newHorizonsBattleStatus::makeSylvanLuckStackStatus(
					battleCallback->getBattle()->getSylvanLuckState(ownerSide), stack->unitId(),
					readback.ordinaryAttackLuck, true, stack->isShooter());
				const auto sylvanTooltip = newHorizonsBattleStatus::sylvanLuckStackTooltip(sylvanLuck,
					LIBRARY->generaltexth->translate("skill.new-horizons.sylvanLuck.name"),
					"Serendipity", "Forest's Favor", "Shared Fortune", "Cascading Fortune", "Fortunate Aim");
				if(!sylvanTooltip.empty())
				{
					description.appendEOL();
					description.appendEOL();
					description.appendRawString(sylvanTooltip);
				}
			}
		}
	}
	text = description.toString(&GAME->translator());
	return;
}

void MoraleLuckBox::setBattleMorale(const CStack * stack, const CBattleInfoCallback * battleCallback)
{
	const auto readback = battleCallback->battleGetMoraleInfo(stack);
	component.value = readback.effective;
	image->setFrame(std::clamp(readback.effective + 3, 0, 6));
	if(label)
		label->setText(std::to_string(readback.effective));

	const int moraleSign = (readback.effective > 0) - (readback.effective < 0);
	hoverText = GAME->translator().translate("core.heroscrn", 4 - moraleSign);

	MetaString description;
	description.appendTextID("new-horizons.combat.morale.readback.values");
	description.replaceTokenNumber("%REAL%", readback.real);
	description.replaceTokenNumber("%EFFECTIVE%", readback.effective);

	if(stack->unaffectedByMorale())
	{
		description.appendEOL();
		description.appendTextID("core.arraytxt", 113);
		text = description.toString(&GAME->translator());
		return;
	}

	std::vector<std::string> bonusDescriptions;
	const auto * descriptionCallback = GAME->interface() ? GAME->interface()->cb.get() : nullptr;
	if(descriptionCallback)
	{
		const auto bonuses = stack->getBonusesOfType(BonusType::MORALE);
		if(bonuses)
		{
			for(const auto & bonus : *bonuses)
			{
				if(!bonus || bonus->val == 0)
					continue;

				auto bonusDescription = bonus->Description(descriptionCallback);
				if(!bonusDescription.empty())
					bonusDescriptions.push_back(std::move(bonusDescription));
			}
		}
	}

	appendOrderSource(bonusDescriptions, *battleCallback, *stack, true);
	const bool hasSources = !bonusDescriptions.empty() || readback.standardBearerBonus != 0
		|| readback.firstRoundModifier != 0 || readback.steadfastAdjustment != 0
		|| readback.espritDeCorpsAdjustment != 0
		|| readback.commandingPresenceFloorApplied || readback.furyUnboundFloorApplied;
	description.appendEOL();
	if(!hasSources)
	{
		description.appendTextID("new-horizons.combat.morale.readback.noSources");
		text = description.toString(&GAME->translator());
		return;
	}

	description.appendEOL();
	description.appendTextID("new-horizons.combat.morale.readback.sourceHeader");
	for(const auto & bonusDescription : bonusDescriptions)
	{
		description.appendEOL();
		description.appendRawString(bonusDescription);
	}
	const auto appendModifier = [&description](std::string_view textId, int32_t value)
	{
		MetaString line;
		line.appendTextID(std::string(textId));
		line.replaceTokenNumber("%VALUE%", value);
		description.appendEOL();
		description.append(line);
	};
	if(readback.standardBearerBonus != 0)
		appendModifier("new-horizons.combat.morale.readback.standardBearer", readback.standardBearerBonus);
	if(readback.firstRoundModifier != 0)
		appendModifier("new-horizons.combat.morale.readback.firstRound", readback.firstRoundModifier);
	if(readback.steadfastAdjustment != 0)
		appendModifier("new-horizons.combat.morale.readback.steadfast", readback.steadfastAdjustment);
	if(readback.espritDeCorpsAdjustment != 0)
		appendModifier("new-horizons.combat.morale.readback.espritDeCorps", readback.espritDeCorpsAdjustment);
	if(readback.commandingPresenceFloorApplied)
	{
		description.appendEOL();
		description.appendTextID("new-horizons.combat.morale.readback.commandingPresence");
	}
	if(readback.furyUnboundFloorApplied)
	{
		description.appendEOL();
		description.appendTextID("new-horizons.combat.morale.readback.furyUnbound");
	}
	text = description.toString(&GAME->translator());
}
