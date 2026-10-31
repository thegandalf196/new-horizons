/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
class BonusList;
namespace stackEffectRefresh
{
DLL_LINKAGE bool matchesIdentity(const Bonus & value, const Bonus * candidate);
/// Borrows value, which must outlive the synchronous selector query.
DLL_LINKAGE CSelector legacyCandidateSelector(const Bonus & value);
struct Plan { bool identified; bool append; };
/// Caller computes the legacy effective query eagerly, including for forced
/// ADD and identified statuses. Only identified statuses scan local exports.
DLL_LINKAGE Plan plan(bool forceAdd, const Bonus & value,
	const BonusList & localExports, bool hasLegacyCandidate);
DLL_LINKAGE void refreshFields(Bonus & target, const Bonus & value);
}
