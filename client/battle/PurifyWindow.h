/*
 * PurifyWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"

#include "../../lib/battle/BattleHex.h"
#include "../../lib/constants/EntityIdentifiers.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class CButton;
class CLabel;
class CListBox;
class CMultiLineLabel;
namespace newHorizonsPurify { struct EligibleStack; }

struct PurifyStackChoices
{
	int32_t unitId = -1;
	std::string stackName;
	int maximumSpellEffectChoices = 1;
	std::vector<std::pair<SpellID, std::string>> eligibleEffects;
	bool physicalPoisonAutomaticallyCleared = false;
	bool graveDustAutomaticallyCleared = false;
};

using PurifyEffectSelection = std::vector<std::pair<int32_t, SpellID>>;

/// Validate a UI selection against the freshly queried per-stack eligibility.
std::optional<PurifyEffectSelection> validatedPurifyChoices(
	const std::vector<newHorizonsPurify::EligibleStack> & currentEligible, const PurifyEffectSelection & choices);

struct PurifyContext
{
	BattleHex center;
	int maximumSpellEffectChoices = 1;
	bool purifierWillClearPhysicalPoison = false;
	bool purifierWillClearGraveDust = false;
	std::vector<PurifyStackChoices> stacks;
	std::function<bool(const PurifyEffectSelection &)> confirm;
	std::function<void()> cancel;
};

/// Lets the player choose eligible spell and bodily effects for each stack in
/// Purify's area. The window submits only after explicit confirmation.
class PurifyWindow final : public CWindowObject
{
public:
	struct SelectionModel;

private:

	PurifyContext context;
	std::shared_ptr<SelectionModel> selection;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CListBox> options;
	std::shared_ptr<CMultiLineLabel> stateLabel;
	std::shared_ptr<CButton> confirmButton;
	std::shared_ptr<CButton> cancelButton;

	void refresh();
	void confirm();
	void cancel();

public:
	explicit PurifyWindow(PurifyContext context);
};
