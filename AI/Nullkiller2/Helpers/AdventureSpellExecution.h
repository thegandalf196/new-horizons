/*
 * AdventureSpellExecution.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/int3.h"

#include <mutex>
#include <optional>
#include <cstdint>

class CGHeroInstance;
struct CPackForServer;
struct PackageApplied;

namespace NK2AI
{
class AIGateway;

/// One in-flight AI cast receipt, never saved or reused as gameplay state.
class AdventureSpellExecution
{
	struct Reply { uint32_t id; bool accepted; };
	struct Pending
	{
		ObjectInstanceID hero;
		SpellID spell;
		int3 destination;
		PlayerColor player;
		std::optional<uint32_t> request;
		std::optional<Reply> reply;
	};
	std::mutex mutex;
	std::optional<Pending> pending;

public:
	bool castAndWait(AIGateway & gateway, const CGHeroInstance * hero, SpellID spell, const int3 & destination);
	void requestSent(const CPackForServer * pack, int requestID);
	void requestRealized(const PackageApplied * pack);
};
}
