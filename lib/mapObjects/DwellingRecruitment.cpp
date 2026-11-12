/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CGDwelling.h"
#include "CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../IGameSettings.h"
#include "../callback/IGameEventCallback.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/creature/NewHorizonsMusterRules.h"
#include "../entities/hero/NewHorizonsLeadership.h"
#include "../networkPacks/PacksForClient.h"
#include "../networkPacks/StackLocation.h"

namespace
{
void message(IGameEventCallback & events, const CGHeroInstance & hero, CreatureID creature, int text, int count = 0)
{
	InfoWindow window;
	window.type = EInfoWindowMode::AUTO;
	window.player = hero.getOwner();
	window.text.appendTextID("core.genrltxt." + std::to_string(text));
	if(count > 0) window.text.replaceNumber(count);
	window.text.replaceNamePlural(creature);
	events.showInfoDialog(&window);
}

SlotID legacyDestination(IGameEventCallback & events, const CGHeroInstance & hero, CreatureID creature, int count, bool merge)
{
	if(merge && !newHorizonsHeroes::recruitmentSlot(&hero, creature, count).validSlot())
	{
		std::pair<SlotID, SlotID> pair;
		if(hero.mergeableStacks(pair))
		{
			const auto type = hero.getCreature(pair.first)->getId();
			const auto total = int64_t(hero.getStackCount(pair.first)) + hero.getStackCount(pair.second);
			if(newHorizonsHeroes::armySlotAccepts(&hero, pair.second, type, total))
				events.moveStack({hero.id, pair.first}, {hero.id, pair.second}, -1);
		}
	}
	return newHorizonsHeroes::recruitmentSlot(&hero, creature, count);
}

void collectFree(IGameEventCallback & events, const CGDwelling & dwelling, const CGHeroInstance & hero,
	CreatureID creature, int count, bool merge)
{
	if(count <= 0) { message(events, hero, creature, 422); return; }
	std::optional<newHorizonsHeroes::CommandArmyPlan> plan;
	SlotID slot;
	if(hero.getArmyCommand())
	{
		plan = newHorizonsHeroes::planCommandRecruitment(hero, newHorizonsHeroes::commandArmySlots(hero), creature, count);
		if(plan) slot = newHorizonsHeroes::commandRecruitmentDestination(*plan);
	}
	else
	{
		slot = legacyDestination(events, hero, creature, count, merge);
	}
	if(!slot.validSlot() || count == 0) { message(events, hero, creature, 425); return; }
	SetAvailableCreatures stock;
	stock.tid = dwelling.id;
	stock.creatures = dwelling.creatures;
	stock.creatures[0].first -= count;
	if(plan)
		for(const auto & move : plan->moves)
		{
			if(!move.withinDestination) continue;
			const StackLocation from(hero.id, move.source), to(hero.id, move.destination);
			if(!(move.swap ? events.swapStacks(from, to) : events.moveStack(from, to, move.count)))
				throw std::runtime_error("Preflighted dwelling Command rearrangement was rejected");
		}
	if(!events.recruitToSlot({hero.id, slot}, creature.toCreature(), count)) return;
	events.sendAndApply(stock);
	message(events, hero, creature, 423, count);
}
}

void CGDwelling::heroAcceptsCreatures(IGameEventCallback & events, const CGHeroInstance * hero) const
{
	if(creatures.empty() || creatures.front().second.empty()) return;
	const auto creature = creatures.front().second.front();
	const auto * type = creature.toCreature();
	const bool external = ID == Obj::CREATURE_GENERATOR1 || ID == Obj::CREATURE_GENERATOR4;
	const auto mode = ID == Obj::CREATURE_GENERATOR1 || ID == Obj::REFUGEE_CAMP
		? EOpenWindowMode::RECRUITMENT_FIRST : EOpenWindowMode::RECRUITMENT_ALL;
	if(external && type->getLevel() == 1 && getOwner() == hero->getOwner())
	{
		const auto category = cb->getCreatureCategory(creature);
		const int rank = hero->getPerkSkillRank(std::string(newHorizonsMuster::RECRUITMENT_SKILL));
		const bool recruiter = hero->hasActivePerk(std::string(newHorizonsMuster::RECRUITMENT_SKILL),
			std::string(newHorizonsMuster::EXTERNAL_RECRUITER_PERK));
		if(category && newHorizonsMuster::amountForExternalCategory(rank, category->category, recruiter))
		{
			events.showObjectWindow(this, mode, hero, true);
			return;
		}
	}
	if(type->getLevel() == 1 && ID != Obj::REFUGEE_CAMP)
	{
		collectFree(events, *this, *hero, creature, creatures.front().first,
			cb->getSettings().getBoolean(EGameSettings::DWELLINGS_MERGE_ON_RECRUIT));
		return;
	}
	if(ID == Obj::WAR_MACHINE_FACTORY)
	{
		SetAvailableCreatures stock;
		stock.tid = id;
		stock.creatures = creatures;
		for(auto & row : stock.creatures)
			row.first = hero->hasArt(row.second.at(0).toCreature()->warMachine, true, false) ? 0 : 1;
		events.sendAndApply(stock);
	}
	events.showObjectWindow(this, mode, hero, true);
}
