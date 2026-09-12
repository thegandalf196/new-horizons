/*
 * BattleHeroActionWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"
#include "../../lib/battle/HeroCommand.h"

class BattleInterface;
class CButton;
class CLabel;
class CMultiLineLabel;
class CGHeroInstance;
class JsonNode;

namespace HeroCommandUI
{
std::string name(HeroCommand command);
}

/// One authoritative hero action: an existing spell, an Order or a Doctrine change.
class BattleHeroActionWindow final : public CWindowObject
{
	std::weak_ptr<BattleInterface> battle;
	const bool ordersOnly;
	std::vector<std::pair<HeroCommand, std::shared_ptr<CButton>>> commands;
	std::shared_ptr<CButton> spellButton;
	std::shared_ptr<CButton> cancel;
	std::shared_ptr<CLabel> state;
	std::vector<std::shared_ptr<CIntObject>> labels;
	std::vector<std::shared_ptr<CMultiLineLabel>> effectLabels;
	std::pair<int, int> displayedRatings;
	bool effectsInitialized = false;

	std::shared_ptr<BattleInterface> currentBattle() const;
	void refresh();
	void refreshEffects(const CGHeroInstance & hero, const JsonNode & rules);
	void setStateText(const std::string & text);
	void chooseCommand(HeroCommand command);
	void chooseSpell();

public:
	explicit BattleHeroActionWindow(const std::shared_ptr<BattleInterface> & battle, bool ordersOnlyMode = false);
	void show(Canvas & canvas) override;
	void showAll(Canvas & canvas) override;
};
