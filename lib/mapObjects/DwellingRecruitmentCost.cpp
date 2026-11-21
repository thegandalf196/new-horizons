/*
 * Part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CGDwelling.h"
#include "CGHeroInstance.h"
#include "NewHorizonsEstates.h"
#include "EstatesRecruitmentQuote.h"
#include "EstatesArtifactQuote.h"
#include "NewHorizonsEstatesPolicy.h"
#include "../CCreatureHandler.h"

TResources CGDwelling::getRecruitmentCost(CreatureID creatureId) const
{
	const auto * creature = creatureId.toCreature();
	if(!creature)
		return {};

	if((ID == Obj::CREATURE_GENERATOR1 || ID == Obj::CREATURE_GENERATOR4) && creature->getLevel() == 1)
		return {};

	return creature->getFullRecruitCost();
}

TResources CGDwelling::getTotalRecruitmentCost(CreatureID creature, int32_t amount, const CGHeroInstance * buyer,
	const newHorizonsEstates::ConscriptionLedger * projectedLedger) const
{
	const auto * definition = creature.toCreature();
	if(buyer && definition && definition->warMachine != ArtifactID::NONE
		&& newHorizonsEstates::usesRevisedPolicy(buyer->getPerkState().getRules()))
	{
		if(amount > 1)
			throw std::runtime_error("Only one machine artifact may be purchased at a time");
		const auto base = newHorizonsEstates::recruitmentCost(getRecruitmentCost(creature), amount, false);
		return newHorizonsEstates::artifactPurchaseQuote(base, buyer).cost;
	}
	const auto specialty = newHorizonsEstates::recruitmentSpecialtyDiscount(buyer, creature);
	if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
		return newHorizonsEstates::quoteRecruitment(getRecruitmentCost(creature), amount,
			projectedLedger ? *projectedLedger : newHorizonsEstates::recruitmentLedger(buyer), true, specialty).cost;
	if(specialty.permille > 0)
		return newHorizonsEstates::quoteRecruitment(getRecruitmentCost(creature), amount, {}, false, specialty).cost;
	const bool discount = buyer && buyer->getOwner().isValidPlayer()
		&& buyer->hasActivePerk("new-horizons:estates", "new-horizons:estates.conscription");
	return newHorizonsEstates::recruitmentCost(getRecruitmentCost(creature), amount, discount);
}

int32_t CGDwelling::getRecruitmentAmount(CreatureID creature, const ResourceSet & resources,
	int32_t available, const CGHeroInstance * buyer,
	const newHorizonsEstates::ConscriptionLedger * projectedLedger) const
{
	const auto * definition = creature.toCreature();
	if(buyer && definition && definition->warMachine != ArtifactID::NONE
		&& newHorizonsEstates::usesRevisedPolicy(buyer->getPerkState().getRules()))
		return available > 0 && !buyer->hasArt(definition->warMachine)
			&& resources.canAfford(getTotalRecruitmentCost(creature, 1, buyer)) ? 1 : 0;
	const auto specialty = newHorizonsEstates::recruitmentSpecialtyDiscount(buyer, creature);
	if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
		return newHorizonsEstates::affordableRecruitment(getRecruitmentCost(creature), resources, available,
			projectedLedger ? *projectedLedger : newHorizonsEstates::recruitmentLedger(buyer), true, specialty);
	if(specialty.permille > 0)
		return newHorizonsEstates::affordableRecruitment(getRecruitmentCost(creature), resources, available, {}, false,
			specialty);
	const bool discount = buyer && buyer->getOwner().isValidPlayer()
		&& buyer->hasActivePerk("new-horizons:estates", "new-horizons:estates.conscription");
	return newHorizonsEstates::affordableRecruitment(getRecruitmentCost(creature), resources, available, discount);
}
