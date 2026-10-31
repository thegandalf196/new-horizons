/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "ClientPacketReceiveSession.h"
#include "OwnedRecipientCaptureMetadata.h"
#include "../CServerHandler.h"
#include "../Client.h"
#include "../GameEngine.h"
#include "GameplayReplayer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/BattleRootLifetime.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/CStack.h"
#include "../../lib/serializer/GameConnection.h"
#include "../../lib/entities/hero/NewHorizonsSpecialtySerialization.h"
#include <shared_mutex>
#include <stdexcept>
namespace
{
template<typename T>
bool sameOwner(const std::shared_ptr<T> & left, const std::shared_ptr<T> & right)
{
	return left.get() == right.get() && !left.owner_before(right) && !right.owner_before(left);
}
void copyCreature(OwnedRecipientCaptureMetadata & result, const CCreature & creature)
{
	const auto id = creature.getId();
	if(result.creatures.contains(id)) return;
	battle::OwnedUnitCreatureResolver::Creature value;
	value.id = id; value.faction = creature.getFactionID(); value.level = creature.getLevel();
	value.doubleWide = creature.isDoubleWide(); value.icon = creature.getIconIndex(); value.warMachine = creature.warMachine;
	value.cost = creature.getFullRecruitCost(); value.key = creature.getJsonKey();
	value.pluralTextID = creature.getNamePluralTextID(); value.singularTextID = creature.getNameSingularTextID();
	result.creatures.emplace(id, std::move(value));
}
OwnedRecipientCaptureMetadata::Occurrence copyOccurrence(const CStack & stack)
{
	return {{stack.unitBaseAmount(), stack.unitId(), stack.unitSide(), stack.unitOwner(), stack.unitSlot(),
		stack.battleFormOriginalCreature(), {stack.getBattleStartMaximumAggregateHP(), stack.getRebirthOriginalAggregateHP()}},
		stack.battleFormCreature(), stack.getPosition(), stack.getCount(), stack.alive(), stack.isGhost()};
}
}
OwnedRecipientCaptureMetadata ClientPacketReceiveSession::captureRecipientMetadata(
	PlayerColor observer, BattleID battle, uint32_t unit)
{
	// Validate the REAL receive owner before any native virtual/source access.
	if(!access.heldOnThisThread() || ENGINE.get() != engine || !activePacket
		|| !sameOwner(handler.networkConnection, connection) || !sameOwner(handler.logicConnection, logic)
		|| !client || handler.client.get() != client || !predictions || !world || !logic
		|| handler.networkLagCompensator.get() != predictions || client->gameStatePtr().get() != world
		|| handler.getState() != EClientState::GAMEPLAY
		|| (handler.gameplayReplayer && handler.gameplayReplayer->isActive()))
		throw std::logic_error("Owned metadata capture requires the current native receive session");
	access.requireEmpty(); // No competing receive child or interface wait after native admission.
	// This negotiated-value lock is released BEFORE acquiring any state/root/native lock.
	const auto capabilities = logic->captureSpecialtySerializationContext(connection);
	const auto codec = OwnedBonusCaptureCodec::fromSourceCapabilities(capabilities);
	auto retainedWorld = sourceWorld.lock();
	if(!retainedWorld || !sameOwner(retainedWorld, client->gameStatePtr()))
		throw std::logic_error("Owned capture world ownership identity changed");
	std::unique_lock<std::shared_mutex> stateAccess(CGameState::mutex);
	auto pin = BattleRootLifetime::retain(retainedWorld); // state -> native -> root; SHORT copy only.
	if(!pin || !pin->admitted()) throw std::logic_error("Owned metadata source is retiring");
	const auto found = client->battleCallbacks.find(observer);
	if(found == client->battleCallbacks.end() || !found->second)
		throw std::invalid_argument("Missing registered observer callback");
	const auto callback = found->second->getBattle(battle);
	const auto * native = retainedWorld->getBattle(battle);
	if(!callback || callback->getPlayerID() != observer || !native || callback->getBattle() != native
		|| native->getBattleID() != battle)
		throw std::invalid_argument("Observer callback does not match this world's battle occurrence");
	const auto roster = callback->battleGetUnitsIf([](const battle::Unit *) { return true; });
	OwnedRecipientCaptureMetadata result; result.captureCodec = codec; result.battle = battle; result.observer = observer;
	size_t matches = 0;
	for(const auto * visible : roster)
	{
		const auto * stack = dynamic_cast<const CStack *>(visible);
		if(!stack || stack->getBattle() != native) throw std::invalid_argument("Foreign visible unit occurrence");
		if(stack->unitId() == unit) { result.recipientOccurrence = result.roster.size(); ++matches; }
		result.roster.push_back(copyOccurrence(*stack));
		copyCreature(result, *stack->unitType());
		copyCreature(result, *stack->battleFormOriginalCreature().toCreature());
	}
	if(matches != 1) throw std::invalid_argument("Recipient is absent or ambiguous in the actual callback roster");
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		result.heroKnowledge.at(side) = callback->battleGetVisibleHeroPolicy(side).kind(); // UNKNOWN stays UNKNOWN.
	return result; // Pin/state then local callback/world retire; only owned values escape.
}
