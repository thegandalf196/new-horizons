/* Part of VCMI engine. GPL-2.0-or-later; authors listed in AUTHORS. */
#include "StdInc.h"
#include "AttackAbilityQueries.h"
#include "../bonuses/IBonusBearer.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/CSpell.h"
#include <algorithm>
#include <sstream>
std::vector<SpellID> attackAbilityQueries::activeSpells(const IBonusBearer & target)
{
	std::vector<SpellID> ret;

	std::stringstream cachingStr;
	cachingStr << "!type_" << vstd::to_underlying(BonusType::NONE) << "source_" << vstd::to_underlying(BonusSource::SPELL_EFFECT);
	CSelector selector = Selector::sourceType()(BonusSource::SPELL_EFFECT)
						 .And(CSelector([](const Bonus * b)->bool
	{
		return b->type != BonusType::NONE && b->sid.as<SpellID>().toSpell() && !b->sid.as<SpellID>().toSpell()->isAdventure();
	}));

	TConstBonusListPtr spellEffects = target.getBonuses(selector, cachingStr.str());
	for(const auto & it : *spellEffects)
	{
		if(!vstd::contains(ret, it->sid.as<SpellID>()))  //do not duplicate spells with multiple effects
			ret.push_back(it->sid.as<SpellID>());
	}

	return ret;
}

std::set<SpellID> attackAbilityQueries::spellsForAttackCasting(const TConstBonusListPtr & spells, const IBonusBearer & defender)
{
	std::set<SpellID> spellsToCast;
	constexpr int unlayeredItemsInternalLayer = -1;

	std::map<int, std::vector<std::shared_ptr<Bonus>>> spellsWithBackupLayers;

	for(int i = 0; i < spells->size(); i++)
	{
		std::shared_ptr<Bonus> bonus = (*spells)[i];
		int layer = bonus->parameters ? bonus->parameters->toVector()[2] : -1;
		vstd::amax(layer, -1);
		spellsWithBackupLayers[layer].push_back(bonus);
	}

	auto addSpellsFromLayer = [&](int layer) -> void
	{
		assert(spellsWithBackupLayers.find(layer) != spellsWithBackupLayers.end());

		for(const auto & spell : spellsWithBackupLayers[layer])
		{
			if (spell->subtype.as<SpellID>() != SpellID())
				spellsToCast.insert(spell->subtype.as<SpellID>());
			else
				logGlobal->error("Invalid spell to cast during attack!");
		}
	};

	if(spellsWithBackupLayers.find(unlayeredItemsInternalLayer) != spellsWithBackupLayers.end())
	{
		addSpellsFromLayer(unlayeredItemsInternalLayer);
		spellsWithBackupLayers.erase(unlayeredItemsInternalLayer);
	}

	for(auto item : spellsWithBackupLayers)
	{
		bool areCurrentLayerSpellsApplied = std::all_of(item.second.begin(), item.second.end(),
			[&](const std::shared_ptr<Bonus> spell)
			{
				std::vector<SpellID> activeSpells = attackAbilityQueries::activeSpells(defender);
				return vstd::find(activeSpells, spell->subtype.as<SpellID>()) != activeSpells.end();
			});

		if(!areCurrentLayerSpellsApplied || item.first == spellsWithBackupLayers.rbegin()->first)
		{
			addSpellsFromLayer(item.first);
			break;
		}
	}

	return spellsToCast;
}

attackAbilityQueries::Parameters attackAbilityQueries::parameters(const IBonusBearer & source,
	BonusType attackMode, SpellID spellID, bool ranged)
{
	Parameters result;
	result.queryOwner = source.getBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
	for(const auto & sf : *result.queryOwner)
	{
		int meleeRanged = -1;
		if (sf->parameters)
		{
			vstd::amax(result.spellLevel, sf->parameters->toVector()[0]);
			meleeRanged = sf->parameters->toVector()[1];
		}

		if (meleeRanged == -1 || meleeRanged == 0 || (meleeRanged == 1 && ranged) || (meleeRanged == 2 && !ranged))
			result.castMe = true;
	}
	result.chance = source.valOfBonuses(Selector::typeSubtype(attackMode, BonusSubtypeID(spellID)));
	vstd::amin(result.chance, 100);
	return result;
}
