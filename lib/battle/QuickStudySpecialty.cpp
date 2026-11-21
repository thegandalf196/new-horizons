/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "QuickStudySpecialty.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "WisdomCombatState.h"
#include "../entities/hero/NewHorizonsCastleSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"

namespace quickStudySpecialty
{
namespace
{
BattleSide heroSide(const CBattleInfoCallback & battle, const CGHeroInstance & hero)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(battle.battleGetFightingHero(side) == &hero)
			return side;
	return BattleSide::NONE;
}
}

std::set<SpellID> seenSpells(const CBattleInfoCallback & battle)
{
	std::set<SpellID> result;
	if(!battle.getBattle())
		return result;
	// Completed casts, plus the accepted hero spell history: an AI projection
	// may hold no completed-cast ledger for a side whose hero it cannot see.
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		for(const auto & [spell, count] : battle.getBattle()->getWisdomCombatState(side).completedCasts)
			if(count > 0)
				result.insert(spell);
		for(const auto & spell : battle.getBattle()->getUsedSpells(side))
			result.insert(spell);
	}
	return result;
}

bool isCopy(const CGHeroInstance & hero, SpellID spell)
{
	return spell.hasValue() && spell.toSpell() && !hero.canCastThisSpell(spell.toSpell());
}

int spentCopies(const CBattleInfoCallback & battle, const CGHeroInstance & hero)
{
	const auto side = heroSide(battle, hero);
	if(side == BattleSide::NONE || !battle.getBattle())
		return 0;
	int result = 0;
	for(const auto & [spell, count] : battle.getBattle()->getWisdomCombatState(side).completedCasts)
		if(isCopy(hero, spell))
			result += static_cast<int>(count);
	return result;
}

bool canCopy(const CBattleInfoCallback & battle, const CGHeroInstance & hero, SpellID spell)
{
	const int uses = newHorizonsHeroes::quickStudyUses(&hero);
	const auto side = heroSide(battle, hero);
	if(uses <= 0 || side == BattleSide::NONE || !battle.getBattle() || !isCopy(hero, spell))
		return false;
	const auto & rules = battle.getBattle()->getMagicRules();
	const auto * candidate = spell.toSpell();
	if(!candidate->isCommonHeroSpell() || candidate->isSpecial() || candidate->isCreatureAbility()
		|| !candidate->isCombat() || !newHorizonsMagic::spellAllowedByHeroRoster(rules, spell)
		|| newHorizonsMagic::isAdventureSpell(rules, spell))
		return false;
	// Each seen spell may be copied once; the per-combat budget caps all copies.
	return seenSpells(battle).contains(spell)
		&& battle.getBattle()->getWisdomCombatState(side).priorCasts(spell) == 0
		&& spentCopies(battle, hero) < uses;
}

int copyEffectPower(const CGHeroInstance & hero, SpellID spell, int ordinaryPower)
{
	const int permille = newHorizonsHeroes::quickStudySpellPowerPermille(&hero);
	if(permille <= 0 || !isCopy(hero, spell))
		return ordinaryPower;
	return static_cast<int>(int64_t{std::max(0, ordinaryPower)} * permille / 1000);
}
}
