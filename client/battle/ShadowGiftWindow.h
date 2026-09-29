/*
 * ShadowGiftWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../windows/CWindowObject.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class CButton;
class CLabel;
class CMultiLineLabel;

/// Target-aware forecast for one Shadow Gift sacrifice choice. These values are
/// presentation only; the server validates the selected percentage and target.
struct ShadowGiftValues
{
	int sacrificePercent = 10;
	int64_t currentHealth = 0;
	int64_t currentHealthAfter = 0;
	int64_t maximumHealth = 0;
	int64_t maximumHealthAfter = 0;
	int32_t damageBonusBasisPoints = 0;
	bool legal = false;
	std::string targetDescription;
};

/// Context for the post-target Shadow Gift sacrifice choice.
struct ShadowGiftContext
{
	ShadowGiftValues initial;
	std::function<ShadowGiftValues(int)> evaluate;
	std::function<bool(int)> confirm;
	std::function<void()> cancel;
};

/// Compact native battle modal. It presents the cost of each bargain and only
/// submits an ordinary hero-spell request after an explicit confirmation.
class ShadowGiftWindow final : public CWindowObject
{
	ShadowGiftContext context;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::vector<std::shared_ptr<CButton>> choices;
	std::shared_ptr<CButton> confirmButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CMultiLineLabel> targetLabel;
	std::shared_ptr<CMultiLineLabel> costLabel;
	std::shared_ptr<CLabel> damageLabel;
	std::shared_ptr<CLabel> stateLabel;
	int selectedPercent = 10;

	ShadowGiftValues values() const;
	void select(int percent);
	void refresh();
	void confirm();
	void cancel();

public:
	explicit ShadowGiftWindow(ShadowGiftContext context);

	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
