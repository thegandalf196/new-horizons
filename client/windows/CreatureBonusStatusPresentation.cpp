/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CreatureBonusStatusPresentation.h"
#include "SylvanFactionStatusPresentation.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "../../lib/callback/CCallback.h"
#include <algorithm>

namespace creatureBonusStatusPresentation
{
bool isHydrasVitality(const Bonus & bonus)
{
	return bonus.type == BonusType::HP_REGENERATION
		&& bonus.source == BonusSource::SPELL_EFFECT
		&& bonus.sid.toString() == "new-horizons:hydrasVitality";
}

std::string hydrasVitalityText(const CStack * battleStack, const Bonus & marker)
{
	if(!battleStack)
		return {};
	const auto state = battleStack->acquireState();
	if(!state)
		return {};
	return "Hydra's Vitality: " + std::to_string(battleStack->getMaxHealth())
		+ " max HP/creature; +" + std::to_string(state->capacityRegenerationProjectedHeal())
		+ " aggregate HP next activation; " + std::to_string(marker.turnsRemain) + " rounds left.";
}

std::vector<Row> supplementalRows(const CStack * battleStack, const CStackInstance * stackNode,
	const BonusList & visibleBonuses, CCallback * callback)
{
	if(!battleStack || !battleStack->getBattle())
		return {};
	std::vector<Row> rows;
	const auto hydrasVitalityMarkers = battleStack->getBonuses(
		newHorizonsMagic::hydrasVitalitySource(battleStack->getBattle()->getMagicRules())
			.And(Selector::type()(BonusType::HP_REGENERATION)));
	const bool hydraMarkerAlreadySelected = std::any_of(visibleBonuses.begin(), visibleBonuses.end(), [](const auto & bonus)
	{
		return !bonus->hidden && isHydrasVitality(*bonus);
	});
	if(!hydrasVitalityMarkers->empty() && !hydraMarkerAlreadySelected)
	{
		Row status;
		status.description = hydrasVitalityText(battleStack, *hydrasVitalityMarkers->front());
		if(!status.description.empty())
		{
			if(stackNode)
				status.imagePath = stackNode->bonusToGraphics(hydrasVitalityMarkers->front());
			status.bonusSource = BonusSource::SPELL_EFFECT;
			rows.push_back(std::move(status));
		}
	}

	if(battleStack->getBattle() && callback)
	{
		const auto battle = callback->getBattle(battleStack->getBattle()->getBattleID());
		if(battle)
			for(const auto & text : newHorizonsSylvanFactionPresentation::currentRows(*battle, battleStack))
			{
				Row status;
				status.description = text;
				status.bonusSource = BonusSource::SECONDARY_SKILL;
				rows.push_back(std::move(status));
			}
	}
	return rows;
}
}
