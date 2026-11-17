/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Unit.h"
#include <span>
#include <vector>
class CBattleInfoCallback;
class ServerCallback;
struct BattleStackAttacked;
namespace battle
{
/// An existing attack/cast owns this accumulator until all of its damage riders finish.
/// It stores actual casualty receipts, never speculative health or another battle branch.
class DLL_LINKAGE RedemptionEvent
{
	struct Loss
	{
		uint32_t recipient;
		PlayerColor owner;
		int64_t maximumHP;
	};
	std::vector<Loss> losses;
	bool completed = false;
	bool inspected = false;
	bool enabled = false;
	bool hasRecipients(const CBattleInfoCallback & battle);
public:
	void record(const CBattleInfoCallback & battle, std::span<const BattleStackAttacked> hits);
	void recordActualLoss(const CBattleInfoCallback & battle, uint32_t recipient,
		PlayerColor owner, int64_t actualLostMaximumHP);
	void complete(const CBattleInfoCallback & battle, ServerCallback & server);
};
}
