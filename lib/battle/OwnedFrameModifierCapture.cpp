/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedFrameBranchQueries.h"
#include "OwnedUnitBonusFrameData.h"
#include "../bonuses/BonusDependencyCapture.h"
#include <stdexcept>
namespace battle
{
bonusDependencies::CapturedModifiers OwnedFrameBranchQueries::captureModifiers(const OwnedUnitBonusFrame & recipient)
{
	const auto & data = *recipient.data;
	if(data.subject->veteranCohesionEarned)
		throw std::invalid_argument("Veteran Cohesion requires its native post-query owner");
	const auto & source = data.forms.at(data.subject->battleFormCreature());
	const auto & selected = data.sourceGraph(source, false);
	const auto query = data.context(*data.subject, source, false);
	const auto raw = data.raw(selected, query.records);
	return selected.captureRootModifiers(query, raw, data.subject.get());
}
}
