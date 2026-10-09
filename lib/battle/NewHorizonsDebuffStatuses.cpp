/*
 * NewHorizonsDebuffStatuses.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsDebuffStatuses.h"

#include "Unit.h"
#include "CUnitState.h"
#include "NewHorizonsOffense.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../GameLibrary.h"
#include "../GameConstants.h"
#include "../json/JsonBonus.h"
#include "../spells/CSpell.h"
#include <vcmi/spells/Service.h>

#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace newHorizonsDebuffStatuses
{
namespace
{
bool tagged(const Bonus & bonus)
{
	return std::ranges::find(bonus.statusTags, BonusStatusTag::DEBUFF) != bonus.statusTags.end();
}

const CSpell * sourceSpell(const Bonus & bonus)
{
	if(bonus.source != BonusSource::SPELL_EFFECT || !LIBRARY || !LIBRARY->spells())
		return nullptr;
	const auto id = bonus.sid.as<SpellID>();
	if(!id.hasValue())
		return nullptr;
	const CSpell * result = nullptr;
	LIBRARY->spells()->forEach([&](const spells::Spell * spell, bool & stop)
	{
		if(spell->getId() == id)
		{
			result = dynamic_cast<const CSpell *>(spell);
			stop = true;
		}
	});
	return result;
}

void registeredIdentities(const JsonNode & node, const Bonus & legacy, std::set<std::string> & result)
{
	if(node.isStruct())
	{
		// Inspect only authored Bonus objects, not spell polarity or arbitrary
		// negative values. The typed component survives historical scaling.
		if(!node["type"].isNull() && !node["statusIdentity"].isNull())
		{
			const auto authored = JsonUtils::parseBonus(node);
			if(authored && tagged(*authored) && authored->hasValidStatusMetadata()
				&& !authored->statusIdentity.empty() && authored->type == legacy.type
				&& authored->subtype == legacy.subtype)
				result.insert(authored->statusIdentity);
		}
		for(const auto & [name, child] : node.Struct())
			registeredIdentities(child, legacy, result);
	}
	else if(node.isVector())
		for(const auto & child : node.Vector())
			registeredIdentities(child, legacy, result);
}

struct ProducerComponent
{
	const char * spell;
	BonusType type;
	const char * subtype = "";
};

// Authored runtime components have no Bonus template in registered spell JSON.
// Keep this producer metadata beside the native stamping API, not a consumer
// spell-name counting whitelist. All lookups require typed SPELL_EFFECT IDs and
// the exact emitted component type/subtype. Lua producers emit these same IDs.
constexpr std::array PRODUCER_COMPONENTS{
	ProducerComponent{"new-horizons:hexOfPain", BonusType::COMBAT_EVENT_TRIGGER, "core:hexOfPain"},
	ProducerComponent{"new-horizons:plague", BonusType::COMBAT_EVENT_TRIGGER, "core:plagueStatus"},
	ProducerComponent{"new-horizons:soulChain", BonusType::COMBAT_EVENT_TRIGGER, "core:soulChainStatus"},
	ProducerComponent{"new-horizons:frailty", BonusType::PRIMARY_SKILL, "defence"},
	ProducerComponent{"new-horizons:doom", BonusType::GENERAL_ATTACK_REDUCTION},
	ProducerComponent{"new-horizons:doom", BonusType::STACKS_SPEED},
	ProducerComponent{"new-horizons:doom", BonusType::STACKS_INITIATIVE},
	ProducerComponent{"new-horizons:doom", BonusType::MORALE},
	ProducerComponent{"new-horizons:timeStop", BonusType::NOT_ACTIVE},
	ProducerComponent{"new-horizons:timeStop", BonusType::INVINCIBLE},
	ProducerComponent{"new-horizons:timeStop", BonusType::TIME_STOP},
	ProducerComponent{"new-horizons:puppetMaster", BonusType::PUPPET_MASTER_CONTROL},
	ProducerComponent{"core:forgetfulness", BonusType::CREATURE_ABILITY_SUPPRESSION},
	ProducerComponent{"core:misfortune", BonusType::FAVORABLE_CREATURE_CHANCE_MULTIPLIER_BASIS_POINTS},
	ProducerComponent{"core:slow", BonusType::STACKS_INITIATIVE}
};

std::string runtimeIdentity(const Bonus & bonus, const CSpell * spell)
{
	if(!spell || bonus.source != BonusSource::SPELL_EFFECT || bonus.sid.as<SpellID>() != spell->getId())
		return {};
	for(const auto & component : PRODUCER_COMPONENTS)
	{
		if(bonus.type != component.type)
			continue;
		const auto * registered = LIBRARY->spells()->getByName(component.spell);
		if(!registered || registered->getId() != spell->getId())
			continue;
		BonusSubtypeID subtype;
		if(component.type == BonusType::COMBAT_EVENT_TRIGGER)
			subtype = ScriptID(ScriptID::decode(component.subtype));
		else if(component.type == BonusType::PRIMARY_SKILL)
			subtype = PrimarySkill(PrimarySkill::DEFENSE);
		if(bonus.subtype == subtype)
			return spell->getJsonKey();
	}
	return {};
}

bool legacyHostileLock(const Bonus & bonus, const CSpell * spell, const battle::Unit & unit)
{
	if(!spell || bonus.source != BonusSource::SPELL_EFFECT
		|| (bonus.type != BonusType::NONE && bonus.type != BonusType::MAGIC_RESISTANCE))
		return false;
	const auto * registered = LIBRARY->spells()->getByName("new-horizons:spellLock");
	if(!registered || registered->getId() != spell->getId())
		return false;
	const auto markers = unit.getBonuses(Selector::source(BonusSource::SPELL_EFFECT, bonus.sid)
		.And(Selector::type()(BonusType::NONE)));
	// This is the exact authored polarity encoding of Spell Lock's typed marker,
	// not a general negative-stat heuristic. Positive means protective/friendly.
	return markers && std::ranges::any_of(*markers, [](const auto & marker)
	{
		return marker && marker->val < 0;
	});
}
}

void applyProducerMetadata(Bonus & bonus)
{
	if(bonus.hasStatusMetadata())
		return;
	const auto identity = runtimeIdentity(bonus, sourceSpell(bonus));
	if(!identity.empty())
	{
		bonus.statusTags = {BonusStatusTag::DEBUFF};
		bonus.statusIdentity = identity;
	}
}

StatusSnapshot snapshot(const battle::Unit & unit)
{
	std::set<std::string> distinct;
	const auto bonuses = unit.getBonuses(Selector::all);
	if(bonuses)
		for(const auto & bonus : *bonuses)
		{
			if(!bonus)
				continue;
			if(!bonus->hasValidStatusMetadata())
				throw std::invalid_argument("Invalid authored status metadata");
			if(tagged(*bonus) && !bonus->statusIdentity.empty())
			{
				distinct.insert(bonus->statusIdentity);
				continue;
			}
			if(newHorizonsOffense::isNoQuarterBonus(bonus.get()))
			{
				distinct.insert(newHorizonsOffense::NO_QUARTER);
				continue;
			}
			const auto * spell = sourceSpell(*bonus);
			std::set<std::string> authored;
			const auto runtime = runtimeIdentity(*bonus, spell);
			if(!runtime.empty())
				authored.insert(runtime);
			if(legacyHostileLock(*bonus, spell, unit))
				authored.insert(spell->getJsonKey());
			if(spell)
				for(int rank = 0; rank < GameConstants::SPELL_SCHOOL_LEVELS; ++rank)
				{
					const auto & level = spell->getLevelInfo(rank);
					registeredIdentities(level.effects, *bonus, authored);
					registeredIdentities(level.cumulativeEffects, *bonus, authored);
					registeredIdentities(level.battleEffects, *bonus, authored);
				}
			distinct.insert(authored.begin(), authored.end());
			if(tagged(*bonus) && authored.empty())
				// Historical tag-only records were legal. Group their components by
				// typed source, without inventing a status from raw type/sign/duration.
				distinct.insert(spell ? spell->getJsonKey() : "legacy:"
					+ std::to_string(static_cast<int>(bonus->source)) + ":" + bonus->sid.toString());
		}

	// This physical affliction is already represented by authoritative typed
	// unit state rather than a Bonus. Its producer owns the Poison identity;
	// sharing that identity with tagged Poison bonuses prevents double counting.
	const auto state = unit.acquireState();
	if(state && state->physicalPoisonBaseDamage > 0 && state->physicalPoisonActivationsRemaining > 0)
		distinct.insert(POISON_IDENTITY);

	return {{distinct.begin(), distinct.end()}};
}
}
