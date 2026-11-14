/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AIGateway.h"
#include "AIUtility.h"
#include "Helpers/ArmyFormation.h"
#include "Helpers/RecruitmentRowPlan.h"
#include "../../lib/mapObjects/TownRecruitmentOffers.h"
#include "Helpers/NewHorizonsMuster.h"
#include "Engine/Nullkiller.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGDwelling.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/callback/Calendar.h"
#include <limits>

namespace NK2AI
{
void AIGateway::recruitCreatures(const CGDwelling * dwelling, const CArmedInstance * recruiter, ObjectInstanceID portalTownId)
{
	if(hasPendingMuster(dwelling))
	{
		logAi->debug("Deferring recruitment until its Muster result is replicated");
		return;
	}
	const auto * hero = dynamic_cast<const CGHeroInstance *>(recruiter);
	for(size_t i = 0; i < dwelling->creatures.size(); ++i)
	{
		const auto & row = dwelling->creatures[i];
		if(row.second.empty()) continue;
		const auto offer = townRecruitmentOffers::query(*dwelling, i, nullkiller->playerID);
		if(!offer || offer->choices.empty()) continue;
		auto creature = row.second.back();
		int available = static_cast<int>(std::min<uint64_t>(offer->available, std::numeric_limits<int>::max()));
		if(offer->choices != row.second)
		{
			const RecruitmentProjection projection{newHorizonsHeroes::commandArmySlots(*recruiter), {}};
			const auto plan = planRecruitmentRow(*recruiter, hero, *dwelling, *offer, available,
				cc->getResourceAmount(), projection, false);
			if(plan)
			{
				creature = plan->creature.creID;
				available = plan->creature.count;
			}
		}
		int count = dwelling->getRecruitmentAmount(creature, cc->getResourceAmount(), available, hero);
		if(count <= 0) continue;
		if(hero && hero->getArmyCommand())
		{
			if(newHorizonsHeroes::planCommandRecruitment(*hero, newHorizonsHeroes::commandArmySlots(*recruiter), creature, count))
				cc->recruitCreatures(dwelling, recruiter, creature, count, static_cast<int>(i), portalTownId);
			continue;
		}
		auto destination = newHorizonsHeroes::recruitmentSlot(recruiter, creature, count);
		const auto capacity = hero ? hero->getLeadershipSlotCapacity(creature) : std::nullopt;
		if(capacity && !destination.validSlot()) count = std::min(count, capacity->maximum);
		if(count <= 0) continue;
		if(!destination.validSlot())
		{
			bool merged = false;
			for(const auto & [source, stack] : recruiter->Slots())
			{
				if(!stack->getType()) continue;
				const auto target = recruiter->getSlotFor(stack->getCreature());
				if(target != source && armyFormation::canMergeOrSwapStacks(recruiter, recruiter, source, target))
				{
					cc->mergeStacks(recruiter, recruiter, source, target);
					merged = true;
					break;
				}
			}
			if(!merged) continue;
			destination = newHorizonsHeroes::recruitmentSlot(recruiter, creature, count);
			if(!destination.validSlot()) continue;
		}
		if(capacity)
		{
			const int present = recruiter->hasStackAtSlot(destination) ? recruiter->getStackCount(destination) : 0;
			count = std::min(count, std::max(0, capacity->maximum - present));
		}
		if(count > 0) cc->recruitCreatures(dwelling, recruiter, creature, count, static_cast<int>(i), portalTownId);
	}
	if(portalTownId == ObjectInstanceID::NONE)
		if(const auto * town = dynamic_cast<const CGTownInstance *>(dwelling)) recruitPortalCreatures(town, recruiter);
}

uint64_t externalMusterArmyReward(const Nullkiller * ai, const CGHeroInstance * hero, const CGDwelling * dwelling)
{
	if(!hero || !dwelling || hero->getOwner() != ai->playerID || dwelling->getOwner() != ai->playerID) return 0;
	const auto calendar = ai->cc->getCalendar();
	const int week = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	::newHorizonsMuster::PerkModifiers modifiers;
	modifiers.masterRecruiter = hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL),
		std::string(::newHorizonsMuster::MASTER_RECRUITER_PERK));
	if(hero->getNewHorizonsMusterUsesThisWeek(week) >= ::newHorizonsMuster::maximumUsesPerWeek(modifiers)
		|| dwelling->getNewHorizonsMusterLastWeek() == week || ai->aiGw->hasPendingMuster(dwelling)) return 0;
	const auto candidate = newHorizonsMuster::chooseExternalCandidate(*dwelling, *ai->cc,
		hero->getPerkSkillRank(std::string(::newHorizonsMuster::RECRUITMENT_SKILL)),
		hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL), std::string(::newHorizonsMuster::EXTERNAL_RECRUITER_PERK)));
	if(!candidate) return 0;
	int amount = candidate->amount;
	if(hero->getArmyCommand())
	{
		amount = dwelling->getRecruitmentAmount(candidate->creature, ai->cc->getResourceAmount(), amount, hero);
		return newHorizonsHeroes::planCommandRecruitment(*hero, newHorizonsHeroes::commandArmySlots(*hero), candidate->creature, amount)
			? static_cast<uint64_t>(amount) * candidate->creature.toCreature()->getAIValue() : 0;
	}
	auto slot = newHorizonsHeroes::recruitmentSlot(hero, candidate->creature, amount);
	if(!slot.validSlot()) slot = newHorizonsHeroes::recruitmentSlot(hero, candidate->creature, 1);
	if(!slot.validSlot()) return 0;
	if(const auto capacity = hero->getLeadershipSlotCapacity(candidate->creature))
	{
		const int present = hero->hasStackAtSlot(slot) ? hero->getStackCount(slot) : 0;
		amount = std::min(amount, std::max(0, capacity->maximum - present));
	}
	amount = dwelling->getRecruitmentAmount(candidate->creature, ai->cc->getResourceAmount(), amount, hero);
	return static_cast<uint64_t>(std::max(0, amount)) * candidate->creature.toCreature()->getAIValue();
}
}
