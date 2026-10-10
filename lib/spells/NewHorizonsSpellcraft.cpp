/* NewHorizonsSpellcraft.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsSpellcraft.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsSpellcraft
{
namespace
{
bool hasRoundDuration(const JsonNode & duration)
{
	const auto hasRoundFlag = [](const JsonNode & value)
	{
		if(value.isNumber())
			return (static_cast<BonusDuration::Type>(value.Integer()) & BonusDuration::N_TURNS) != 0;
		return value.isString() && value.String() == "N_TURNS";
	};
	if(duration.isVector())
		return std::ranges::any_of(duration.Vector(), hasRoundFlag);
	return hasRoundFlag(duration);
}
}

bool roundTemporary(const CSpell & spell, int32_t effectLevel)
{
	const auto & level = spell.getLevelInfo(effectLevel);
	// Mirror the mechanics factory: modern effects win; its legacy fallback
	// wraps effects (or, only when absent, cumulativeEffects) as core:timed.
	if(!spell.hasBattleEffects())
	{
		const auto & bonuses = !level.effects.Struct().empty() ? level.effects : level.cumulativeEffects;
		for(const auto & [name, bonus] : bonuses.Struct())
			if(hasRoundDuration(bonus["duration"]))
				return true;
	}
	const auto & effects = level.battleEffects;
	for(const auto & [name, effect] : effects.Struct())
	{
		const auto & type = effect["type"].String();
		if(type == "timed" || type == "core:timed")
		{
			for(const auto & [key, bonus] : effect["bonus"].Struct())
				if(bonus["duration"].String() == "N_TURNS")
					return true;
		}
		if(type == "core:battleForm" || type == "core:phantomArmy" || type == "core:spellLock"
			|| type == "core:focusMagicEnchantment" || type == "core:hexOfPainEffect"
			|| type == "core:plagueEffect" || type == "core:soulChainEffect"
			|| type == "core:doomEffect" || type == "core:shadowGiftEffect"
			|| type == "core:vampirismEffect" || type == "core:hydrasVitality"
			|| type == "newHorizonsCurse" || type == "newHorizonsSorrow"
			|| type == "core:attachCombatScript" || type == "attachCombatScript"
			|| type == "clone" || type == "core:clone" || type == "earthquake" || type == "core:earthquake")
			return true;
		if((type == "obstacle" || type == "core:obstacle")
			&& (effect["turnsRemaining"].isNumber() && effect["turnsRemaining"].Integer() > 0))
			return true;
	}
	// Fire Wall and Earthquake have saved round lifetimes; permanent traps,
	// action-bound Time Stop/control, and combat-long summons do not.
	return newHorizonsMagic::isFireWall(spell.getId()) || spell.getId() == SpellID::EARTHQUAKE;
}

bool extendAvailable(const IBattleInfo & battle, BattleSide side, const CSpell & spell, int32_t effectLevel)
{
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| battle.getRound() < 0 || battle.getExtendSpellLastRound(side) >= battle.getRound()
		|| !newHorizonsMagic::rulesActive(battle.getMagicRules())
		|| !newHorizonsMagic::spellAllowedBySavedRoster(battle.getMagicRules(), spell.getId()))
		return false;
	const auto * hero = battle.getSideHero(side);
	return hero && hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL), std::string(EXTEND_SPELL))
		&& roundTemporary(spell, effectLevel);
}
}
