/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include <memory>
class HypotheticBattle;
namespace battle { class OwnedUnitBonusFrame; }
namespace vstd { class RNG; }
namespace creatureBurnAI
{
class CapturedTick;
class OwnedTick;
struct PendingEcho
{
	newHorizonsSoulChain::Link link;
	BattleSide victimSide = BattleSide::NONE;
	int64_t actualDamage = 0;
};
/// Capture only in the actual callback/library interval; this does not construct a live frame.
CapturedTick captureOwnedTick(const std::shared_ptr<HypotheticBattle> & battle,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame, size_t markerHandle,
	BattleUnitTurnReason reason, const newHorizonsCreatureBurn::IdentityFacts & identities);
/// Pure postcapture injury preparation; caller supplies the existing branch RNGStub representative.
OwnedTick prepareOwnedCapturedTick(const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame,
	const CapturedTick & captured, vstd::RNG & representative);
[[nodiscard]] std::optional<PendingEcho> applyOwnedTick(HypotheticBattle & battle,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & frame, const OwnedTick & prepared);
class CapturedTick
{
	struct Data;
	std::shared_ptr<const Data> data;
	explicit CapturedTick(std::shared_ptr<const Data> value) : data(std::move(value)) {}
	friend class OwnedTick;
	friend CapturedTick captureOwnedTick(const std::shared_ptr<HypotheticBattle> &, const std::shared_ptr<const battle::OwnedUnitBonusFrame> &,
		size_t, BattleUnitTurnReason, const newHorizonsCreatureBurn::IdentityFacts &);
	friend OwnedTick prepareOwnedCapturedTick(const std::shared_ptr<const battle::OwnedUnitBonusFrame> &,
		const CapturedTick &, vstd::RNG &);
	friend std::optional<PendingEcho> applyOwnedTick(HypotheticBattle &,
		const std::shared_ptr<const battle::OwnedUnitBonusFrame> &, const OwnedTick &);
};
class OwnedTick
{
	CapturedTick captured;
	std::optional<BattleStackAttacked> hit;
	OwnedTick(CapturedTick value, std::optional<BattleStackAttacked> injury)
		: captured(std::move(value)), hit(std::move(injury)) {}
	friend OwnedTick prepareOwnedCapturedTick(const std::shared_ptr<const battle::OwnedUnitBonusFrame> &,
		const CapturedTick &, vstd::RNG &);
	friend std::optional<PendingEcho> applyOwnedTick(HypotheticBattle &,
		const std::shared_ptr<const battle::OwnedUnitBonusFrame> &, const OwnedTick &);
public:
	const std::optional<BattleStackAttacked> & injury() const { return hit; }
};
class OwnedEcho;
/// LIVE authorized callback interval only; absent/dead primary or zero echo returns no receipt.
/// Caller MUST finish the returned immediate echo before treating activation as complete.
std::optional<OwnedEcho> prepareOwnedEcho(const std::shared_ptr<HypotheticBattle> & battle,
	const PendingEcho & echo, const std::shared_ptr<const battle::OwnedUnitBonusFrame> & primary,
	vstd::RNG & representative);
void applyOwnedEcho(HypotheticBattle & battle,
	const std::shared_ptr<const battle::OwnedUnitBonusFrame> & primary, const OwnedEcho & prepared);
class OwnedEcho
{
	struct Data;
	std::shared_ptr<const Data> data;
	explicit OwnedEcho(std::shared_ptr<const Data> value) : data(std::move(value)) {}
	friend std::optional<OwnedEcho> prepareOwnedEcho(const std::shared_ptr<HypotheticBattle> &,
		const PendingEcho &, const std::shared_ptr<const battle::OwnedUnitBonusFrame> &, vstd::RNG &);
	friend void applyOwnedEcho(HypotheticBattle &,
		const std::shared_ptr<const battle::OwnedUnitBonusFrame> &, const OwnedEcho &);
public:
	const BattleStackAttacked & injury() const;
};
}
