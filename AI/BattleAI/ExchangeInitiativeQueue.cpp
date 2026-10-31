/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ExchangeInitiativeQueue.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "AttackPossibility.h"
#include "../../lib/battle/NewHorizonsOrdinaryStrike.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/bonuses/BonusList.h"
#include <algorithm>

namespace
{
/// Filter before scheduling, so spent actors cannot affect equal-speed ties.
/// This is a read-only callback view, not a cloned or mutated battle model.
class RemainingQueueView final : public CBattleInfoCallback
{
	const std::shared_ptr<const CBattleInfoCallback> subject;
	const std::set<uint32_t> allowed;
public:
	RemainingQueueView(std::shared_ptr<const CBattleInfoCallback> source, const std::vector<uint32_t> & ids)
		: subject(std::move(source)), allowed(ids.begin(), ids.end()) {}
	const IBattleInfo * getBattle() const override { return subject->getBattle(); }
	std::optional<PlayerColor> getPlayerID() const override { return subject->getPlayerID(); }
	const battle::Unit * battleActiveUnit() const override { return nullptr; }
	battle::Units battleGetUnitsIf(const battle::UnitFilter & predicate) const override
	{
		return subject->battleGetUnitsIf([&](const auto * unit)
		{
			return allowed.contains(unit->unitId()) && predicate(unit);
		});
	}
};
}

ExchangeInitiativeQueue::ExchangeInitiativeQueue(std::shared_ptr<HypotheticBattle> battle, bool watch)
	: battle(std::move(battle)), watch(watch) {}

ExchangeInitiativeQueue::Snapshot ExchangeInitiativeQueue::snapshot() const
{
	Snapshot result;
	for(const auto * unit : battle->battleGetAllUnits(true))
	{
		const auto effects = unit->getBonuses(Selector::type()(BonusType::STACKS_INITIATIVE));
		const bool wingbeat = std::any_of(effects->begin(), effects->end(), [](const auto & bonus)
		{
			return newHorizonsOrdinaryStrike::isInitiativeEffect(bonus.get());
		});
		result.emplace(unit->unitId(), Initiative{unit->getInitiative(), wingbeat});
	}
	return result;
}

bool ExchangeInitiativeQueue::changed(const Snapshot & current) const
{
	for(const auto & [id, before] : previous)
	{
		const auto now = current.find(id);
		if(now != current.end() && (before.wingbeat || now->second.wingbeat)
			&& before.value != now->second.value)
			return true;
	}
	return false;
}

void ExchangeInitiativeQueue::beginRound(const battle::Units & all, const battle::Units & participants)
{
	original = &participants;
	originalIndex = 0;
	if(!watch)
		return;
	finishRound();
	global.clear();
	members.clear();
	remaining = participants;
	for(const auto * unit : all)
		global.push_back(unit->unitId());
	for(const auto * unit : participants)
		members.emplace(unit->unitId(), unit);
	const auto current = snapshot();
	for(const auto & [id, value] : current)
		sawWingbeat |= value.wingbeat;
	roundFinished = false;
	// Cached future queues retain next-activation effects. Re-query after the actual
	// projected round boundary, including any expiry at a previous activation.
	if(!firstRound && sawWingbeat)
		rebuild();
	previous = current;
	firstRound = false;
}

void ExchangeInitiativeQueue::rebuild()
{
	RemainingQueueView view(battle, global);
	std::vector<battle::Units> order;
	view.battleGetTurnOrder(order, 0, 1, -1, lastSide);
	global.clear();
	remaining.clear();
	if(order.empty())
		return;
	for(const auto * unit : order.front())
	{
		global.push_back(unit->unitId());
		if(const auto found = members.find(unit->unitId()); found != members.end())
			remaining.push_back(found->second); // Preserve the original pointer semantics.
	}
}

void ExchangeInitiativeQueue::finishRound()
{
	if(roundFinished)
		return;
	// Nonparticipants after the final exchanging actor still determine the
	// next round's equal-Initiative side preference, without simulating attacks.
	for(auto it = global.rbegin(); it != global.rend(); ++it)
		if(const auto * unit = battle->battleGetUnitByID(*it); unit && unit->alive())
		{
			lastSide = unit->unitSide();
			break;
		}
	roundFinished = true;
}

const battle::Unit * ExchangeInitiativeQueue::next()
{
	if(!watch)
		return originalIndex < original->size() ? (*original)[originalIndex++] : nullptr;
	const auto current = snapshot();
	for(const auto & [id, value] : current)
		sawWingbeat |= value.wingbeat;
	if(changed(current))
		rebuild();
	previous = current;
	if(remaining.empty())
	{
		finishRound();
		return nullptr;
	}
	const auto * result = remaining.front();
	remaining.erase(remaining.begin());
	members.erase(result->unitId());
	const auto found = std::find(global.begin(), global.end(), result->unitId());
	if(found != global.end())
		global.erase(global.begin(), std::next(found));
	if(const auto * currentUnit = battle->battleGetUnitByID(result->unitId()); currentUnit && currentUnit->alive())
		lastSide = currentUnit->unitSide();
	return result;
}

const battle::Unit * ExchangeInitiativeQueue::nextIn(battle::Units & queue, size_t index)
{
	if(!watch)
		return index < queue.size() ? queue[index] : nullptr;
	if(index != 0)
	{
		// Hero-action completion already permuted this suffix for Seize/Rapid.
		// Every actor in this client is a member; retain those existing changes.
		remaining.assign(queue.begin() + index, queue.end());
		global.clear();
		members.clear();
		for(const auto * unit : remaining)
		{
			global.push_back(unit->unitId());
			members.emplace(unit->unitId(), unit);
		}
	}
	const auto * selected = next();
	queue.resize(index);
	if(selected)
		queue.push_back(selected);
	queue.insert(queue.end(), remaining.begin(), remaining.end());
	return selected;
}

void ExchangeInitiativeQueue::appendParticipantArmies(HypotheticBattle & battle,
	const battle::Unit * reference, const std::map<int, battle::Units> & rounds,
	battle::Units & ours, battle::Units & enemies)
{
	for(int exchangeTurn = 0; exchangeTurn < rounds.size(); exchangeTurn++)
	{
		for(auto unit : rounds.at(exchangeTurn))
		{
			if(unit->isTurret())
				continue;

			bool isOur = battle.battleMatchOwner(reference, unit, true);
			auto & attackerQueue = isOur ? ours : enemies;
			auto u = battle.getForUpdate(unit->unitId());

			if(u->alive() && !vstd::contains(attackerQueue, unit))
			{
				attackerQueue.push_back(unit);

#if BATTLE_TRACE_LEVEL
				logAi->trace("Exchanging: %s", u->getDescription());
#endif
			}
		}
	}
}
