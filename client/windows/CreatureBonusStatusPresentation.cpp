/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CreatureBonusStatusPresentation.h"
#include "SylvanFactionStatusPresentation.h"
#include "../battle/ArcaneInspirationPresentation.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "../../lib/callback/CCallback.h"
#include <algorithm>

namespace creatureBonusStatusPresentation
{
std::vector<Row> visibleRows(const IBonusBearer & bonusSource, const CStackInstance & stackNode,
	const BonusList & visibleBonuses,
	const std::function<std::string(const std::shared_ptr<Bonus> &)> & describe)
{
	// Match the effective ability selected by creature spell targeting, including
	// legacy allied-only policies. Creature identity is not an ability contract.
	const auto randomSpellcaster = bonusSource.getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	bool hostileWishes = false;
	if(randomSpellcaster && randomSpellcaster->parameters)
	{
		const auto policy = randomSpellcaster->parameters->toJsonNode();
		hostileWishes = policy.getType() == JsonNode::JsonType::DATA_INTEGER && policy.Integer() == 1;
	}
	std::vector<Row> rows;
	for(const auto & bonus : visibleBonuses)
	{
		Row row;
		row.description = describe(bonus);
		row.imagePath = stackNode.bonusToGraphics(bonus);
		row.bonusSource = bonus->source;
		if(bonus->type == BonusType::RANDOM_SPELLCASTER && randomSpellcaster && !randomSpellcaster->hidden)
		{
			const std::string prefix = hostileWishes
				? "new-horizons.creature.wishes.hostile" : "new-horizons.creature.wishes.allied";
			row.description = LIBRARY->generaltexth->translate(prefix + ".compact");
			row.tooltip = LIBRARY->generaltexth->translate(prefix + ".help");
		}
		if(!row.description.empty() && !bonus->hidden)
			rows.push_back(std::move(row));
	}
	return rows;
}

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
		{
			const auto inspiration = arcaneInspirationPresentation::forStack(*battleStack, battle.get());
			if(inspiration.active())
				rows.push_back({"Arcane Inspiration / Spellshock", ImagePath::builtin(arcaneInspirationPresentation::SMALL_ICON),
					BonusSource::HERO_SPECIAL, inspiration.details});
		}
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
