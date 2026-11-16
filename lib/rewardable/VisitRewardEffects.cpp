/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Interface.h"
#include "../TerrainHandler.h"
#include "../CPlayerState.h"
#include "../callback/IGameInfoCallback.h"
#include "../callback/IGameEventCallback.h"
#include "../gameState/CGameState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapping/TerrainTile.h"

void Rewardable::Interface::grantVisitResourcesAndReveal(IGameEventCallback & gameEvents,
	const Reward & reward, const CGHeroInstance * hero) const
{
	auto cb = getObject()->cb;
	const auto * object = dynamic_cast<const CGObjectInstance *>(getObject());
	const auto source = object ? std::optional<Obj>(object->ID) : std::nullopt;
	gameEvents.giveResources(hero->tempOwner, reward.visitResources(hero, source));

	if (reward.revealTiles)
	{
		const auto & props = *reward.revealTiles;

		const auto functor = [&props](const TerrainTile * tile)
		{
			int score = 0;
			if (tile->getTerrain()->isSurface())
				score += props.scoreSurface;

			if (tile->getTerrain()->isUnderground())
				score += props.scoreSubterra;

			if (tile->getTerrain()->isWater())
				score += props.scoreWater;

			if (tile->getTerrain()->isRock())
				score += props.scoreRock;

			return score > 0;
		};

		FowTilesType tiles;
		if (props.radius > 0)
		{
			cb->getTilesInRange(tiles, hero->getSightCenter(), props.radius, ETileVisibility::HIDDEN, hero->getOwner());
			if (props.hide)
				cb->getTilesInRange(tiles, hero->getSightCenter(), props.radius, ETileVisibility::REVEALED, hero->getOwner());

			vstd::erase_if(tiles, [&](const int3 & coord){
				return !functor(cb->getTile(coord));
			});
		}
		else
		{
			cb->getAllTiles(tiles, hero->tempOwner, -1, functor);
		}

		if (props.hide)
		{
			for (auto & player : cb->gameState().players)
			{
				if (cb->getPlayerStatus(player.first) == EPlayerStatus::INGAME && cb->getPlayerRelations(player.first, hero->getOwner()) == PlayerRelations::ENEMIES)
					gameEvents.changeFogOfWar(tiles, player.first, ETileVisibility::HIDDEN);
			}
		}
		else
		{
			gameEvents.changeFogOfWar(tiles, hero->getOwner(), ETileVisibility::REVEALED);
		}
	}

}
