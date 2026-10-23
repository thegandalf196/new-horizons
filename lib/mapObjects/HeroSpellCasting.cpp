/*
 * HeroSpellCasting.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/artifact/CArtifactInstance.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include <vcmi/spells/Spell.h>

namespace
{
bool equippedScrollGrantsCasting(const CGHeroInstance & hero, const spells::Spell & spell, bool mapAllowed)
{
	// Only a genuine equipped source may bypass School proficiency. Do not turn
	// arbitrary SPELL bonuses, Tomes, backpack items or perk variants into scrolls.
	if(!spell.isCombat() || spell.isSpecial() || !mapAllowed)
		return false;
	const auto sources = hero.getSourcesForSpell(spell.getId());
	for(const auto & [position, slot] : hero.artifactsWorn)
	{
		(void)slot;
		const auto * artifact = hero.getArt(position); // Excludes locked component slots.
		if(!artifact || !artifact->isScroll() || artifact->getScrollSpellID() != spell.getId())
			continue;
		if(vstd::contains(sources, BonusSourceID(artifact->getId())))
			return true;
	}
	return false;
}
}

bool CGHeroInstance::canCastThisSpell(const spells::Spell * spell) const
{
	if(!spell || !newHorizonsMagic::spellAllowedByHeroRoster(getMagicRules(), spell->getId()))
		return false;
	if(isNewHorizonsSpellExcluded(spell->getId()))
		return false;
	const bool inSpellBook = isSpellInscribedForCasting(spell->getId()) && hasSpellbook();
	// Inscription and equipped scrolls grant casting access, not ordinary learning.
	// Physical Spellbook, mana, action and artifact completion rules still apply.
	if(!inSpellBook && !newHorizonsMagic::hasSchoolProficiency(this, spell->getId())
		&& !equippedScrollGrantsCasting(*this, *spell, cb->isAllowed(spell->getId())))
		return false;

	if(spell->isSpecial())
	{
		if(inSpellBook)
		{//hero has this spell in spellbook
			logGlobal->error("Special spell %s in spellbook.", spell->getNameTextID());
		}
		return hasBonusOfType(BonusType::SPELL, BonusSubtypeID(spell->getId()));
	}
	else if(!cb->isAllowed(spell->getId()))
	{
		if(inSpellBook)
		{
			//hero has this spell in spellbook
			//it is normal if set in map editor, but trace it to possible debug of magic guild
			logGlobal->trace("Banned spell %s in spellbook.", spell->getNameTextID());
		}
	}
	return !getSourcesForSpell(spell->getId()).empty();
}
