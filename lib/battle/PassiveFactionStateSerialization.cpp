/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "SideInBattle.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "../CStack.h"

void SideInBattle::validatePassiveFactionSerialization(bool supported) const
{
	if(passiveDivineMandateUses > 5)
		throw std::runtime_error("Invalid passive Divine Mandate use count");
	if(!supported && std::ranges::any_of(orderStates, [](const HeroOrderState & order)
		{ return order.passiveDivineMandate.has_value(); }))
		throw std::runtime_error("Cannot discard passive Divine Mandate Order snapshot");
	bool temporaryGate = false;
	for(const auto & gate : pendingDemonicGates)
	{
		gate.validate();
		temporaryGate = temporaryGate || gate.temporary;
	}
	for(const auto & stack : gatedDemonicStacks)
	{
		stack.validate();
		temporaryGate = temporaryGate || stack.temporary;
	}
	if(!supported && (temporaryGate || passiveDivineMandateUses != 0))
		throw std::runtime_error("Cannot discard passive faction side state in an older format");
}
void BattleInfo::validatePassiveFactionStateSerialization(bool supported) const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		getSide(side).validatePassiveFactionSerialization(supported);
	for(const auto & stack : stacks)
		if(stack) battle::validatePhysicalPoisonSerialization(*stack, supported);
	if(!supported)
		for(const auto & stack : stacks)
			if(stack && (stack->arcaneMarks.hasState() || stack->demonicGateOpened))
				throw std::runtime_error("Cannot discard passive faction unit state from a battle");
}
