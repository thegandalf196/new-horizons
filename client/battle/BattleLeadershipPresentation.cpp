/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleStacksController.h"
#include "BattleInterface.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/BattleInfo.h"

void BattleStacksController::showLeadershipEfficiency(Canvas & canvas, const CStack * stack,
	const Point & boxPosition, int counterWidth, int counterHeight, int displayedCount)
{
	if(settings["battle"].Struct().contains("showLeadershipEfficiency")
		&& !settings["battle"]["showLeadershipEfficiency"].Bool())
		return;
	const auto battle = owner.getBattle();
	const auto * hero = battle ? battle->battleGetOwnerHero(stack) : nullptr;
	if(!hero || !stack->alive() || displayedCount <= 0)
		return;

	const auto & readout = leadershipReadouts[stack->unitId()].get(hero, stack->creatureId(),
		displayedCount, nullptr, battle->getBattle(), [&]()
	{
		auto result = battle->battleLeadershipReadout(stack);
		// Hit animation stages the count until the impact frame. Only the badge
		// follows that visual count; authoritative previews use the actual count.
		result.count = displayedCount;
		result.efficiency = newHorizonsHeroes::squareRootLeadershipEfficiency(displayedCount, result.capacity);
		return result;
	});
	if(!readout.active || !leadershipPresentation::reducedEfficiency(readout.efficiency))
		return;
	const auto size = leadershipPresentation::badgeSize(leadershipPresentation::BadgeRole::BATTLE);
	leadershipPresentation::drawBadge(canvas,
		Point(boxPosition.x + (counterWidth - size.width) / 2, boxPosition.y + counterHeight + 1),
		leadershipPresentation::compactPercent(readout.efficiency), leadershipPresentation::BadgeRole::BATTLE);
}
