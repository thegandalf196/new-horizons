/*
 * HeroMasteryWindow.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "CWindowObject.h"
#include "HeroMasteryDialogState.h"
#include "../../lib/entities/hero/NewHorizonsMasteryRules.h"

class CButton;
class CLabel;

/// A saved three-option offer, never a fourth secondary-skill rank.
/// Submission is not completion: only the matching authoritative query resolution
/// closes this window. Rejected requests permit another explicit selection.
class HeroMasteryWindow : public CWindowObject
{
	newHorizonsHeroes::MasteryOffer offer;
	HeroMasteryDialogState state;
	std::function<int(int)> submit;
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::array<std::shared_ptr<CButton>, 3> choices;
	std::shared_ptr<CButton> confirm;
	std::shared_ptr<CLabel> status;

	void select(int index);
	void confirmSelection();
	void refreshControls();

public:
	HeroMasteryWindow(const newHorizonsHeroes::MasteryOffer & savedOffer, QueryID queryID, std::function<int(int)> callback);
	void requestApplied(uint32_t requestID, bool success);
	void queryResolved(QueryID queryID);
	void close() override;
};
