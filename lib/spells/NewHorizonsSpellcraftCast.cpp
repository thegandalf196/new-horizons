/*
 * NewHorizonsSpellcraftCast.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include <vcmi/spells/Caster.h>
#include "../battle/CBattleInfoCallback.h"
#include "../battle/BattleInfo.h"

namespace newHorizonsHeroes
{
SpellcraftCastContext resolveSpellcraftContext(const spells::IBattleCast * event)
{
	if(const auto captured = event->getSpellcraftContext())
		return *captured;
	const auto * callback = event->getBattle();
	const auto * state = callback ? callback->getBattle() : nullptr;
	const auto * caster = event->getCaster();
	const auto * spell = event->getSpell();
	const bool originalHero = event->getMode() == spells::Mode::HERO;
	return state && caster && spell ? captureSpellcraftContext(state->getMagicRules(),
		caster->getHeroCaster(), spell->getId(), originalHero) : SpellcraftCastContext{originalHero, std::nullopt};
}
}

namespace spells
{
BattleCast::BattleCast(const CBattleInfoCallback * cb_, const Caster * caster_, const Mode mode_, const CSpell * spell_):
	spell(spell_),
	cb(cb_),
	caster(caster_),
	mode(mode_)
{
	spellcraftContext = newHorizonsHeroes::resolveSpellcraftContext(this);
}

}
