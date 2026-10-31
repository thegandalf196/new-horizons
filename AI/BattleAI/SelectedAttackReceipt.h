/*
 * SelectedAttackReceipt.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class AttackPossibility;
class BattleHex;
class DamageCache;
class HypotheticBattle;
class StackWithBonuses;
class VisibleCandidateWorld;
struct BattleAttackInfo;
struct FortuneStrikeProjection;
namespace battle { class Unit; }
namespace battle { class CUnitState; }

/// Selected ordinary-preview transfer, NOT an authoritative action, an owned
/// originating export program, complete policy knowledge or absolute utility.
/// Snapshot objects keep the existing read-only-after-preview alias contract.
class SelectedAttackReceipt
{
public:
	/// Collection-point HP, AFTER existing selected movement/reactions and BEFORE
	/// replayed strikes. Not per-hit damage provenance or a native event receipt.
	struct RecipientObservation
	{
		uint32_t id;
		int64_t beforeHealth;
		int64_t previewHealth;
	};
private:
	std::shared_ptr<const VisibleCandidateWorld> queryOwner;
	std::shared_ptr<HypotheticBattle> effectOwner;
	std::shared_ptr<battle::CUnitState> actorSnapshot;
	std::vector<std::shared_ptr<battle::CUnitState>> recipients;
	std::vector<RecipientObservation> observations;
	std::map<uint32_t, int64_t> finalDamage;
	int64_t soulHarvestHealthGained = 0;
public:
	explicit SelectedAttackReceipt(const AttackPossibility & preview);
	/// Existing replay collection point: append actor AFTER collateral victims,
	/// preserving duplicate occurrences and last-write-wins damage-map behavior.
	void captureDamage(HypotheticBattle & branch);
	const auto & affectedUnits() const { return recipients; }
	const auto & healthObservations() const { return observations; }
	const auto & sourceOwner() const { return queryOwner; }
	const auto & effectPreviewOwner() const { return effectOwner; }
	std::map<uint32_t, int64_t> & damage() { return finalDamage; }
};

namespace selectedAttackReceipt
{
/// Existing Night Prowler, movement-reaction and endpoint prefix, in original order.
BattleHex commitMovement(const AttackPossibility & preview, battle::CUnitState & actor, HypotheticBattle & branch);
/// Reconstruct the same native damage context at authoritative source/target origins.
BattleAttackInfo attackForStrike(const FortuneStrikeProjection & strike,
	const battle::Unit * actor, const battle::Unit * target);
/// Revalue this same selected geometry only when its actor's Forest Ambush
/// lifecycle differs from the branch. No target/path enumeration or new RNG.
std::unique_ptr<AttackPossibility> refreshForestAmbush(const AttackPossibility & preview,
	const std::shared_ptr<HypotheticBattle> & branch, DamageCache & damageCache);
/// Return actual growth repeated by selected replay, for gross-loss accounting.
int64_t grantSoulHarvest(battle::CUnitState & actor, int64_t killedCreatures);
/// Apply captured pre-attack or residual damage at the caller's ordered stage.
void applyCapturedDamage(HypotheticBattle & branch,
	const std::shared_ptr<StackWithBonuses> & recipient, int64_t damage);
/// Restore an ordinary melee endpoint or a currently legal Skirmisher
/// endpoint before replayed damage; other shooting from-fields grant no movement.
void restorePosition(const AttackPossibility & preview, battle::CUnitState & actor,
	const HypotheticBattle & branch);
/// Copy only the existing selected-preview resource/control fields. Physical
/// damage, Fortune, expiry and continuation remain in their ordered owners.
void restoreResources(const AttackPossibility & preview, const battle::CUnitState & captured,
	const std::shared_ptr<StackWithBonuses> & recipient,
	const std::shared_ptr<StackWithBonuses> & originalActor, HypotheticBattle & branch);
}
