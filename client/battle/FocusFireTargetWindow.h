/*
 * FocusFireTargetWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"
#include "../../lib/battle/HeroCommand.h"

class BattleInterface;
class CButton;
class CLabel;
class CListBox;
class CMultiLineLabel;
struct FocusFireSelectionContext;

/// Modal value selection for targeted Orders. It never mutates battle state;
/// confirmation sends the same authoritative HERO_COMMAND packet as the
/// untargeted Orders panel. Protect uses the same list twice (Protector then
/// Ward) and preserves both IDs until the authority validates the request.
class FocusFireTargetWindow : public CWindowObject
{
	std::shared_ptr<FocusFireSelectionContext> selection;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CListBox> targets;
	std::shared_ptr<CLabel> state;
	std::shared_ptr<CMultiLineLabel> selectedTarget;
	std::shared_ptr<CButton> confirmButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CButton> backButton;

	void refresh();
	void confirm();
public:
	explicit FocusFireTargetWindow(const std::shared_ptr<BattleInterface> & owner,
		HeroCommand command = HeroCommand::FOCUS_FIRE);
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
