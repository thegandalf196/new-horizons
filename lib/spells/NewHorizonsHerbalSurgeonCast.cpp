/*
 * NewHorizonsHerbalSurgeonCast.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include <vcmi/spells/Caster.h>
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "../entities/hero/NewHorizonsHerbalSurgeon.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleInfo.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsHeroes
{
int resolveHerbalSurgeonRegenerationPercent(const spells::IBattleCast * event)
{
	if(const auto captured = event->getHerbalSurgeonRegenerationPercent())
	{
		if(*captured < 0 || *captured > 200)
			throw std::invalid_argument("Invalid captured Herbal Surgeon percentage");
		return *captured;
	}
	const auto * callback = event->getBattle();
	const auto * state = callback ? callback->getBattle() : nullptr;
	const auto * caster = event->getCaster();
	const auto * spell = event->getSpell();
	if(event->getMode() != spells::Mode::HERO || !state || !caster || !spell)
		return 0;
	const auto * hero = caster->getHeroCaster();
	if(!hero || !hero->getHeroTypeID().hasValue()
		|| hero->getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:verdish")))
		return 0;
	const auto & magic = state->getMagicRules();
	if(!newHorizonsMagic::rulesActive(magic)
		|| magic["rulesetVersion"].Integer() != newHorizonsMagic::CURRENT_RULESET_VERSION
		|| !newHorizonsMagic::spellAllowedBySavedRoster(magic, spell->getId()))
		return 0;
	const auto base = newHorizonsMagic::spellVariantBase(magic, spell->getId());
	const auto * family = base.hasValue() ? base.toSpell() : nullptr;
	return family && family->getJsonKey() == newHorizonsMagic::NATURE_REGENERATION_SPELL
		? herbalSurgeonRegenerationPercent(hero) : 0;
}
}

namespace spells
{
void BattleCast::captureOriginalSpellSpecialties()
{
	spellcraftContext = newHorizonsHeroes::resolveSpellcraftContext(this);
	herbalSurgeonRegenerationPercent = newHorizonsHeroes::resolveHerbalSurgeonRegenerationPercent(this);
}
void BaseMechanics::captureOriginalSpellSpecialties(const IBattleCast * event)
{
	spellcraftContext = newHorizonsHeroes::resolveSpellcraftContext(event);
	herbalSurgeonRegenerationPercent = newHorizonsHeroes::resolveHerbalSurgeonRegenerationPercent(event);
}
}
