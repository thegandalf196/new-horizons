/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsAssimilation.h"
#include "../../CCreatureHandler.h"
#include "../../bonuses/Bonus.h"
#include "../../bonuses/BonusParameters.h"
#include "../../bonuses/Limiters.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../hero/CHero.h"
#include "../hero/NewHorizonsHeroRules.h"

namespace newHorizonsCreatures
{
namespace
{
bool matchesCreatureLine(CreatureID family, CreatureID creature, bool includeUpgrades = true)
{
	const auto * source = family.hasValue() ? family.toCreature() : nullptr;
	const auto * candidate = creature.hasValue() ? creature.toCreature() : nullptr;
	return source && candidate && (source == candidate
		|| (includeUpgrades && source->isMyDirectOrIndirectUpgrade(candidate)));
}

bool matchesAuthoredCreatureSpecialty(const CHero & hero, CreatureID creature)
{
	for(const auto & bonus : hero.specialty)
	{
		if(!bonus || bonus->source != BonusSource::HERO_SPECIAL)
			continue;
		// Converted alias producers are replaced on the instance. Falling back to
		// their original limiters would grant both the old and captured target lines.
		if(hero.creatureLineSpecialtyAlias
			&& std::ranges::find(hero.creatureLineSpecialtyAlias->bonuses, bonus)
				!= hero.creatureLineSpecialtyAlias->bonuses.end())
			continue;
		if(bonus->type == BonusType::SPECIAL_UPGRADE && bonus->parameters)
		{
			// Same typed target as fillUpgradeInfo; subtype names the source and
			// does not make every convertible source creature a Command specialty.
			if(matchesCreatureLine(bonus->parameters->toCreature(), creature))
				return true;
		}
		else if(bonus->val > 0 && (bonus->type == BonusType::PRIMARY_SKILL
			|| bonus->type == BonusType::CREATURE_DAMAGE || bonus->type == BonusType::STACKS_SPEED))
		{
			// Fixed creature-stat specialties may predate the conversion alias.
			// Accept only an explicit creature limiter, never a faction, source
			// upgrade subtype, dependency aura or unrestricted army-wide modifier.
			const auto * limiter = dynamic_cast<const CCreatureTypeLimiter *>(bonus->limiter.get());
			if(limiter && matchesCreatureLine(limiter->creatureID, creature, limiter->includeUpgrades))
				return true;
		}
	}
	return false;
}
}

bool hasIntrinsicAssimilation(const CCreature & creature)
{
	return std::ranges::any_of(creature.getExportedBonusList(), [](const auto & bonus)
	{
		return bonus && bonus->type == BonusType::ASSIMILATION
			&& bonus->source == BonusSource::CREATURE_ABILITY;
	});
}

bool hasIntrinsicAssimilation(CreatureID creature)
{
	return creature.hasValue() && creature.toCreature() && hasIntrinsicAssimilation(*creature.toCreature());
}

bool isCommandSpecialistCreature(const CGHeroInstance & hero, CreatureID creature)
{
	if(!hero.getArmyCommand() || !hero.getHeroType() || !creature.hasValue())
		return false;
	const auto & development = hero.getPrimaryGrowthRules();
	const auto * type = hero.getHeroType();
	std::optional<CreatureID> target;
	if(type->creatureLineSpecialtyAlias)
		target = hero.getCreatureLineSpecialtyTarget();
	return isCommandSpecialistCreature(*type, development, hero.getMagicRules(), target, creature);
}

bool isCommandSpecialistCreature(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, std::optional<CreatureID> target, CreatureID creature)
{
	if(!creature.hasValue() || !creature.toCreature())
		return false;
	if(target && hero.creatureLineSpecialtyAlias
		&& *target != hero.creatureLineSpecialtyAlias->creature)
		return matchesCreatureLine(*target, creature);
	// Resolve the captured authored specialty, independent of whether initialization
	// has already attached its combat bonus objects. This also covers starting armies.
	const auto resolved = newHorizonsHeroes::resolveSpecialtyPresentation(
		hero, development, magic, target);
	return (resolved.creature && matchesCreatureLine(*resolved.creature, creature))
		|| matchesAuthoredCreatureSpecialty(hero, creature);
}
}
