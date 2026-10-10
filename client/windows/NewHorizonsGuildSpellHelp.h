/*
 * NewHorizonsGuildSpellHelp.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/MetaString.h"

namespace newHorizonsGuildSpellHelp
{
/// Presentation only. The visitor's saved rules and ordinary learning legality
/// remain authoritative; a known spell is never described as School-locked.
inline MetaString acquisitionRequirement(const JsonNode & worldRules,
	const CSpell * spell, const CGHeroInstance * visitor)
{
	if(!spell)
		return {};
	const auto & rules = visitor ? visitor->getMagicRules() : worldRules;
	if(!newHorizonsMagic::rulesActive(rules))
		return {};
	if(visitor)
	{
		if(visitor->spellbookContainsSpell(spell->getId()))
			return {};
		const auto status = visitor->getSpellLearningStatus(spell);
		if(status != CGHeroInstance::SpellLearningStatus::LEARNABLE
			&& status != CGHeroInstance::SpellLearningStatus::INSUFFICIENT_SCHOOL)
			return {}; // Do not mislabel another acquisition restriction.
	}
	const auto rank = newHorizonsMagic::requiredSchoolRank(rules, spell->getId());
	const auto skills = newHorizonsMagic::spellSchoolSkills(rules, spell->getId());
	if(rank <= 0 || skills.empty())
		return {};

	MetaString reason;
	reason.appendTextID("new-horizons.adventure.spellLearning.requires");
	reason.appendTextID("core.skilllev", rank - 1);
	reason.appendTextID("new-horizons.adventure.spellLearning.proficiencyIn");
	for(size_t index = 0; index < skills.size(); ++index)
	{
		if(index > 0)
			reason.appendTextID("new-horizons.adventure.spellLearning.or");
		reason.appendTextID(LIBRARY->skillh->getById(skills[index])->getNameTextID());
	}
	return reason;
}
}
