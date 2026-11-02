/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "OwnedFrameAccessibility.h"
#include "OwnedStackQuerySource.h"
#include "../../lib/battle/OwnedBattleFrameBatch.h"
#include "../../lib/battle/ProjectedBattleAccessibility.h"
#include <stdexcept>
AccessibilityInfo HypotheticBattle::getAccessibility() const
{
	if(!usesOwnedFrameQueries()) return CBattleInfoCallback::getAccessibility();
	for(const auto & [id, unit] : stackStates)
		if(unit->ownedQuerySource) { unit->ownedQuerySource->current(); break; }
	battle::Units units;
	for(const auto & occurrence : ownedFrameBatch->occurrences()) units.push_back(&ownedFrameFor(occurrence.identity.id)->unit());
	return ownedFrameAccessibility(*this, ownedFrameBatch->accessibilityScene(), std::move(units));
}
