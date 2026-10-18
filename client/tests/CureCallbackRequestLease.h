/* License: GNU General Public License v2.0 or later; see license.txt. */
#pragma once

#include <cstdint>
#include <optional>
#include "../../lib/callback/IClient.h"
#include "../../lib/battle/BattleAction.h"
#include <memory>
#include <vector>

class CCallback;
class CClient;
class CPlayerInterface;
class BattleInterface;

/// GUI-thread-only, short lease on the player's callback dependency. Shares the
/// actual gamestate/battle, forwards every request, never applies a pack itself.
class CureCallbackRequestLease final : public IClient
{
	CClient * client;
	std::weak_ptr<CPlayerInterface> player;
	std::weak_ptr<BattleInterface> owner;
	std::shared_ptr<CCallback> original;
	std::shared_ptr<CCallback> recording;
	uint64_t capturedGeneration = 0;
	void requireClient() const;

public:
	unsigned requests = 0;
	std::vector<BattleAction> spellRequests;
	bool endedBeforeSubmission = true;
	CureCallbackRequestLease(const std::shared_ptr<BattleInterface> & battle, CClient & actualClient);
	~CureCallbackRequestLease();
	void restore();
	int sendRequest(const CPackForServer & request, PlayerColor color, bool wait) override;
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor color,
		const BattleID & battle, const BattleStateInfoForRetreat & state) override;
};
