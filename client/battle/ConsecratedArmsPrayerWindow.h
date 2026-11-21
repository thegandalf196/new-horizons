/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../windows/CWindowObject.h"
#include "../../lib/battle/BattleAction.h"

#include <functional>
#include <memory>
#include <vector>

class BattleInterface;
class CButton;
class CListBox;
class CMultiLineLabel;

namespace consecratedArmsPrayerChoice { class Selection; }

namespace consecratedArmsPrayerUI
{
/// Single submission point for player Orders. A Chosen Prayers (Consecrated
/// Arms 4) Order that still needs a prayer and recipient opens the choice
/// window; every other Order is sent unchanged. Both paths use the ordinary
/// validated hero-action request; nothing here mutates battle state.
void submitOrder(const std::shared_ptr<BattleInterface> & owner, const BattleAction & order);
using HeroSpellSender = std::function<void(const BattleAction &)>;
/// Hero spell submission: Loynis's Sermon Light spell first opens the same
/// prayer and recipient picker; `send` submits the completed, unchanged-path action.
void submitHeroSpell(const std::shared_ptr<BattleInterface> & owner, const BattleAction & spell, HeroSpellSender send);
}

/// Prayer choice, then recipient choice, for one already selected Order.
/// Cancel closes it without sending anything or spending the Hero Action.
class ConsecratedArmsPrayerWindow final : public CWindowObject
{
public:
	using Selection = consecratedArmsPrayerChoice::Selection;

private:
	std::weak_ptr<BattleInterface> battle;
	BattleAction order;
	std::shared_ptr<Selection> selection;
	consecratedArmsPrayerUI::HeroSpellSender sender;
	bool submitted = false;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::vector<std::shared_ptr<CIntObject>> prayerCards;
	std::shared_ptr<CListBox> recipients;
	std::shared_ptr<CMultiLineLabel> stateLabel;
	std::shared_ptr<CButton> confirmButton;
	std::shared_ptr<CButton> cancelButton;

	std::shared_ptr<BattleInterface> currentBattle() const;
	void createPrayerCards(const BattleInterface & owner);
	void createRecipientList();
	void refresh();
	void confirm();

public:
	ConsecratedArmsPrayerWindow(const std::shared_ptr<BattleInterface> & owner, const BattleAction & order,
		std::shared_ptr<Selection> selection, consecratedArmsPrayerUI::HeroSpellSender sender = {});
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
