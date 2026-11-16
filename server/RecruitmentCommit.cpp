/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#include "StdInc.h"
#include "RecruitmentCommit.h"
#include "CGameHandler.h"
#include "../lib/CPlayerState.h"
#include "../lib/callback/IGameInfoCallback.h"
#include "../lib/mapObjects/CGDwelling.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/mapObjects/NewHorizonsEstates.h"
#include "../lib/mapObjects/NewHorizonsEstatesPolicy.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/entities/artifact/CArtifact.h"
#include "../lib/networkPacks/ArtifactLocation.h"
#include "../lib/networkPacks/EstatesPacks.h"

namespace recruitmentCommit
{
Payment prepare(const CGDwelling & source, const CArmedInstance & destination,
	const CGHeroInstance * buyer, CreatureID creature, int32_t count)
{
	Payment result;
	result.owner = destination.getOwner();
	const auto * definition = creature.toCreature();
	if(buyer && definition && definition->warMachine != ArtifactID::NONE
		&& newHorizonsEstates::usesRevisedPolicy(buyer->getPerkState().rules))
	{
		result.artifact = newHorizonsEstates::artifactPurchaseQuote(
			newHorizonsEstates::recruitmentCost(source.getRecruitmentCost(creature), count, false), buyer);
		result.cost = result.artifact->cost;
		return result;
	}
	if(newHorizonsEstates::usesConscriptionLedger(buyer, creature))
		result.ledger = newHorizonsEstates::quoteRecruitment(source.getRecruitmentCost(creature), count,
			newHorizonsEstates::recruitmentLedger(buyer), true);
	result.cost = result.ledger ? result.ledger->cost : source.getTotalRecruitmentCost(creature, count, buyer);
	return result;
}

void pay(CGameHandler & handler, const Payment & admitted)
{
	if(!admitted.ledger)
	{
		handler.giveResources(admitted.owner, -admitted.cost);
		return;
	}
	EstatesKingdomChanged paid;
	paid.owner = admitted.owner;
	paid.before = paid.after = handler.gameInfo().getPlayerState(paid.owner)->estates;
	if(paid.before.recruitmentBaseCost != admitted.ledger->before)
		throw std::runtime_error("Recruitment ledger changed after admission");
	paid.after.recruitmentBaseCost = admitted.ledger->after;
	paid.payment = admitted.cost;
	handler.sendAndApply(paid);
}

bool grantWarMachine(CGameHandler & handler, const CGHeroInstance * hero, ArtifactID artifact, const Payment & admitted)
{
	const auto * definition = artifact.toArtifact();
	bool free = false;
	for(auto slot : definition->getPossibleSlots().at(ArtBearer::HERO))
		if(hero->getArt(slot) == nullptr)
			free = true;
	if(!free)
		handler.removeArtifact(ArtifactLocation(hero->id, definition->getPossibleSlots().at(ArtBearer::HERO).front()));
	if(!handler.giveHeroNewArtifact(hero, artifact, ArtifactPosition::FIRST_AVAILABLE))
		throw std::runtime_error("Prevalidated machine-factory artifact purchase failed");
	if(admitted.artifact && admitted.artifact->usesHoarder)
		handler.setObjPropertyValue(hero->id, ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK, admitted.artifact->week);
	return true;
}
}
