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
class HypotheticBattle;
class StackWithBonuses;
class VisibleCandidateWorld;
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
/// Copy only the existing selected-preview resource/control fields. Physical
/// damage, Fortune, expiry and continuation remain in their ordered owners.
void restoreResources(const AttackPossibility & preview, const battle::CUnitState & captured,
	const std::shared_ptr<StackWithBonuses> & recipient,
	const std::shared_ptr<StackWithBonuses> & originalActor, HypotheticBattle & branch);
}
