/*
 * StackTemporaryStatusPresenter.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "NewHorizonsBattleStatus.h"
#include "ArcaneInspirationPresentation.h"
#include "ElementalAttunementPresentation.h"
#include "../../lib/Point.h"

class CStack;
class CLabel;
class CAnimImage;
class CPicture;
class CIntObject;
class LRClickableAreaWText;

/// Plans the shared three-slot strip and renders stack conditions that are not spells.
/// Spell-effect readback remains owned by StackInfoBasicPanel.
class StackTemporaryStatusPresenter
{
public:
	struct Entry
	{
		newHorizonsBattleStatus::StackStatusIconKind kind;
		std::optional<SpellID> spell;
		bool arcaneMarks = false;
		bool inspiration = false;
	};
	struct Widgets
	{
		std::vector<std::shared_ptr<CLabel>> & labels;
		std::vector<std::shared_ptr<CAnimImage>> & icons;
		std::vector<std::shared_ptr<CPicture>> & temporaryCreatureIcons;
		std::vector<std::shared_ptr<CIntObject>> & physicalStatusIcons;
		std::vector<std::shared_ptr<LRClickableAreaWText>> & statusTooltips;
	};
	StackTemporaryStatusPresenter(const CStack * stack,
		const newHorizonsBattleStatus::StackInfoStatusSnapshot & status, const CBattleInfoCallback * battle);
	const Entry & entry(std::size_t index) const { return entries.at(index); }
	const newHorizonsBattleStatus::StackStatusDisplayPlan & plan() const { return displayPlan; }
	bool isEmpty() const { return empty; }
	bool hasPriorityStatus() const;
	bool append(std::size_t index, const Point & position, Widgets widgets) const;

private:
	const CStack * stack;
	const newHorizonsBattleStatus::StackInfoStatusSnapshot & status;
	arcaneInspirationPresentation::Readout inspiration;
	elementalAttunementPresentation::Readout attunement;
	std::vector<Entry> entries;
	newHorizonsBattleStatus::StackStatusDisplayPlan displayPlan;
	std::size_t totalEffectCount = 0;
	bool empty = true;
};
