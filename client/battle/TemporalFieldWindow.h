/*
 * TemporalFieldWindow.h, part of VCMI / New Horizons
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

/// Authoritative preview values for the optional Temporal Field cast mode.
/// The window only presents them and asks the controller to revalidate the
/// choice; it does not calculate or mutate gameplay state.
struct TemporalFieldValues
{
	int ordinaryMana = 0;
	int massMana = 0;
	int availableMana = 0;
	int eligibleEnemyCount = 0;
	bool massAffordable = false;
	bool remaining = false;
	std::string eligibleEnemyDescription;
};

/// Context for the pre-target Temporal Field choice.
struct TemporalFieldContext
{
	Point anchor;
	TemporalFieldValues initial;
	std::function<TemporalFieldValues()> evaluate;
	std::function<bool()> confirmOrdinary;
	std::function<bool()> confirmMass;
	std::function<void()> cancel;
};

/// Compact pre-target choice for Sorcery Magic's Temporal Field perk.
/// Ordinary returns to existing target selection; Mass submits one shared
/// hero-spell action with spellMassSlow set and an invalid aim hex.
class TemporalFieldWindow final : public CWindowObject
{
	TemporalFieldContext context;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CButton> ordinaryButton;
	std::shared_ptr<CButton> massButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CMultiLineLabel> targetLabel;
	std::shared_ptr<CLabel> costLabel;
	std::shared_ptr<CLabel> magnitudeLabel;
	std::shared_ptr<CLabel> stateLabel;
	std::string statusOverride;

	TemporalFieldValues values() const;
	void refresh();
	void confirmOrdinary();
	void confirmMass();
	void cancel();

public:
	explicit TemporalFieldWindow(TemporalFieldContext context);

	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
