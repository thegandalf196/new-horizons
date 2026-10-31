/* Part of VCMI. Authors: see AUTHORS. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackEffectRefresh.h"
#include "../bonuses/BonusList.h"
#include <algorithm>
namespace stackEffectRefresh
{
bool matchesIdentity(const Bonus & value, const Bonus * candidate)
{
	return candidate && candidate->source == value.source && candidate->sid == value.sid
		&& candidate->type == value.type && candidate->subtype == value.subtype
		&& candidate->valType == value.valType && candidate->statusIdentity == value.statusIdentity;
}
CSelector legacyCandidateSelector(const Bonus & value)
{
	const CSelector matchingStatusIdentity([&value](const Bonus * candidate)
	{
		return candidate && candidate->statusIdentity == value.statusIdentity;
	});
	return Selector::source(BonusSource::SPELL_EFFECT, value.sid)
		.And(Selector::typeSubtypeValueType(value.type, value.subtype, value.valType)).And(matchingStatusIdentity);
}
Plan plan(bool forceAdd, const Bonus & value, const BonusList & localExports, bool hasLegacyCandidate)
{
	if(!value.hasStatusMetadata()) return {false, forceAdd || !hasLegacyCandidate};
	const bool hasMatchingIdentity = std::ranges::any_of(localExports, [&value](const auto & candidate)
	{
		return matchesIdentity(value, candidate.get());
	});
	return {true, forceAdd || !hasMatchingIdentity};
}
void refreshFields(Bonus & target, const Bonus & value)
{
	target.turnsRemain = std::max(target.turnsRemain, value.turnsRemain);
	for(const BonusStatusTag tag : value.statusTags)
		if(std::find(target.statusTags.begin(), target.statusTags.end(), tag) == target.statusTags.end())
			target.statusTags.push_back(tag);
}
}
