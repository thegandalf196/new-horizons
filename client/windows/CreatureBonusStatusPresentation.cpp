/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CreatureBonusStatusPresentation.h"
#include "SylvanFactionStatusPresentation.h"
#include "SylvanStatusArt.h"
#include "SylvanAmbushStatusPresentation.h"
#include "WarcastStatusPresentation.h"
#include "CreatureAbilityGrouping.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../battle/ArcaneInspirationPresentation.h"
#include "../battle/CurrentFactionSkillPresentation.h"
#include "../../lib/CStack.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/mapObjects/army/CStackInstance.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/spells/HydrasVitalityIdentity.h"
#include "../../lib/callback/CCallback.h"
#include <boost/algorithm/string/replace.hpp>
#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace creatureBonusStatusPresentation
{
namespace
{
std::optional<Row> groupedRow(const IBonusBearer & bonusSource, const CStackInstance & stackNode,
	const std::vector<std::shared_ptr<Bonus>> & members)
{
	const auto & first = *members.front();
	creatureAbilityGrouping::Card card;
	if(first.type == BonusType::SPELL_IMMUNITY)
	{
		std::vector<std::string> spells;
		for(const auto & bonus : members)
			if(const auto * spell = bonus->subtype.as<SpellID>().toSpell())
				spells.push_back(spell->getNameTranslated());
		card = creatureAbilityGrouping::spellImmunity(spells);
	}
	else
	{
		std::vector<std::pair<int, std::string>> entries;
		for(const auto & bonus : members)
		{
			const int percent = bonusSource.valOfBonuses(bonus->type, bonus->subtype);
			if(bonus->type == BonusType::HATE)
			{
				if(const auto * creature = bonus->subtype.as<CreatureID>().toCreature())
					entries.emplace_back(percent, creature->getNamePluralTranslated());
			}
			else if(const auto * spell = bonus->subtype.as<SpellID>().toSpell())
				entries.emplace_back(percent, spell->getNameTranslated());
		}
		card = first.type == BonusType::HATE ? creatureAbilityGrouping::hatred(entries)
			: first.type == BonusType::SPELL_AFTER_ATTACK ? creatureAbilityGrouping::onHitEffects(entries)
			: creatureAbilityGrouping::spellVulnerability(entries);
	}
	if(card.description.empty())
		return std::nullopt;
	return Row{card.description, stackNode.bonusToGraphics(members.front()), first.source, card.tooltip};
}
}

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
	// Cards that differ only by target merge into one card per type when repeated.
	const auto groupable = [](const Bonus & bonus)
	{
		return !bonus.hidden && (bonus.type == BonusType::MORE_DAMAGE_FROM_SPELL
			|| bonus.type == BonusType::HATE || bonus.type == BonusType::SPELL_IMMUNITY
			|| bonus.type == BonusType::SPELL_AFTER_ATTACK);
	};
	std::map<BonusType, std::vector<std::shared_ptr<Bonus>>> groups;
	for(const auto & bonus : visibleBonuses)
		if(groupable(*bonus))
			groups[bonus->type].push_back(bonus);
	std::erase_if(groups, [](const auto & group) { return group.second.size() < 2; });

	std::vector<Row> rows;
	for(const auto & bonus : visibleBonuses)
	{
		// The supplemental row carries this marker's captured potency and lifetime.
		if(bonus->type == BonusType::NONE && consecratedArms::identity(bonus.get()))
			continue;
		if(const auto group = groups.find(bonus->type); group != groups.end())
		{
			if(group->second.empty())
				continue; // already emitted at the first member's position
			if(auto row = groupedRow(bonusSource, stackNode, group->second))
				rows.push_back(std::move(*row));
			group->second.clear();
			continue;
		}
		Row row;
		row.description = describe(bonus);
		row.imagePath = stackNode.bonusToGraphics(bonus);
		row.bonusSource = bonus->source;
		if(bonus->type == BonusType::PHYSICAL_DEFENSE && bonus->description.empty())
		{
			row.tooltip = row.description;
			row.description = LIBRARY->generaltexth->translate("new-horizons.creature.physicalDefense.compact");
			const auto value = bonusSource.valOfBonuses(bonus->type, bonus->subtype);
			boost::replace_all(row.description, "%value%", (value > 0 ? "+" : "") + std::to_string(value));
		}
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
	constexpr std::array<const char *, 6> prayerImages = {
		"NH_prayer_divine_purpose.png", "NH_prayer_dauntless_assault.png",
		"NH_prayer_steadfast_soul.png", "NH_prayer_vicarious_redemption.png",
		"NH_prayer_unbroken_vow.png", "NH_prayer_heavens_judgment.png"};
	const auto prayers = consecratedArms::activePrayers(*battleStack);
	const auto prayerTexts = passiveFactionSkillPresentation::prayerTexts(prayers);
	size_t textIndex = 0;
	for(const auto & prayer : prayers)
	{
		if(!prayer)
			continue;
		const auto & text = prayerTexts.at(textIndex++);
		Row status;
		status.description = text.value;
		status.tooltip = text.details;
		status.imagePath = ImagePath::builtin(prayerImages.at(static_cast<size_t>(prayer->prayer)));
		status.bonusSource = BonusSource::SECONDARY_SKILL;
		rows.push_back(std::move(status));
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
				const auto art = sylvanStatusArt::forRow(text);
				if(!art.image.empty())
					status.imagePath = ImagePath::builtin(art.image);
				status.tooltip = art.help;
				rows.push_back(std::move(status));
			}
		if(battle)
			for(auto & row : warcastStatusPresentation::rows(*battle, battleStack))
				rows.push_back({std::move(row.text), ImagePath::builtin(row.image), BonusSource::SECONDARY_SKILL,
					std::move(row.help)});
		for(auto & card : sylvanAmbushStatusPresentation::cards(battleStack,
			[](const std::string & key) { return LIBRARY->generaltexth->translate(key); }))
			rows.push_back({std::move(card.text), ImagePath::builtin(card.image), BonusSource::SECONDARY_SKILL,
				std::move(card.help)});
	}
	return rows;
}
}
