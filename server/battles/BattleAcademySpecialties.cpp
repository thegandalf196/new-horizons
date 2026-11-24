/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleAcademySpecialties.h"
#include "../CGameHandler.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsAcademySpecialties.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/AcademySpecialtyPacks.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"

namespace battleAcademySpecialties
{
void markHardenedHits(CGameHandler & handler, const CBattleInfoCallback & battle,
	const std::vector<BattleStackAttacked> & hits)
{
	SetStackEffect effect;
	effect.battleID = battle.getBattle()->getBattleID();
	std::vector<uint32_t> marked;
	for(const auto & hit : hits)
	{
		const auto * unit = battle.battleGetUnitByID(hit.stackAttacked);
		if(hit.damageAmount <= 0 || !unit || !unit->alive() || vstd::contains(marked, unit->unitId()))
			continue;
		const auto side = unit->unitSide();
		const auto * hero = side == BattleSide::ATTACKER || side == BattleSide::DEFENDER ? battle.battleGetFightingHero(side) : nullptr;
		const auto marker = newHorizonsAcademy::hardenedMarker(hero, *unit);
		if(!marker)
			continue;
		// Replace the earlier mark: the newer, larger total wins and the duration restarts.
		const auto held = unit->getBonuses(CSelector(newHorizonsAcademy::isHardenedMarker));
		std::vector<Bonus> earlier;
		for(const auto & bonus : *held)
			earlier.push_back(*bonus);
		if(!earlier.empty())
			effect.toRemove.emplace_back(unit->unitId(), std::move(earlier));
		effect.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{*marker});
		marked.push_back(unit->unitId());
		logGlobal->debug("Hardened: %s now takes %d permille less damage", unit->getDescription(), marker->val);
	}
	if(!effect.toAdd.empty())
		handler.sendAndApply(effect);
}

void completeCast(CGameHandler & handler, const CGHeroInstance & hero, const CSpell & spell, bool automatic)
{
	if(automatic)
		return;
	const auto next = newHorizonsHeroes::practiceAfterCast(&hero, spell.getId());
	if(!next)
		return;
	SetNewHorizonsSpellPractice practice;
	practice.heroID = hero.id;
	practice.spell = spell.getId();
	practice.percent = *next;
	handler.sendAndApply(practice);
	logGlobal->debug("Practice Makes Perfect: %s's %s is now %d%% stronger", hero.getNameTextID(), spell.getJsonKey(), *next);
}

void regenerateMana(CGameHandler & handler, const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = battle.battleGetFightingHero(side);
		const int mana = hero ? newHorizonsHeroes::inexhaustibleKnowledgeMana(*hero) : 0;
		if(mana <= 0)
			continue;
		handler.restoreSpellPoints(hero->id, mana);
		BattleLogMessage log;
		log.battleID = battle.getBattle()->getBattleID();
		MetaString line;
		line.appendTextID("new-horizons.academy.inexhaustibleKnowledge.log");
		line.replaceTextID(hero->getNameTextID());
		line.replaceNumber(mana);
		log.lines.push_back(std::move(line));
		handler.sendAndApply(log);
		logGlobal->debug("Inexhaustible Knowledge: %s regains %d Mana", hero->getNameTextID(), mana);
	}
}

void grantSpoilsOfAlchemy(CGameHandler & handler, BattleID battleID, const CGHeroInstance * winner,
	const std::map<CreatureID, int32_t> & enemyCasualties)
{
	if(!winner || newHorizonsHeroes::spoilsGoldPerMercury(winner) <= 0 || !winner->getOwner().isValidPlayer())
		return;
	int64_t destroyedGold = 0;
	for(const auto & [creature, count] : enemyCasualties)
	{
		const auto * type = creature.hasValue() ? creature.toCreature() : nullptr;
		// War machines are not recruited creatures.
		if(type && count > 0 && type->warMachine == ArtifactID::NONE)
			destroyedGold += int64_t{type->getFullRecruitCost()[EGameResID::GOLD]} * count;
	}
	const int mercury = newHorizonsHeroes::spoilsOfAlchemyMercury(winner, destroyedGold);
	logGlobal->debug("Spoils of Alchemy: %s destroyed %d Gold of enemies, gains %d Mercury",
		winner->getNameTextID(), static_cast<int>(destroyedGold), mercury);
	if(mercury <= 0)
		return;
	handler.giveResource(winner->getOwner(), EGameResID::MERCURY, mercury);
	BattleLogMessage log;
	log.battleID = battleID;
	MetaString line;
	line.appendTextID("new-horizons.academy.spoilsOfAlchemy.log");
	line.replaceTextID(winner->getNameTextID());
	line.replaceNumber(mercury);
	log.lines.push_back(std::move(line));
	handler.sendAndApply(log);
}
}
