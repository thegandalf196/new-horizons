/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StackInfoBasicPanel.h"
#include "ShieldPresentation.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include "../../lib/texts/TextOperations.h"

void StackInfoBasicPanel::appendVitalStatus(const CStack * stack)
{
	auto healthRemaining = TextOperations::formatMetric(stack->getFirstHPleft(), 4);
	if(stack->health.getTemporaryHitPoints() > 0)
		healthRemaining += "+" + TextOperations::formatMetric(stack->health.getTemporaryHitPoints(), 4);
	labels.push_back(std::make_shared<CLabel>(9, 168, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->translate("vcmi.battleWindow.killed") + ":"));
	labels.push_back(std::make_shared<CLabel>(9, 180, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, LIBRARY->generaltexth->allTexts[389] + ":"));
	labels.push_back(std::make_shared<CLabel>(69, 180, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, std::to_string(stack->getKilled())));
	labels.push_back(std::make_shared<CLabel>(69, 192, EFonts::FONT_TINY, ETextAlignment::BOTTOMRIGHT, Colors::WHITE, healthRemaining));

	displayedShields = shieldPresentationOverride ? *shieldPresentationOverride : battle::shieldSnapshot(*stack);
	const auto help = shieldPresentation::tooltip(displayedShields);
	if(!help.empty())
	{
		labels.push_back(std::make_shared<CLabel>(9, 194, EFonts::FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "SHIELD"));
		statusTooltips.push_back(std::make_shared<LRClickableAreaWText>(Rect(7, 193, 67, 12), help, help));
	}
}
