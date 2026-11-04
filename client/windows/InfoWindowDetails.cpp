/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "InfoWindows.h"

#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"

void CInfoWindow::addDetailsButton(const std::string & details, PlayerColor player)
{
	OBJECT_CONSTRUCTION;
	// Native dialog layout includes this button when measuring and centering
	// the footer. It neither replaces OK nor inherits a confirmation shortcut.
	auto button = std::make_shared<CButton>(Point(), AnimationPath::builtin("NH_info_button"),
		CButton::tooltip("Details", "Show formulas and additional rules."), [details, player]()
		{
			auto explanation = CInfoWindow::create(details, player);
			explanation->releasesDialog = false;
			ENGINE->windows().pushWindow(explanation);
		});
	buttons.push_back(std::move(button));
}
