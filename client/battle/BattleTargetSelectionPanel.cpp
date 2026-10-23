/*
 * BattleTargetSelectionPanel.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleTargetSelectionPanel.h"

#include "BattleActionsController.h"
#include "BattleConsole.h"
#include "BattleInterface.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "render/Canvas.h"

BattleTargetSelectionPanel::BattleTargetSelectionPanel(BattleInterface & owner_)
	: CIntObject(0), owner(owner_)
{
	pos = Rect(0, 0, 720, 62);
	center();
	OBJECT_CONSTRUCTION;
	// Match the battle hero/status compartments: one continuous DiBoxBck
	// leather surface, a warm outer rim, and the red-and-brass inset frame.
	decoration.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, 720, 62)));
	const ColorRGBA transparent(0, 0, 0, 0);
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, 720, 62),
		transparent, ColorRGBA(213, 185, 117)));
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(1, 1, 718, 60),
		transparent, ColorRGBA(145, 18, 12), 2));
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(3, 3, 714, 56),
		transparent, ColorRGBA(120, 98, 56)));
	// Recess the compact target forecast separately while leaving the
	// surrounding leather uninterrupted behind the title and tactile buttons.
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(8, 24, 456, 32),
		ColorRGBA(0, 0, 0, 75), ColorRGBA(82, 65, 40, 255)));
	compactDecoration.push_back(std::make_shared<CFilledTexture>(
		ImagePath::builtin("DiBoxBck"), Rect(0, 0, 500, 42)));
	compactDecoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, 500, 42),
		transparent, ColorRGBA(213, 185, 117)));
	compactDecoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(1, 1, 498, 40),
		transparent, ColorRGBA(145, 18, 12), 2));
	compactDecoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(3, 3, 494, 36),
		transparent, ColorRGBA(120, 98, 56)));
	for(const auto & item : compactDecoration)
		item->setEnabled(false);
	heading = std::make_shared<CLabel>(12, 7, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "Storm of Daggers");
	targetReadback = std::make_shared<CMultiLineLabel>(Rect(12, 25, 450, 32), FONT_TINY,
		ETextAlignment::TOPLEFT, Colors::WHITE, "");

	undoButton = std::make_shared<CButton>(Point(474, 15), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Undo", "Remove the last selected target."), [this]
		{
			if(owner.actionsController)
			{
				if(owner.actionsController->stormOfDaggersTargetSelectionModeActive())
					owner.actionsController->undoStormOfDaggersTarget();
				else if(owner.actionsController->soulChainTargetSelectionModeActive())
					owner.actionsController->undoSoulChainTarget();
				else
					owner.actionsController->undoRepeatedPlacement();
			}
		});
	undoButton->setTextOverlay("Undo", FONT_SMALL, Colors::WHITE);
	undoButton->setHoverable(true);

	cancelButton = std::make_shared<CButton>(Point(554, 15), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Cancel this selection without spending Mana or the Hero Action."), [this]
		{
			if(owner.actionsController)
				owner.actionsController->endCastingSpell();
		}, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);

	confirmButton = std::make_shared<CButton>(Point(634, 15), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm", "Revalidate the complete selection, then cast the spell."), [this]
		{
			if(owner.actionsController)
			{
				if(owner.actionsController->stormOfDaggersTargetSelectionModeActive())
					owner.actionsController->confirmStormOfDaggersTargets();
				else if(owner.actionsController->soulChainTargetSelectionModeActive())
					owner.actionsController->confirmSoulChainTargets();
				else
					owner.actionsController->confirmRepeatedPlacement();
			}
		});
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);

	refresh();
}

void BattleTargetSelectionPanel::refresh()
{
	const auto * controller = owner.actionsController.get();
	const bool repeatedPlacement = controller && controller->repeatedPlacementModeActive();
	const bool storm = controller && controller->stormOfDaggersTargetSelectionModeActive();
	const bool soulChain = controller && controller->soulChainTargetSelectionModeActive();
	active = repeatedPlacement || storm || soulChain;
	if(!controller || !active)
	{
		stormPreviewLogged = false;
		loggedStormPreview.clear();
		loggedStormCanConfirm = false;
		undoButton->setEnabled(false);
		cancelButton->setEnabled(false);
		confirmButton->setEnabled(false);
		return;
	}

	setCompactMode(storm);
	if(repeatedPlacement)
		refreshPlacement();
	else if(soulChain)
		refreshSoulChain();
	else
		refreshStorm();
}

void BattleTargetSelectionPanel::refreshPlacement()
{
	const auto * controller = owner.actionsController.get();

	const auto & selectedHexes = controller->getRepeatedPlacementSelectedHexes();
	const int required = controller->repeatedPlacementRequiredHexes();
	const int remaining = std::max(0, required - static_cast<int>(selectedHexes.size()));
	const std::string placementName = controller->quicksandPlacementModeActive()
		? "Quicksand patches" : "Land Mine mines";
	const std::string title = placementName + "  |  " + std::to_string(selectedHexes.size())
		+ "/" + std::to_string(required) + " selected, " + std::to_string(remaining) + " remaining";
	if(heading->getText() != title)
		heading->setText(title);

	std::string readback;
	for(size_t index = 0; index < selectedHexes.size(); ++index)
	{
		if(!readback.empty())
			readback += ";  ";
		readback += "#" + std::to_string(index + 1) + " hex "
			+ std::to_string(selectedHexes[index].toInt());
	}

	if(!controller->repeatedPlacementReady())
	{
		if(!readback.empty())
			readback += ".  ";
		readback += "Click legal empty hexes in order; Backspace removes the last; Esc cancels.";
	}
	else
	{
		if(!readback.empty())
			readback += ".  ";
		readback += "All placements selected. Confirm to cast, or Undo to revise.";
	}

	if(targetReadback->getText() != readback)
		targetReadback->setText(readback);
	undoButton->setEnabled(!selectedHexes.empty());
	undoButton->block(selectedHexes.empty());
	cancelButton->setEnabled(true);
	confirmButton->setEnabled(true);
	confirmButton->block(!controller->repeatedPlacementReady());
}

void BattleTargetSelectionPanel::refreshSoulChain()
{
	const auto * controller = owner.actionsController.get();

	const auto preview = controller->getSoulChainSelectionPreview();
	std::string title = "Soul Chain  |  ";
	if(preview.selectedTargetCount == 0)
		title += "select primary enemy (0/3)";
	else
		title += "primary + " + std::to_string(preview.selectedTargetCount - 1) + "/2 secondary";
	if(heading->getText() != title)
		heading->setText(title);

	std::string readback;
	if(preview.targets.empty())
		readback = "Select a primary enemy, then up to two linked enemies.";
	else
	{
		readback = "Primary: " + preview.targets.front().name;
		if(preview.targets.size() > 1)
		{
			readback += " | Linked: ";
			for(size_t index = 1; index < preview.targets.size(); ++index)
			{
				if(index > 1)
					readback += ", ";
				readback += preview.targets[index].name;
			}
		}
	}
	readback += "\nConfirm to cast / Undo last / Esc cancels";

	if(targetReadback->getText() != readback)
		targetReadback->setText(readback);
	undoButton->setEnabled(preview.selectedTargetCount > 0);
	undoButton->block(preview.selectedTargetCount == 0);
	cancelButton->setEnabled(true);
	confirmButton->setEnabled(true);
	confirmButton->block(!preview.canConfirm);
}

void BattleTargetSelectionPanel::refreshStorm()
{
	const auto * controller = owner.actionsController.get();
	const auto preview = controller->getStormOfDaggersSelectionPreview();
	const auto title = "Storm of Daggers | " + std::to_string(preview.selectedTargetCount)
		+ "/" + std::to_string(preview.maximumTargetCount);
	if(heading->getText() != title)
		heading->setText(title);

	// The same targets can acquire new defenses or become illegal. Compare the
	// complete readback, and cache before appendBattleLog can reenter this update.
	const auto previewLog = stormPreviewLog(preview);
	if(!stormPreviewLogged || loggedStormPreview != previewLog
		|| loggedStormCanConfirm != preview.canConfirm)
	{
		stormPreviewLogged = true;
		loggedStormPreview = previewLog;
		loggedStormCanConfirm = preview.canConfirm;
		owner.appendBattleLog(previewLog);
	}
	undoButton->setEnabled(true);
	undoButton->block(preview.selectedTargetCount == 0);
	cancelButton->setEnabled(true);
	confirmButton->setEnabled(true);
	confirmButton->block(!preview.canConfirm);
}

std::string BattleTargetSelectionPanel::stormPreviewLog(const StormOfDaggersSelectionPreview & preview)
{
	std::string message = "Storm of Daggers preview: "
		+ std::to_string(preview.selectedTargetCount) + "/"
		+ std::to_string(preview.maximumTargetCount) + " targets.";
	if(preview.poolAvailable)
		message += " Pool " + std::to_string(preview.totalDamagePool) + "; "
			+ std::to_string(preview.rawDamagePerTarget) + " per target before resistance.";
	for(size_t index = 0; index < preview.targets.size(); ++index)
	{
		const auto & target = preview.targets[index];
		message += "\n#" + std::to_string(index + 1) + " " + target.name + ": ";
		if(target.projectedDamage)
			message += "about " + std::to_string(*target.projectedDamage) + " damage.";
		else
			message += "damage estimate unavailable.";
	}
	message += "\n" + preview.status;
	return message;
}

void BattleTargetSelectionPanel::setCompactMode(bool compact)
{
	if(compactMode == compact)
		return;
	compactMode = compact;
	pos.w = compact ? 500 : 720;
	pos.h = compact ? 42 : 62;
	for(const auto & item : decoration)
		item->setEnabled(!compact);
	for(const auto & item : compactDecoration)
		item->setEnabled(compact);
	targetReadback->setEnabled(!compact);
	heading->moveTo(Point(pos.x + 12, pos.y + (compact ? 14 : 7)));
	undoButton->moveTo(Point(pos.x + (compact ? 254 : 474), pos.y + (compact ? 5 : 15)));
	cancelButton->moveTo(Point(pos.x + (compact ? 334 : 554), pos.y + (compact ? 5 : 15)));
	confirmButton->moveTo(Point(pos.x + (compact ? 414 : 634), pos.y + (compact ? 5 : 15)));
	reposition();
}

void BattleTargetSelectionPanel::reposition()
{
	center();
	if(compactMode && owner.console)
		moveTo(Point(pos.x, owner.console->pos.y - pos.h - 4));
}

void BattleTargetSelectionPanel::update()
{
	refresh();
}

void BattleTargetSelectionPanel::show(Canvas & to)
{
	if(active)
		CIntObject::showAll(to);
}

void BattleTargetSelectionPanel::showAll(Canvas & to)
{
	if(active)
		CIntObject::showAll(to);
}
