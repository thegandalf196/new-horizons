/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "Bonus.h"
#include "OwnedBonusPayloads.h"
#include <span>

namespace bonusDependencies
{
enum class Change : uint16_t
{
	None = 0, Count = 1, Position = 2, Controller = 4, Round = 8,
	OwnerLevel = 16, Profile = 32, ActiveEffects = 64, Roster = 128, TerrainContext = 256
};
constexpr Change operator|(Change a, Change b)
{
	return static_cast<Change>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}
constexpr bool intersects(Change a, Change b)
{
	return (static_cast<uint16_t>(a) & static_cast<uint16_t>(b)) != 0;
}
struct Classification
{
	bool supported = true;
	Change dependencies = Change::None;
};
/// Admission only. Unknown concrete subclasses and cyclic/oversized graphs fail closed.
DLL_LINKAGE Classification classify(const Bonus & raw);
/// Only for local rows observed from the actual native collector after propagation.
/// Immutable propagation updaters are retained as dependency owners, never replayed.
DLL_LINKAGE Classification classifyMaterializedLocal(const Bonus & raw);

/// A trace of the ACTUAL collector's updater applications, in execution order.
/// The caller retains native contexts only during capture, under the source lease.
/// Same raw occurrence/exporter can traverse different traces; never deduplicate it.
struct Stage
{
	BonusEvaluationContext context;
	bool currentUnitCount = false;
	bool inheritedPrimaryRating = false;
};
struct Values
{
	int value;
	PlayerColor owner;
	ILimiter::EDecision decision;
};
class DLL_LINKAGE Recipe;
namespace detail
{
class TerrainCaptureCertificate;
struct TerrainWitnesses;
Recipe captureCertified(const std::shared_ptr<const ownedBonusPayloads::Captured> &, size_t,
	std::span<const Stage>, const BonusEvaluationContext &, bool, const TerrainCaptureCertificate *);
}
class DLL_LINKAGE Recipe final
{
	struct Instruction
	{
		TUpdaterPtr kernel;
		std::optional<int> level;
		int count = 0;
		bool currentUnitCount = false;
		PlayerColor owner;
	};
	std::shared_ptr<const ownedBonusPayloads::Captured> payloads;
	std::shared_ptr<const Bonus> record;
	std::shared_ptr<const Bonus> materialized;
	std::vector<Instruction> instructions;
	std::shared_ptr<const detail::TerrainWitnesses> terrainWitnesses;
	ILimiter::EDecision decision = ILimiter::EDecision::DISCARD;
	Change required = Change::None;
	friend Recipe capture(const std::shared_ptr<const ownedBonusPayloads::Captured> &, size_t,
		std::span<const Stage>, const BonusEvaluationContext &, bool);
	friend Recipe detail::captureCertified(const std::shared_ptr<const ownedBonusPayloads::Captured> &, size_t,
		std::span<const Stage>, const BonusEvaluationContext &, bool, const detail::TerrainCaptureCertificate *);
public:
	Change dependencies() const noexcept { return required; }
	/// Count is refreshed locally. All other dirty dependencies require cold rebind.
	bool canRefresh(Change changes) const noexcept;
	Values evaluate(int currentCount, Change changes = Change::Count) const;
	/// Retain this owner with typed bindings and raw-presence metadata, even if discarded.
	const std::shared_ptr<const Bonus> & rawRecord() const noexcept { return record; }
	const std::shared_ptr<const Bonus> & outputRecord() const noexcept { return materialized; }
};
/// Cold binding only. All native collection stages must be represented, including
/// source-only and primary-rating stages. Empty traces require no remaining updater;
/// never skip raw inheritance stages. No source pointers survive.
/// Propagation-updated raw templates require a separate validated materialization.
/// Batch overload preserves cross-root payload and repeated-record alias ownership.
DLL_LINKAGE Recipe capture(const std::shared_ptr<const ownedBonusPayloads::Captured> & owned,
	size_t rootIndex, std::span<const Stage> stages, const BonusEvaluationContext & finalContext,
	bool materializedLocal = false);
DLL_LINKAGE Recipe capture(const std::shared_ptr<const Bonus> & raw,
	std::span<const Stage> stages, const BonusEvaluationContext & finalContext);
}
