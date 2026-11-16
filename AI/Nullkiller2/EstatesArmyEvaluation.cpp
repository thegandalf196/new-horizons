/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "AIUtility.h"
#include "AIGateway.h"
#include "Engine/Nullkiller.h"
#include "../../lib/StartInfo.h"
#include "Helpers/NewHorizonsMuster.h"
#include "../../lib/mapObjects/MapObjects.h"
#include "../../lib/mapObjects/EstatesRecruitmentQuote.h"
#include "../../lib/entities/creature/NewHorizonsMusterRules.h"

namespace NK2AI
{
namespace
{
class CollectibleMineArmy : public CArmedInstance
{
public:
	CollectibleMineArmy() : CArmedInstance(nullptr, BonusNodeType::UNKNOWN, true) {}
	void armyChanged() override {}
};
}
uint64_t ownedMineArmyReward(const Nullkiller * aiNk, const CGHeroInstance * hero, const CGMine * mine)
{
	if(!hero || !mine || hero->getOwner() != aiNk->playerID || mine->getOwner() != aiNk->playerID
		|| !aiNk->settings->isGarrisonTroopsUsageAllowed() || aiNk->cc->getStartInfo()->restrictedGarrisonsForAI()
		|| !aiNk->cc->isVisible(mine->visitablePos()) || aiNk->cc->getObj(mine->id, false) != mine)
		return 0;
	// Only guards already delivered to this visible mine; pending Company debt
	// is not an army and never justifies a collection journey.
	CollectibleMineArmy collectible;
	for(const auto & [slot, stack] : mine->Slots())
		if(!mine->isCompanySlot(slot))
			collectible.setCreature(slot, stack->getCreatureID(), stack->getCount());
	return aiNk->armyManager->howManyReinforcementsCanGet(hero, hero, &collectible, TerrainId::NONE);
}

uint64_t townMusterArmyReward(const Nullkiller * aiNk, const CGHeroInstance * hero, const CGTownInstance * town)
{
	if(!hero || !town || hero->getOwner() != aiNk->playerID || town->getOwner() != aiNk->playerID
		|| (town->getVisitingHero() && town->getVisitingHero() != hero))
		return 0;
	const auto calendar = aiNk->cc->getCalendar();
	const int week = ::newHorizonsMuster::absoluteWeek(calendar.getCurrentDay(), calendar.getDaysInWeek());
	const auto selected = [hero](std::string_view perk)
	{
		return hero->hasActivePerk(std::string(::newHorizonsMuster::RECRUITMENT_SKILL), std::string(perk));
	};
	::newHorizonsMuster::PerkModifiers modifiers;
	modifiers.volunteerNetwork = selected(::newHorizonsMuster::VOLUNTEER_NETWORK_PERK);
	modifiers.eliteDraft = selected(::newHorizonsMuster::ELITE_DRAFT_PERK);
	modifiers.championsCall = selected(::newHorizonsMuster::CHAMPIONS_CALL_PERK);
	modifiers.masterRecruiter = selected(::newHorizonsMuster::MASTER_RECRUITER_PERK);
	if(hero->getNewHorizonsMusterUsesThisWeek(week) >= ::newHorizonsMuster::maximumUsesPerWeek(modifiers)
		|| town->getNewHorizonsMusterLastWeek() == week || aiNk->aiGw->hasPendingMuster(town))
		return 0;
	const auto candidate = newHorizonsMuster::chooseTownCandidate(*town, *aiNk->cc,
		hero->getPerkSkillRank(std::string(::newHorizonsMuster::RECRUITMENT_SKILL)), modifiers, hero,
		selected(::newHorizonsMuster::BROAD_MUSTER_PERK));
	if(!candidate)
		return 0;
	auto resources = aiNk->cc->getResourceAmount();
	const auto * buyer = dynamic_cast<const CGHeroInstance *>(town->getUpperArmy());
	auto projectedSlots = newHorizonsHeroes::commandArmySlots(*hero);
	newHorizonsEstates::RecruitmentQuoteCursor prices(buyer);
	const auto pay = [&](CreatureID creature, int count)
	{
		resources -= town->getTotalRecruitmentCost(creature, count, buyer, &prices.current());
		if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
			prices.accept(newHorizonsEstates::quoteRecruitment(town->getRecruitmentCost(creature), count, prices.current(), true));
	};
	const auto recruitableValue = [hero, buyer, town, &resources, &projectedSlots, &prices, &pay](CreatureID creature, int amount) -> uint64_t
	{
		if(hero->getArmyCommand())
		{
			amount = town->getRecruitmentAmount(creature, resources, amount, buyer, &prices.current());
			const auto plan = newHorizonsHeroes::planCommandRecruitment(*hero, projectedSlots, creature, amount);
			if(!plan) return 0;
			projectedSlots = plan->slots;
			pay(creature, amount);
			return static_cast<uint64_t>(amount) * creature.toCreature()->getAIValue();
		}
		auto slot = newHorizonsHeroes::recruitmentSlot(hero, creature, amount);
		if(!slot.validSlot())
			slot = newHorizonsHeroes::recruitmentSlot(hero, creature, 1);
		if(!slot.validSlot())
			return 0;
		if(const auto capacity = hero->getLeadershipSlotCapacity(creature))
		{
			const int current = hero->hasStackAtSlot(slot) ? hero->getStackCount(slot) : 0;
			amount = std::min(amount, std::max(0, capacity->maximum - current));
		}
		amount = town->getRecruitmentAmount(creature, resources, amount, buyer, &prices.current());
		pay(creature, amount);
		return static_cast<uint64_t>(amount) * creature.toCreature()->getAIValue();
	};
	if(candidate->isSplit())
	{
		const auto firstValue = recruitableValue(candidate->creature, candidate->firstAmount);
		return firstValue + recruitableValue(candidate->secondCreature, candidate->secondAmount);
	}
	return recruitableValue(candidate->creature, candidate->amount);
}

}
