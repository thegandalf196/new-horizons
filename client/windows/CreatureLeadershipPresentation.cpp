/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CCreatureWindow.h"
#include "../CPlayerInterface.h"
#include "../GameInstance.h"
#include "../widgets/Images.h"
#include "../widgets/LeadershipEfficiencyPresentation.h"
#include "../widgets/MiscWidgets.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>

void CStackWindow::MainSection::addLeadershipReadout(int rowY, const CGHeroInstance * hero,
	const CStack * stack, CreatureID creature, int64_t count)
{
	if(stack)
		count = stack->getCount();
	const auto battle = stack && stack->getBattle() && GAME->interface() && GAME->interface()->cb
		? GAME->interface()->cb->getBattle(stack->getBattle()->getBattleID()) : nullptr;
	const auto readout = battle ? battle->battleLeadershipReadout(stack)
		: newHorizonsHeroes::leadershipStackReadout(hero, creature, count, nullptr, hero);
	if(readout.active && leadershipPresentation::reducedEfficiency(readout.efficiency))
		leadershipEfficiencyPercent = leadershipPresentation::compactPercent(readout.efficiency);

	auto value = LIBRARY->generaltexth->translate("new-horizons.leadership.readout.costCapacity");
	boost::replace_all(value, "%REQUIREMENT%", std::to_string(readout.requirement));
	boost::replace_all(value, "%COUNT%", std::to_string(readout.count));
	boost::replace_all(value, "%CAPACITY%", std::to_string(readout.capacity));
	addStatLabel(EStat::LEADERSHIP, readout.active ? value : "--");
	auto hover = LIBRARY->generaltexth->translate("new-horizons.leadership.readout.label");
	boost::replace_all(hover, "%REQUIREMENT%", std::to_string(readout.requirement));
	leadershipArea = std::make_shared<LRClickableAreaWText>(Rect(114, rowY, 194, 20),
		hover, leadershipPresentation::detailText(readout));
}

void CStackWindow::MainSection::show(Canvas & canvas)
{
	CWindowSection::show(canvas);
	// Animated creature previews repaint their whole portrait on ordinary frames.
	drawLeadershipBadge(canvas);
}

void CStackWindow::MainSection::showAll(Canvas & canvas)
{
	CWindowSection::showAll(canvas);
	drawLeadershipBadge(canvas);
}

void CStackWindow::MainSection::drawLeadershipBadge(Canvas & canvas)
{
	if(leadershipEfficiencyPercent && animation)
	{
		const auto role = leadershipPresentation::BadgeRole::PORTRAIT;
		const auto size = leadershipPresentation::badgeSize(role);
		leadershipPresentation::drawBadge(canvas,
			Point(animation->pos.x + animation->pos.w - size.width - 1, animation->pos.y + 1),
			*leadershipEfficiencyPercent, role);
	}
}
