/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "FrozenScoringBonuses.h"
#include "BonusDependencyQueryFrame.h"
namespace frozenScoring
{
/// Owned FROZEN effective answers, distinct from CapturedModifiers/raw recipes.
/// Query context Count is diagnostic only: later scratch health/count changes
/// deliberately never reevaluate a frozen updater/limiter or change these values.
class DLL_LINKAGE OwnedSource final : public IBonusBearer
{
	struct Data;
	bonusDependencies::QueryFrameBinding context;
	std::shared_ptr<const Data> data;
	OwnedSource(bonusDependencies::QueryFrameBinding, std::shared_ptr<const Data>);
public:
	static std::shared_ptr<const OwnedSource> capture(bonusDependencies::QueryFrameBinding,
		std::span<const Bonus> stacked, const std::optional<Records> & unstacked, bool veteranCohesionEarned);
	const bonusDependencies::QueryFrameBinding & binding() const noexcept { return context; }
	bool matchesProfile(uint32_t recipientId, CreatureID creature) const noexcept;
	/// Same native postprocessor consumes CURRENT receiving unit's scalar flag.
	/// This does not reevaluate frozen source rules or change frozen values.
	TConstBonusListPtr query(const CSelector &, bool unstacked, bool currentVeteranCohesionEarned) const;
	TConstBonusListPtr getAllBonuses(const CSelector &, const std::string & cachingStr = std::string()) const override;
	TConstBonusListPtr getUnstackedBonuses(const CSelector &) const override;
	bool isBonusCacheable() const override { return false; }
	bool mayHaveBonusType(BonusType) const override { return true; } // Frozen absence proves no RAW absence.
	int32_t getTreeVersion() const override { return 0; }
};
}
