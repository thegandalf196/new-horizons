/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "StackInfoBasicPanel.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/battle/NewHorizonsForestAmbush.h"
void StackInfoBasicPanel::appendActionStatus()
{
	if(displayedStatus.defend.defending)
	{
		const auto badge = displayedStatus.defend.bulwark ? "BULWARK" : "DEFEND";
		auto tooltip = newHorizonsBattleStatus::defendStatusTooltip(displayedStatus.defend);
		if(displayedStatus.battlecraftWait)
			tooltip += "\n\n" + newHorizonsBattleStatus::battlecraftWaitTooltip(*displayedStatus.battlecraftWait);
		labels.push_back(std::make_shared<CLabel>(8, 155, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::YELLOW, badge));
		statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(7, 153, 39, 13), tooltip, tooltip));
	}
	else if(displayedStatus.battlecraftWait || displayedStatus.overwatchRange > 0
		|| displayedStatus.forestAmbush.concealed || displayedStatus.forestAmbush.shotPending)
	{
		auto tooltip = displayedStatus.battlecraftWait
			? newHorizonsBattleStatus::battlecraftWaitTooltip(*displayedStatus.battlecraftWait) : std::string();
		if(displayedStatus.overwatchRange > 0)
			tooltip += "\n\nOverwatch ready: one 50%-damage ranged reaction when a legal enemy voluntarily enters range "
				+ std::to_string(displayedStatus.overwatchRange) + ". Ends at the delayed activation; once per round.";
		if(displayedStatus.forestAmbush.concealed)
			tooltip += "\n\nForest Ambush: concealed until the delayed activation. Hostile direct attacks and single-target spells cannot select this stack; area and indirect effects still apply. An incoming attack or any outgoing physical creature attack reveals it.";
		if(displayedStatus.forestAmbush.shotPending)
			tooltip += "\n\nForest Ambush: the first primary physical ranged strike in this delayed activation gains +"
				+ std::to_string(newHorizonsForestAmbush::FIRST_SHOT_LUCK)
				+ " Luck within normal limits. This bonus expires at activation end.";
		const auto badge = displayedStatus.forestAmbush.concealed ? "HIDDEN"
			: displayedStatus.forestAmbush.shotPending ? "AMBUSH"
			: displayedStatus.overwatchRange > 0 ? "WATCH" : "WAIT";
		labels.push_back(std::make_shared<CLabel>(8, 155, EFonts::FONT_TINY, ETextAlignment::TOPLEFT,
			Colors::YELLOW, badge));
		statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(7, 153, 39, 13), tooltip, tooltip));
	}
}
