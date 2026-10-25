/* RouteMovement.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "RouteMovement.h"
#include "../AIGateway.h"
#include "ArmyFormation.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/MapObjects.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/TerrainTile.h"
#include "../../../lib/pathfinder/CGPathNode.h"
#include "../../../lib/ScopeGuard.h"

namespace NK2AI
{
namespace
{
class RouteMovement
{
	AIGateway & gateway;
	const int3 dst;
	const HeroPtr & heroPtr;
	std::unique_ptr<RouteActorObservation::Command> command;

	void clearProbing()
	{
		gateway.teleportChannelProbingList.clear();
		if(gateway.status.channelProbing())
			gateway.status.setChannelProbing(false);
	}

	void armCommand()
	{
		try
		{
			command = gateway.routeActorObservation.arm(*heroPtr.get(), gateway.playerID, *gateway.cc);
		}
		catch(const RouteActorUnavailable &)
		{
			clearProbing();
			throw;
		}
	}

	void afterMovementCheck()
	{
		gateway.waitTillFree(); // Movement may complete a battle or blocking dialog.
		const auto unavailable = command->complete(*gateway.cc);
		command.reset();
		if(unavailable)
		{
			gateway.nullkiller->invalidatePathfinderData();
			clearProbing();
			throw *unavailable;
		}
	}

	const CGObjectInstance * getObj(int3 coord, bool ignoreHero)
	{
		auto tile = gateway.cc->getTile(coord, false);
		assert(tile);
		return gateway.cc->getObj(tile->topVisitableObj(ignoreHero), false);
	}

	static bool isTeleportAction(EPathNodeAction action)
	{
		if(action != EPathNodeAction::TELEPORT_NORMAL && action != EPathNodeAction::TELEPORT_BLOCKING_VISIT)
		{
			if(action != EPathNodeAction::TELEPORT_BATTLE)
			{
				return false;
			}
		}

		return true;
	}

	static const CGObjectInstance * getDestTeleportObj(const CGObjectInstance * currentObject,
		const CGObjectInstance * nextObjectTop, const CGObjectInstance * nextObject)
	{
		if(CGTeleport::isConnected(currentObject, nextObjectTop))
			return nextObjectTop;
		if(nextObjectTop && nextObjectTop->ID == Obj::HERO)
		{
			if(CGTeleport::isConnected(currentObject, nextObject))
				return nextObject;
		}

		return nullptr;
	}

	void doMovement(int3 dst, bool transit, const EPathfindingLayer & layer)
	{
		armCommand();
		gateway.cc->moveHero(*heroPtr, heroPtr->convertFromVisitablePos(dst), transit, layer);
	}

	void doTeleportMovement(ObjectInstanceID exitId, int3 exitPos)
	{
		if(gateway.cc->getObj(exitId) && gateway.cc->getObj(exitId)->ID == Obj::WHIRLPOOL)
		{
			gateway.nullkiller->armyFormation->rearrangeArmyForWhirlpool(*heroPtr);
		}

		armCommand();
		const auto clearDestination = vstd::makeScopeGuard([&]()
		{
			gateway.destinationTeleport = ObjectInstanceID();
			gateway.destinationTeleportPos = int3(-1);
		});
		gateway.destinationTeleport = exitId;
		if(exitPos.isValid())
			gateway.destinationTeleportPos = exitPos;
		gateway.cc->moveHero(*heroPtr, heroPtr->pos, false);
		gateway.destinationTeleport = ObjectInstanceID();
		gateway.destinationTeleportPos = int3(-1);
		afterMovementCheck();
	}

	void doChannelProbing()
	{
		auto currentPos = heroPtr->visitablePos();
		auto currentTeleport = getObj(currentPos, true);

		if(currentTeleport)
		{
			auto currentExit = currentTeleport->id;

			gateway.status.setChannelProbing(true);
			for(auto exit : gateway.teleportChannelProbingList)
				doTeleportMovement(exit, int3(-1));

			gateway.teleportChannelProbingList.clear();
			gateway.status.setChannelProbing(false);

			doTeleportMovement(currentExit, currentPos);
		}
		else
		{
			logAi->debug("Unexpected channel probbing at " + currentPos.toString());

			gateway.teleportChannelProbingList.clear();
			gateway.status.setChannelProbing(false);
		}
	}

	bool followPath(const CGPath & path, int & i)
	{
		for(; i > 0; i--)
		{
			int3 currentCoord = path.nodes[i].coord;
			const auto & nextNode = path.nodes[i - 1];
			int3 nextCoord = nextNode.coord;

			auto currentObject = getObj(currentCoord, currentCoord == heroPtr->visitablePos());
			auto nextObjectTop = getObj(nextCoord, false);
			auto nextObject = getObj(nextCoord, true);
			auto destTeleportObj = getDestTeleportObj(currentObject, nextObjectTop, nextObject);
			if(isTeleportAction(nextNode.action) && destTeleportObj != nullptr)
			{
				//we use special login if hero standing on teleporter it's mean we need
				doTeleportMovement(destTeleportObj->id, nextCoord);
				if(gateway.teleportChannelProbingList.size())
					doChannelProbing();
				gateway.nullkiller->memory->markObjectVisited(destTeleportObj); //FIXME: Monoliths are not correctly visited

				continue;
			}

			//stop sending move requests if the next node can't be reached at the current turn (hero exhausted his move points)
			if(nextNode.turns)
			{
				//blockedHeroes.insert(h); //to avoid attempts of moving heroes with very little MPs
				return false;
			}

			if(nextCoord == heroPtr->visitablePos())
				continue;

			bool isConnected = false;
			bool isNextObjectTeleport = false;
			// Check there is node after next one; otherwise transit is pointless
			if(i - 2 >= 0)
			{
				isConnected = CGTeleport::isConnected(nextObjectTop, getObj(path.nodes[i - 2].coord, false));
				isNextObjectTeleport = CGTeleport::isTeleport(nextObjectTop);
			}
			if(isConnected || isNextObjectTeleport || nextNode.layer == EPathfindingLayer::AIR)
				// Hero should be able to go through object if it's allow transit
				doMovement(nextCoord, true, nextNode.layer);
			else
				doMovement(nextCoord, false, nextNode.layer);

			afterMovementCheck();

			if(gateway.teleportChannelProbingList.size())
				doChannelProbing();
		}
		return true;
	}

public:
	RouteMovement(AIGateway & gateway, const int3 & destination, const HeroPtr & hero)
		: gateway(gateway), dst(destination), heroPtr(hero) {}

	bool run()
	{
		if(!heroPtr.isVerified())
			throw cannotFulfillGoalException("Hero was lost!");
		const auto * initial = heroPtr.getUnverified();
		if(gateway.cc->getHero(initial->id) != initial || initial->getOwner() != gateway.playerID)
			throw RouteActorUnavailable(initial->id, "<unverified hero>", false);

		if(heroPtr->isGarrisoned() && heroPtr->getVisitedTown())
		{
			gateway.swapGarrisonHero(heroPtr->getVisitedTown());
			gateway.moveCreaturesToHero(heroPtr->getVisitedTown());
		}
		if(const auto * startingTown = heroPtr->getVisitedTown();
			startingTown && startingTown->tempOwner == gateway.playerID && startingTown->getVisitingHero() == heroPtr.get())
			gateway.purchaseUsefulWarMachines(startingTown, heroPtr.get());

		//TODO: consider if blockVisit objects change something in our checks: AIUtility::isBlockVisitObj()

		logAi->debug("Moving hero %s to tile %s", heroPtr->getNameTextID(), dst.toString());
		int3 startHpos = heroPtr->visitablePos();
		bool ret = false;
		if(startHpos == dst)
		{
			//FIXME: this assertion fails also if AI moves onto defeated guarded object
			//assert(cb->getVisitableObjs(dst).size() > 1); //there's no point in revisiting tile where there is no visitable object
			armCommand();
			gateway.cc->moveHero(*heroPtr, heroPtr->convertFromVisitablePos(dst), false);
			afterMovementCheck(); // TODO: is it feasible to hero get killed there if game work properly?
			// If revisiting, teleport probing is never done, and so the entries into the list would remain unused and uncleared
			gateway.teleportChannelProbingList.clear();
			// not sure if AI can currently reconsider to attack bank while staying on it. Check issue 2084 on mantis for more information.
			ret = true;
		}
		else
		{
			CGPath path;
			gateway.nullkiller->getPathsInfo(heroPtr.get())->getPath(path, dst);
			if(path.nodes.empty())
			{
				logAi->error("Hero %s cannot reach %s from %s with %d movement points.",
					heroPtr->getNameTextID(), dst.toString(), startHpos.toString(), heroPtr->movementPointsRemaining());
				throw cannotFulfillGoalException("No live path to movement target!");
			}
			int i = (int)path.nodes.size() - 1;

			gateway.teleportChannelProbingList.clear();
			gateway.status.setChannelProbing(false);

			if(!followPath(path, i))
				return false;

			if(path.nodes[0].action == EPathNodeAction::BLOCKING_VISIT || path.nodes[0].action == EPathNodeAction::BATTLE)
			{
				// when we take resource we do not reach its position. We even might not move
				// also guarded town is not get visited automatically after capturing
				ret = heroPtr.isVerified() && i == 0;
			}
		}
		if(heroPtr.isVerified())
		{
			if(auto visitedObject = vstd::frontOrNull(gateway.cc->getVisitableObjs(heroPtr->visitablePos()))) //we stand on something interesting
			{
				if(visitedObject != *heroPtr)
				{
					gateway.performObjectInteraction(visitedObject, heroPtr);
					ret = true;
				}
			}
		}
		if(heroPtr.isVerified()) //we could have lost hero after last move
		{
			ret = ret || (dst == heroPtr->visitablePos());

			if(startHpos == heroPtr->visitablePos() && !ret) //we didn't move and didn't reach the target
			{
				throw cannotFulfillGoalException("Invalid path found!");
			}

			logAi->debug("Hero %s moved from %s to %s. Returning %d.", heroPtr->getNameTextID(), startHpos.toString(), heroPtr->visitablePos().toString(), ret);
		}
		return ret;
	}
};
}

bool moveRouteActor(AIGateway & gateway, const int3 & destination, const HeroPtr & hero)
{
	return RouteMovement(gateway, destination, hero).run();
}
}
