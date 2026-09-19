/*
 * MagicArrowOverchargeWindow.h, part of VCMI / New Horizons
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
class CSlider;

/// Values returned by the authoritative Magic Arrow overcharge preview.
///
/// The client deliberately does not calculate New Horizons spell rules.  The
/// battle/runtime layer supplies these values for the currently selected
/// target and may reject stale or unaffordable confirmations.  Keeping this
/// DTO on the client boundary also makes the panel usable by the normal spell
/// targeting flow without teaching the widget about battle state.
struct MagicArrowOverchargeValues
{
	int overcharge = 0;
	int maximumOvercharge = 0;
	int baseMana = 0;       ///< Wisdom-adjusted ordinary spell cost.
	int additionalMana = 0; ///< Deliberate overcharge surcharge.
	int totalMana = 0;
	int availableMana = 0;
	int baseDamage = 0;
	int projectedDamage = 0; ///< Raw projected damage before target resistance.
	bool affordable = false;
	std::string targetDescription;
};

/// Context for one target-selection -> overcharge -> confirmation sequence.
///
/// `evaluate` is called for every slider/button change.  `confirm` must send
/// the already-targeted action through the normal server request path and
/// return true only after it accepted the request locally.  It is expected to
/// revalidate battle identity, target identity, mana and the selected value;
/// the panel never mutates gameplay state itself.
struct MagicArrowOverchargeContext
{
	Point anchor;
	MagicArrowOverchargeValues initial;
	std::function<MagicArrowOverchargeValues(int)> evaluate;
	std::function<bool(int)> confirm;
	std::function<void()> cancel;
};

/// Compact post-target Magic Arrow control.  This is intentionally a normal
/// modal window rather than a second targeting mode: cancel returns to the
/// battle through the controller, while confirm submits the same generic hero
/// spell action with the authority-approved overcharge payload.
class MagicArrowOverchargeWindow final : public CWindowObject
{
	MagicArrowOverchargeContext context;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CSlider> slider;
	std::shared_ptr<CButton> minus;
	std::shared_ptr<CButton> plus;
	std::shared_ptr<CButton> confirmButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CLabel> overchargeLabel;
	std::shared_ptr<CLabel> costLabel;
	std::shared_ptr<CLabel> damageLabel;
	std::shared_ptr<CMultiLineLabel> targetLabel;
	std::shared_ptr<CLabel> stateLabel;

	MagicArrowOverchargeValues valuesFor(int overcharge) const;
	void setOvercharge(int overcharge);
	void refresh();
	void confirm();
	void cancel();

public:
	explicit MagicArrowOverchargeWindow(MagicArrowOverchargeContext context);

	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
