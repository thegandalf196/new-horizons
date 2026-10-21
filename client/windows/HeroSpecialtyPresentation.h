/* Part of VCMI / New Horizons; GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

struct HeroSpecialtyPresentation
{
	std::string name;
	std::string description;
	std::optional<CreatureID> creature;
	size_t prototypeFrame = 0;
	std::optional<SpellID> spell;
	std::optional<SecondarySkill> skill;
	std::optional<std::string> artwork;
	AnimationPath animation(bool small = false) const
	{
		if(artwork)
			return AnimationPath::builtin(*artwork + (small ? "/32" : "/44"));
		if(creature)
			return AnimationPath::builtin("CPRSMALL");
		// SPELLSCR uses the same registered index at both sizes. SPELLINT
		// would require index+1, which these existing callers do not request.
		if(spell)
			return AnimationPath::builtin("SPELLSCR");
		if(skill)
			return AnimationPath::builtin(small ? "SECSK32" : "SECSKILL");
		return AnimationPath::builtin(small ? "UN32" : "UN44");
	}
	size_t frame() const
	{
		if(artwork)
			return 0;
		if(creature)
			return creature->toCreature()->getIconIndex();
		if(spell)
			return spell->toSpell()->getIconIndex();
		if(skill)
			return skill->toEntity(LIBRARY)->getIconIndex();
		return prototypeFrame;
	}
};

inline HeroSpecialtyPresentation heroSpecialtyPresentation(const CHero & hero,
	const newHorizonsHeroes::SpecialtyPresentation & metadata)
{
	HeroSpecialtyPresentation result;
	result.name = metadata.name;
	result.description = metadata.description;
	result.prototypeFrame = static_cast<size_t>(hero.imageIndex);
	result.spell = metadata.spell;
	result.skill = metadata.skill;
	result.artwork = metadata.artwork;
	// The unchanged creature alias already has its native specialty art.
	// Only a changed target (Wisp) needs the replacement creature portrait.
	if(metadata.creature && hero.creatureLineSpecialtyAlias
		&& *metadata.creature != hero.creatureLineSpecialtyAlias->creature)
		result.creature = metadata.creature;
	return result;
}

// Explicit read-only context: an absent/captured legacy target never consults
// installed defaults. The original prototype remains the compatibility alias.
inline HeroSpecialtyPresentation heroSpecialtyPresentation(const CHero & hero,
	const JsonNode & development, std::optional<CreatureID> target, const JsonNode & magic = JsonNode())
{
	return heroSpecialtyPresentation(hero,
		newHorizonsHeroes::resolveSpecialtyPresentation(hero, development, magic, target));
}

inline HeroSpecialtyPresentation heroSpecialtyPresentation(const CGHeroInstance & hero)
{
	return heroSpecialtyPresentation(*hero.getHeroType(), hero.getSpecialtyPresentation());
}
