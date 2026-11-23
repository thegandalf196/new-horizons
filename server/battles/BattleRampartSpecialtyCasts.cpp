/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleRampartSpecialtyCasts.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/NewHorizonsRampartSpecialties.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMetamagicCompletion.h"

namespace battleRampartSpecialties
{
namespace
{
void logLine(CGameHandler & handler, const CBattleInfoCallback & battle, const std::string & textID, const battle::Unit & unit)
{
	BattleLogMessage message;
	message.battleID = battle.getBattle()->getBattleID();
	MetaString line;
	line.appendTextID(textID);
	line.replaceNumber(unit.getCount());
	line.replaceName(unit.creatureId(), unit.getCount());
	message.lines.push_back(std::move(line));
	handler.sendAndApply(message);
}

/// Replaces any earlier copy of the specialty effect (same stacking), so a new cast
/// refreshes its value and duration instead of adding a second copy.
void refresh(CGameHandler & handler, const CBattleInfoCallback & battle, const battle::Unit & unit, const Bonus & bonus)
{
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	const auto stacking = bonus.stacking;
	// Hold the list: ranging over *temporary leaves it dangling before C++23.
	const auto held = unit.getBonuses(CSelector([stacking](const Bonus * candidate)
		{ return candidate && candidate->stacking == stacking; }));
	std::vector<Bonus> earlier;
	for(const auto & existing : *held)
		earlier.push_back(*existing);
	if(!earlier.empty())
		effect.toRemove.emplace_back(unit.unitId(), std::move(earlier));
	effect.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{bonus});
	handler.sendAndApply(effect);
}

/// Records the damage-side Rampart specialties a combat starts with (they act per blow).
void logActivePassives(const CGHeroInstance * hero)
{
	if(!hero)
		return;
	const auto active = [hero](const char * name, int value)
	{
		if(value > 0)
			logGlobal->debug("Rampart specialty active: %s %s (%d)", hero->getNameTextID(), name, value);
	};
	active("Giant-Slayer permille per step", newHorizonsHeroes::giantSlayerPermillePerStep(hero));
	active("Use the Ground permille", newHorizonsHeroes::useTheGroundPermille(hero));
	active("Perfect Mount percent", newHorizonsHeroes::perfectMountPercent(*hero).value_or(0));
	active("Treantmoot Dendroid percent", newHorizonsHeroes::treantmootPercent(*hero).value_or(0));
	active("Forest Ambush shot percent", newHorizonsHeroes::forestAmbushShotPercent(hero));
	active("Sorceress of Enroth spell level", newHorizonsHeroes::sorceressFreeSpellLevel(hero));
}

bool ridesOn(const CGHeroInstance & hero, const CSpell & spell)
{
	return (spell.getId() == SpellID::CURE && newHorizonsHeroes::livingRemedyBasisPoints(&hero) > 0)
		|| (newHorizonsRampart::isIceSpell(spell.getId()) && newHorizonsHeroes::brittlePermille(&hero) > 0);
}
}

CastContext prepareCast(const CBattleInfoCallback & battle, const CGHeroInstance & hero, const CSpell & spell,
	const spells::Mechanics & mechanics, const battle::Target & target)
{
	CastContext result;
	if(newHorizonsRampart::sorceressCastIsFree(battle, hero, spell))
		logGlobal->debug("Sorceress of Enroth: %s casts %s as her free first spell", hero.getNameTextID(), spell.getJsonKey());
	if(!ridesOn(hero, spell))
		return result;
	for(const auto * stack : mechanics.getAffectedStacks(target))
		if(stack && stack->alive())
			result.recipients.emplace_back(stack->unitId(), stack->getAvailableHealth());
	return result;
}

void completeCast(CGameHandler & handler, const CBattleInfoCallback & battle, const CGHeroInstance & hero,
	const CSpell & spell, BattleSide side, const CastContext & context)
{
	for(const auto & [id, healthBefore] : context.recipients)
	{
		const auto * unit = battle.battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost())
			continue;
		// Living Remedy: every friendly stack Uland's Cure reached regenerates.
		if(spell.getId() == SpellID::CURE && unit->unitSide() == side)
			if(const auto regeneration = newHorizonsRampart::livingRemedy(&hero, *unit))
			{
				refresh(handler, battle, *unit, *regeneration);
				logLine(handler, battle, "new-horizons.rampart.livingRemedy.log", *unit);
				logGlobal->debug("Living Remedy: %s regenerates %d Health per round", unit->getDescription(), regeneration->val);
			}
		// Brittle: an enemy stack Alagar's ice spell actually hit loses its Defend.
		if(newHorizonsRampart::isIceSpell(spell.getId()) && unit->unitSide() != side
			&& unit->getAvailableHealth() < healthBefore)
			if(const auto marker = newHorizonsRampart::brittleMarker(&hero))
			{
				newHorizonsMetamagic::cancelDefend(*handler.spellcastEnvironment(), battle, *unit);
				refresh(handler, battle, *unit, *marker);
				logLine(handler, battle, "new-horizons.rampart.brittle.log", *unit);
				logGlobal->debug("Brittle: %s marked, +%d permille physical damage", unit->getDescription(), marker->val);
			}
	}
}

void grantAgainstTheOdds(CGameHandler & handler, const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.battleGetFightingHero(side);
		logActivePassives(hero);
		const auto luck = hero ? newHorizonsRampart::againstTheOdds(battle, side) : std::nullopt;
		if(!luck)
			continue;
		// Her stacks gain the Luck; the hero's own Luck (spell Luck rolls) is unchanged.
		SetStackEffect grant;
		grant.battleID = battle.getBattle()->getBattleID();
		for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * candidate)
			{ return candidate->unitSide() == side && candidate->alive() && !candidate->isGhost(); }))
			grant.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{*luck});
		if(grant.toAdd.empty())
			continue;
		handler.sendAndApply(grant);
		logGlobal->debug("Against the Odds: %s's army gains +%d Luck", hero->getNameTextID(), luck->val);
		BattleLogMessage message;
		message.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendTextID("new-horizons.rampart.againstTheOdds.log");
		line.replaceNumber(luck->val);
		message.lines.push_back(std::move(line));
		handler.sendAndApply(message);
	}
}
}
