/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitBonusFrame.h"
#include "VisibleHeroPolicy.h"
#include <optional>
#include <stdexcept>
class ClientPacketReceiveSession;
class BattleInfo;
class CBattleInfoCallback;
namespace battle
{
class OwnedBattleFrameCapture;
class OwnedBattleFrameSubject;
struct BattleAccessibilityState;
/// Owned active-battle occurrences only. No callback, world, native node, deferred
/// factory or authority grant survives capture. Query payload borrows are READ-ONLY.
class DLL_LINKAGE OwnedBattleFrameBatch final : boost::noncopyable
{
public:
	struct Occurrence
	{
		OwnedUnitBonusFrame::Identity identity;
		std::shared_ptr<const OwnedUnitBonusFrame> frame;
	};
	BattleID battle() const { return battleId; }
	PlayerColor observer() const { return player; }
	uint32_t activeUnitId() const { return activeId; }
	int32_t activationSerial() const { return serial; }
	const std::vector<Occurrence> & occurrences() const { return units; }
	const std::shared_ptr<const OwnedUnitBonusFrame> & frame(uint32_t id) const;
	VisibleHeroPolicy::Knowledge heroKnowledge(BattleSide side) const { return heroPolicy(side).kind(); }
	const VisibleHeroPolicy & heroPolicy(BattleSide side) const { return policies.at(side).value(); }
	/// Historical owned query data only; never freshness or action authority.
	const std::shared_ptr<CBattleInfoCallback> & querySubject() const
	{
		if(!sealedSubject) throw std::logic_error("Owned batch query subject was not sealed");
		return sealedSubject;
	}
	const BattleAccessibilityState & accessibilityScene() const
	{
		if(!scene) throw std::logic_error("Owned batch accessibility scene was not captured");
		return *scene;
	}
private:
	friend class OwnedBattleFrameCapture;
	friend class OwnedBattleFrameSubject;
	BattleID battleId;
	uint32_t activeId = 0;
	int32_t serial = 0;
	PlayerColor player;
	BattleSideArray<std::optional<VisibleHeroPolicy>> policies; // Uncaptured refuses; never invented UNKNOWN/ABSENT.
	std::vector<Occurrence> units;
	std::shared_ptr<const BattleAccessibilityState> scene;
	std::shared_ptr<CBattleInfoCallback> sealedSubject;
	OwnedBattleFrameBatch() = default;
};
/// Construction is reachable only from the actual synchronous receive owner.
/// This is not a public import API for callers holding arbitrary raw pointers.
class DLL_LINKAGE OwnedBattleFrameCapture final
{
	friend class ::ClientPacketReceiveSession;
	static std::shared_ptr<const OwnedBattleFrameBatch> capture(const BattleInfo & native,
		const CBattleInfoCallback & observer, uint32_t activeId, const OwnedBonusCaptureCodec & codec);
};
}
