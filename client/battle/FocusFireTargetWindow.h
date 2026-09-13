/*
 * FocusFireTargetWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"

class BattleInterface;
class CButton;
class CLabel;
class CListBox;
class CMultiLineLabel;
struct FocusFireSelectionContext;

/// Private source4 prototype: modal value selection, never creature/hex targeting.
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
	explicit FocusFireTargetWindow(const std::shared_ptr<BattleInterface> & owner);
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
