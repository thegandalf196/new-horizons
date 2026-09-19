/*
 * SelectiveDispelWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class CButton;
class CLabel;
class CMultiLineLabel;

struct SelectiveDispelContext
{
	Point anchor;
	std::string targetDescription;
	/// `selective` is false for ordinary full Dispel and true for the perk mode.
	/// False keeps the modal open because live state no longer accepts the cast.
	std::function<bool(bool selective)> confirm;
	std::function<void()> cancel;
};

/// Post-target choice required by the optional Selective Dispel perk. Gameplay
/// remains behind the normal versioned BattleAction and authoritative server
/// validation; this window only chooses Full, Selective, or Cancel.
class SelectiveDispelWindow final : public CWindowObject
{
	SelectiveDispelContext context;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CButton> fullButton;
	std::shared_ptr<CButton> selectiveButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CMultiLineLabel> targetLabel;
	std::shared_ptr<CLabel> stateLabel;

	void confirm(bool selective);
	void cancel();

public:
	explicit SelectiveDispelWindow(SelectiveDispelContext context);
};
