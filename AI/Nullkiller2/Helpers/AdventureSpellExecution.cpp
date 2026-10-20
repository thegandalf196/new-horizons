/*
 * AdventureSpellExecution.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "AdventureSpellExecution.h"
#include "../AIGateway.h"
#include "../../../lib/ScopeGuard.h"
#include "../../../lib/callback/CCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/serializer/CTypeList.h"

namespace NK2AI
{
bool AdventureSpellExecution::castAndWait(AIGateway & gateway, const CGHeroInstance * hero,
	SpellID spell, const int3 & destination)
{
	const auto owner = gateway.cc->getPlayerID();
	if(!hero || !owner)
		return false;
	{
		std::lock_guard lock(mutex);
		if(pending)
			return false; // A nested cast cannot borrow another request's receipt.
		pending = Pending{hero->id, spell, destination, *owner, std::nullopt, std::nullopt};
	}
	const auto clear = vstd::makeScopeGuard([this]()
	{
		std::lock_guard lock(mutex);
		pending.reset();
	});
	const bool previousWait = gateway.cc->waitTillRealize;
	const auto restoreWait = vstd::makeScopeGuard([callback = gateway.cc, previousWait]()
	{
		callback->waitTillRealize = previousWait;
	});
	gateway.cc->waitTillRealize = true;
	// No receipt lock crosses submission/wait; callback game-state ownership stays unchanged.
	gateway.cc->castSpell(hero, spell, destination);
	gateway.waitTillFree();
	std::lock_guard lock(mutex);
	return pending->request && pending->reply
		&& pending->reply->id == *pending->request && pending->reply->accepted;
}

void AdventureSpellExecution::requestSent(const CPackForServer * pack, int requestID)
{
	const auto * cast = dynamic_cast<const CastAdvSpell *>(pack);
	if(!cast || requestID < 0)
		return;
	std::lock_guard lock(mutex);
	if(pending && !pending->request && cast->player == pending->player
		&& cast->hid == pending->hero && cast->sid == pending->spell && cast->pos == pending->destination)
		pending->request = static_cast<uint32_t>(requestID);
}

void AdventureSpellExecution::requestRealized(const PackageApplied * pack)
{
	if(pack->packType != CTypeList::getInstance().getTypeID<CastAdvSpell>(nullptr))
		return;
	std::lock_guard lock(mutex);
	if(!pending || pack->player != pending->player)
		return;
	// CClient submits before calling requestSent. An immediate reply can arrive
	// first; it still cannot fulfill anything until the exact sent ID is known.
	if(!pending->request || *pending->request == pack->requestID)
		pending->reply = Reply{pack->requestID, pack->result};
}
}
