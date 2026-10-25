/* RouteActorObservation.h, part of VCMI engine; GPL v2.0 or later. */
#pragma once

#include "../../../lib/constants/EntityIdentifiers.h"
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class CGHeroInstance;
class CGObjectInstance;
class CPlayerSpecificInfoCallback;

namespace NK2AI
{
/// An unsuccessful route outcome, independent of ordinary path availability.
class DLL_EXPORT RouteActorUnavailable final : public std::exception
{
public:
	const ObjectInstanceID heroID;
	const std::string heroName;
	const bool observedRemoval;

	RouteActorUnavailable(ObjectInstanceID id, std::string name, bool observedRemoval);
	const char * what() const noexcept override;
};

/// Active command receipts only; callbacks never retain a removed actor.
class DLL_EXPORT RouteActorObservation
{
public:
	class DLL_EXPORT Command
	{
		friend class RouteActorObservation;
		RouteActorObservation & owner;
		const ObjectInstanceID heroID;
		const std::string heroName;
		const CGHeroInstance * const identity;
		const PlayerColor player;
		bool removed = false;
		bool active = true;

		Command(RouteActorObservation & owner, const CGHeroInstance & actor, PlayerColor player,
			const CPlayerSpecificInfoCallback & callback);
	public:
		~Command();
		Command(const Command &) = delete;
		Command & operator=(const Command &) = delete;
		/// Called after authoritative movement/battle/query completion, under the game-state read lock.
		std::optional<RouteActorUnavailable> complete(const CPlayerSpecificInfoCallback & callback);
	};

	/// Arm immediately before submitting the command, with an already verified owned actor.
	std::unique_ptr<Command> arm(const CGHeroInstance & actor, PlayerColor player,
		const CPlayerSpecificInfoCallback & callback);
	/// Called before the authoritative removal packet changes the map.
	void objectRemoved(const CGObjectInstance & object);

private:
	std::mutex mutex;
	std::vector<Command *> activeCommands;
};
}
