/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/battle/BattleSide.h"
#include <map>
#include <memory>
#include <set>
#include <vector>

class HypotheticBattle;

/// Reuses the canonical queue only for a branch containing Wingbeat effects.
/// Membership remains the exchange's original finite horizon; no new actors.
class ExchangeInitiativeQueue
{
public:
	/// Append in original round/pointer order using the existing army-owner relation.
	static void appendParticipantArmies(HypotheticBattle & battle, const battle::Unit * reference,
		const std::map<int, battle::Units> & rounds, battle::Units & ours, battle::Units & enemies);
	explicit ExchangeInitiativeQueue(std::shared_ptr<HypotheticBattle> battle, bool watch);
	void beginRound(const battle::Units & global, const battle::Units & members);
	const battle::Unit * next();
	/// Full-queue clients may retain existing priority permutations between actors.
	const battle::Unit * nextIn(battle::Units & queue, size_t index);

private:
	struct Initiative
	{
		int value = 0;
		bool wingbeat = false;
	};
	using Snapshot = std::map<uint32_t, Initiative>;
	Snapshot snapshot() const;
	bool changed(const Snapshot & current) const;
	void rebuild();
	void finishRound();
	std::shared_ptr<HypotheticBattle> battle;
	bool watch;
	bool firstRound = true;
	bool sawWingbeat = false;
	bool roundFinished = true;
	BattleSide lastSide = BattleSide::NONE;
	Snapshot previous;
	const battle::Units * original = nullptr;
	size_t originalIndex = 0;
	std::vector<uint32_t> global;
	std::map<uint32_t, const battle::Unit *> members;
	battle::Units remaining;
};
