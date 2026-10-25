/* RouteActorObservation.cpp, part of VCMI engine; GPL v2.0 or later. */
#include "StdInc.h"
#include "RouteActorObservation.h"
#include "../../../lib/callback/CPlayerSpecificInfoCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <utility>

namespace NK2AI
{
namespace
{
std::string checkedActorName(const CGHeroInstance & actor, PlayerColor player,
	const CPlayerSpecificInfoCallback & callback)
{
	if(actor.getOwner() != player || callback.getPlayerID() != player || callback.getHero(actor.id) != &actor)
		throw RouteActorUnavailable(actor.id, "<unverified hero>", false);
	return actor.getNameTextID();
}
}

RouteActorUnavailable::RouteActorUnavailable(ObjectInstanceID id, std::string name, bool observedRemoval)
	: heroID(id), heroName(std::move(name)), observedRemoval(observedRemoval) {}

const char * RouteActorUnavailable::what() const noexcept
{
	return observedRemoval ? "Route actor was removed during the issued action."
		: "Route actor identity or ownership became unavailable without a matching removal.";
}

RouteActorObservation::Command::Command(RouteActorObservation & owner,
	const CGHeroInstance & actor, PlayerColor player, const CPlayerSpecificInfoCallback & callback)
	: owner(owner), heroID(actor.id), heroName(checkedActorName(actor, player, callback)), identity(&actor), player(player)
{
	std::lock_guard lock(owner.mutex);
	owner.activeCommands.push_back(this);
}

RouteActorObservation::Command::~Command()
{
	std::lock_guard lock(owner.mutex);
	std::erase(owner.activeCommands, this);
}

std::optional<RouteActorUnavailable> RouteActorObservation::Command::complete(
	const CPlayerSpecificInfoCallback & callback)
{
	bool observed = false;
	{
		std::lock_guard lock(owner.mutex);
		assert(active);
		observed = removed;
		active = false;
		std::erase(owner.activeCommands, this);
	}
	const auto * current = callback.getHero(heroID);
	if(callback.getPlayerID() != player)
		return RouteActorUnavailable(heroID, heroName, false);
	if(!observed && current == identity && current && current->getOwner() == player)
		return std::nullopt;
	return RouteActorUnavailable(heroID, heroName, !current && observed);
}

std::unique_ptr<RouteActorObservation::Command> RouteActorObservation::arm(
	const CGHeroInstance & actor, PlayerColor player, const CPlayerSpecificInfoCallback & callback)
{
	return std::unique_ptr<Command>(new Command(*this, actor, player, callback));
}

void RouteActorObservation::objectRemoved(const CGObjectInstance & object)
{
	std::lock_guard lock(mutex);
	for(auto * command : activeCommands)
	{
		if(command->active && command->identity == &object && command->heroID == object.id
			&& command->player == object.getOwner() && object.ID == Obj::HERO)
			command->removed = true;
	}
}
}
