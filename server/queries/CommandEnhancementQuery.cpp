/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "MapQueries.h"
#include "QueriesProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/callback/IGameInfoCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForServer.h"

CommandEnhancementQuery::CommandEnhancementQuery(CGameHandler * owner,
	newHorizonsHeroes::CommandEnhancementOffer savedOffer)
	: CQuery(owner, TYPE), offer(std::move(savedOffer))
{
	addPlayer(offer.player);
}

bool CommandEnhancementQuery::blocksPack(const CPackForServer * pack) const
{
	const auto * reply = dynamic_cast<const HeroCommandEnhancementReply *>(pack);
	return !reply || reply->player != offer.player;
}

void CommandEnhancementQuery::onAdded(PlayerColor color)
{
	if(prompted || accepted || owner->topQuery(color).get() != this || !gh->uiReadyForDialogs.contains(color))
		return;
	const auto * hero = gh->gameInfo().getHero(offer.hero);
	if(!hero || newHorizonsHeroes::prepareCommandEnhancementOffer(hero->id, hero->getOwner(),
		hero->getCommandState(), hero->getArmyCommandRules()) != offer)
		throw std::runtime_error("Command query no longer matches its saved entitlement");
	HeroCommandEnhancementDialog dialog;
	dialog.queryID = queryID;
	dialog.offer = offer;
	prompted = true;
	gh->sendAndApply(dialog);
}

void CommandEnhancementQuery::onExposure(QueryPtr topQuery)
{
	for(auto color : players)
		onAdded(color);
}

void CommandEnhancementQuery::onRemoval(PlayerColor color)
{
	if(!accepted)
		throw std::runtime_error("Command enhancement cannot be dismissed without a validated choice");
	gh->sendQueryResolved(queryID);
	if(const auto * hero = gh->gameInfo().getHero(offer.hero))
		gh->heroLevelUpChoiceDone(hero);
	gh->resumeMasteryQueries(color); // Continue other restored heroes when this chain finishes.
}

void CommandEnhancementQuery::notifyObjectAboutRemoval(const CGObjectInstance * object, const CGHeroInstance * hero) const
{
	object->heroLevelUpDone(*gh, hero);
}

bool CGameHandler::offerHeroCommandEnhancement(const CGHeroInstance * hero)
{
	if(!hero || !hero->getOwner().isValidPlayer() || hero->hasPendingCommandLevelEvent())
		return false;
	for(const auto & query : queries->allQueries())
		if(query->getType() == CommandEnhancementQuery::TYPE
			&& static_cast<const CommandEnhancementQuery &>(*query).offer.hero == hero->id)
			return true;
	const auto offer = newHorizonsHeroes::prepareCommandEnhancementOffer(hero->id, hero->getOwner(),
		hero->getCommandState(), hero->getArmyCommandRules());
	const auto command = hero->getArmyCommand();
	if(!offer || !command || offer->milestone > command->effective())
		return false;
	queries->addQuery(std::make_shared<CommandEnhancementQuery>(this, *offer));
	return true;
}

bool CGameHandler::heroCommandEnhancementReply(QueryID qid, ObjectInstanceID heroId,
	int64_t milestone, int32_t choice, PlayerColor player)
{
	if(!player.isValidPlayer())
		return false;
	const auto top = queries->topQuery(player);
	if(!top || top->queryID != qid || top->getType() != CommandEnhancementQuery::TYPE)
		return false;
	auto & query = static_cast<CommandEnhancementQuery &>(*top);
	const auto * hero = gameInfo().getHero(heroId);
	const auto command = hero ? hero->getArmyCommand() : std::nullopt;
	if(query.accepted || !query.wasPrompted() || !hero || !command || hero->hasPendingCommandLevelEvent()
		|| !newHorizonsHeroes::validCommandEnhancementReply(query.offer, heroId, player, milestone, choice,
			hero->getCommandState(), hero->getArmyCommandRules(), command->effective())
		|| hero->getOwner() != player)
		return false;
	HeroCommandEnhancementChosen chosen;
	chosen.offer = query.offer;
	chosen.choice = choice;
	sendAndApply(chosen); // Replicated state precedes resolution and asynchronous request ACK.
	query.accepted = true;
	queries->popQuery(top);
	return true;
}
