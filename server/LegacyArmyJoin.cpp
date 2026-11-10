/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommandArmyAdmission.h"
#include "CGameHandler.h"
#include "../lib/entities/hero/NewHorizonsLeadership.h"
#include "../lib/mapObjects/CGHeroInstance.h"
#include "../lib/networkPacks/StackLocation.h"
#include "../lib/networkPacks/PacksForClient.h"
#include <array>

namespace commandArmyAdmission
{
bool tryLegacyLimitedJoin(CGameHandler & handler, const CArmedInstance & source, const CGHeroInstance & hero)
{
	if(hero.getArmyCommand()) return false;
	struct Slot { CreatureID creature; int64_t count = 0; bool occupied = false; };
	struct Move { SlotID source, destination; TQuantity count; };
	std::array<Slot, GameConstants::ARMY_SIZE> projected;
	for(const auto & [position, stack] : hero.Slots()) projected[position.getNum()] = {stack->getCreatureID(), stack->getCount(), true};
	std::vector<Move> moves;
	bool limited = false;
	for(const auto & [position, stack] : source.Slots())
	{
		const auto creature = stack->getCreatureID();
		const auto capacity = hero.getLeadershipSlotCapacity(creature);
		int destination = -1;
		for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
			if(projected[i].occupied && projected[i].creature == creature) { destination = i; break; }
		if(destination < 0)
			for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
				if(!projected[i].occupied) { destination = i; projected[i] = {creature, 0, true}; break; }
		if(destination < 0) { limited |= capacity.has_value(); continue; }
		int64_t count = stack->getCount();
		if(capacity)
		{
			count = std::max<int64_t>(0, std::min<int64_t>(count, capacity->maximum - projected[destination].count));
			limited |= count < stack->getCount();
		}
		projected[destination].count += count;
		moves.push_back({position, SlotID(destination), static_cast<TQuantity>(count)});
	}
	if(!limited) return false;
	for(const auto & move : moves)
		if(move.count > 0) handler.moveStack({source.id, move.source}, {hero.id, move.destination}, move.count);
	if(source.stacksCount() > 0) handler.showGarrisonDialog(source.id, hero.id, true, MetaString());
	return true;
}
}
