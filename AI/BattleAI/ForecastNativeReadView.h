/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../Global.h"
#include "../../lib/battle/UnitStateValueSnapshot.h"
#include "../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../lib/battle/IUnitInfo.h"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>
namespace battle { class Unit; class CUnitState; class OwnedUnitCreatureResolver; }
namespace bonusDependencies { class CapturedModifiers; class OwnedBonusQueryFrame; }
namespace frozenScoring { class OwnedSource; }
namespace nativeRules { class CapturedRuleDocument; }
namespace forecast
{
struct NativeReadAcquisition;
struct NativeReadQuerySource;
/// Pure owned inputs; not a native-world admission certificate. The caller
/// binds these exact scalar/query/environment owners before publication.
struct NativeReadDescriptor
{
	battle::UnitStateValueSnapshot::Identity identity;
	battle::UnitPreparationHealthBasis healthBasis;
	std::shared_ptr<const battle::OwnedUnitCreatureResolver> creatures;
};
struct NativeReadOccurrence
{
	std::shared_ptr<const NativeReadDescriptor> descriptor;
	std::shared_ptr<const battle::UnitStateValueSnapshot> values;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> modifiers;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queries;
	/// Exclusive alternative: frozen effective scoring answers, no raw recipe.
	std::shared_ptr<const frozenScoring::OwnedSource> scoring;
};
/// Complete current side/context values. Null optional means UNKNOWN, never
/// an inferred empty policy. The owner contains no callback/provider/Unit.
struct NativeReadEnvironment
{
	BattleSideArray<std::optional<battle::ProjectedUnitEnvironmentState::SideInputs>> inputs;
	BattleSideArray<std::optional<battle::ProjectedUnitEnvironmentState::SideState>> states;
	std::shared_ptr<const nativeRules::CapturedRuleDocument> magic;
	std::optional<std::set<CreatureID>> skeletonUpgrades;
	std::optional<SecondarySkill> necromancyLuckSource;
	std::optional<std::pair<int32_t, int32_t>> legacyMoraleLimits, legacyLuckLimits;
	std::map<uint32_t, int> warfare;
	uint64_t revision = 0;
};
struct NativeReadWork
{
	uint64_t materialized = 0, owningLoans = 0, queryRebindings = 0;
	bool saturated = false;
};
/// One occurrence handle into an immutable, externally owned ordered group.
/// Forks share handles. Native scalar/query views materialize only on demand;
/// no mutable ancestor state, environment, cache, RNG or closure is retained.
class DLL_LINKAGE NativeReadView final
{
public:
	/// Optional single-decision, single-thread diagnostic owner. Actual events
	/// debit it once; caller harvests explicitly, never by summing group work().
	/// Retained by groups/loans, inherited by forks, never retargeted or reset.
	static std::vector<std::shared_ptr<const NativeReadView>> fromOwned(
		std::vector<NativeReadOccurrence>, std::shared_ptr<const NativeReadEnvironment>,
		std::shared_ptr<NativeReadWork> decisionWork = nullptr);
	/// New branch-local lazy receiver/counter contexts, sharing immutable tables.
	/// Preserves input order and inherits only actually observed cache history.
	static std::vector<std::shared_ptr<const NativeReadView>> forkViews(
		std::span<const std::shared_ptr<const NativeReadView>>,
		std::span<const std::optional<int32_t>> currentNativeCaches = {});
	/// Direct already-owned current-world loan; avoids an intermediate read receiver.
	static NativeReadAcquisition acquireFromOwned(std::vector<NativeReadOccurrence>,
		std::shared_ptr<const NativeReadEnvironment>, uint32_t recipientId,
		std::shared_ptr<NativeReadWork> decisionWork = nullptr);
	/// Ownership evidence only: not callback registration, legality or source admission.
	static std::optional<NativeReadQuerySource> querySource(const battle::CUnitState &);
	const battle::CUnitState & unit() const;
	std::shared_ptr<const battle::Unit> retainUnit() const;
	std::shared_ptr<const battle::UnitStateValueSnapshot> valuesOwner() const;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queryFrame() const;
	std::shared_ptr<const frozenScoring::OwnedSource> scoringSource() const;
	std::shared_ptr<const NativeReadEnvironment> environmentOwner() const;
	/// Owning opaque GROUP identity for call-local telemetry deduplication.
	std::shared_ptr<const void> workIdentity() const;
	NativeReadWork work() const noexcept;
private:
	class Group;
	std::shared_ptr<Group> group;
	size_t slot;
	NativeReadView(std::shared_ptr<Group>, size_t);
};
struct NativeReadQuerySource
{
	std::shared_ptr<const NativeReadDescriptor> descriptor;
	std::shared_ptr<const bonusDependencies::CapturedModifiers> modifiers;
	std::shared_ptr<const bonusDependencies::OwnedBonusQueryFrame> queries;
	/// Exclusive alternative: frozen effective scoring answers, no raw recipe.
	std::shared_ptr<const frozenScoring::OwnedSource> scoring;
};
struct NativeReadAcquisition
{
	std::shared_ptr<battle::CUnitState> unit;
	std::shared_ptr<const NativeReadView> owner; // Diagnostics/retention; does not materialize a read receiver.
};
}
