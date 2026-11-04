/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureWindow.h"
#include "NewHorizonsCreatureAbilityHelp.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../battle/ArcaneMarkPresentation.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/InfoAboutArmy.h"

void CStackWindow::appendInnateAbilityCards(const CStack * stack,
	const CStackInstance * strategic, const CCreature * creature)
{
	// Use the unit's own captured battle/world rules. A missing battle context
	// must not borrow the adventure profile, and a bare preview invents none.
	const JsonNode * abilityRules = nullptr;
	if(stack)
	{
		if(const auto * battleContext = stack->getBattle())
			abilityRules = &battleContext->getMagicRules();
	}
	else if(strategic && strategic->cb)
		abilityRules = &strategic->cb->getMagicRules();
	else if(GAME->interface() && GAME->interface()->cb)
		abilityRules = &GAME->interface()->cb->getMagicRules();
	auto innateAbilities = newHorizonsCreatureAbilityHelp::translatedInnateAbilities(
		creature->getJsonKey(), abilityRules,
		stack && stack->alive() ? stack->summonRoundsRemaining : 0);
	if(stack)
		if(const auto * battle = stack->getBattle())
			if(auto help = newHorizonsCreatureAbilityHelp::demonicGateReinforcement(
				stack->unitId(), battle->getSide(stack->unitSide()).gatedDemonicStacks))
				innateAbilities.push_back(std::move(*help));
	for(const auto & help : innateAbilities)
	{
		BonusInfo ability;
		ability.description = help.compact;
		ability.tooltip = help.details;
		ability.imagePath = help.iconPath.empty() ? ImagePath{} : ImagePath::builtin(help.iconPath);
		ability.bonusSource = BonusSource::CREATURE_ABILITY;
		activeBonuses.push_back(std::move(ability));
	}

	if(stack)
	{
		for(const auto & marks : arcaneMarkPresentation::forStack(*stack))
		{
			BonusInfo ability;
			ability.description = marks.compact;
			ability.tooltip = marks.details;
			ability.imagePath = ImagePath::builtin(arcaneMarkPresentation::MEDIUM_ICON);
			ability.bonusSource = BonusSource::SECONDARY_SKILL;
			activeBonuses.push_back(std::move(ability));
		}
	}

}
