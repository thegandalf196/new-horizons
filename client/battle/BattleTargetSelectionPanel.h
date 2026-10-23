/*
 * BattleTargetSelectionPanel.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../gui/CIntObject.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class BattleInterface;
class CButton;
class CLabel;
class CMultiLineLabel;
struct StormOfDaggersSelectionPreview;

/// Ordered multi-target selection controls; forecasts use the ordinary battle log.
class BattleTargetSelectionPanel final : public CIntObject
{
	BattleInterface & owner;
	bool active = false;
	bool compactMode = false;
	bool stormPreviewLogged = false;
	bool loggedStormCanConfirm = false;
	std::string loggedStormPreview;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::vector<std::shared_ptr<CIntObject>> compactDecoration;
	std::shared_ptr<CLabel> heading;
	std::shared_ptr<CMultiLineLabel> targetReadback;
	std::shared_ptr<CButton> undoButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CButton> confirmButton;

	void refresh();
	void refreshPlacement();
	void refreshSoulChain();
	void refreshStorm();
	void setCompactMode(bool compact);
	static std::string stormPreviewLog(const StormOfDaggersSelectionPreview & preview);

public:
	explicit BattleTargetSelectionPanel(BattleInterface & owner);
	void reposition();
	void update();
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
