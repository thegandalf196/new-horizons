/*
 * FocusFireTargetWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "FocusFireTargetWindow.h"
#include "BattleHeroActionWindow.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "BattleInterface.h"
#include "BattleActionsController.h"
#include "../CPlayerInterface.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/MiscWidgets.h"
#include "../windows/InfoWindows.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/ObjectLists.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/callback/CCallback.h"

struct FocusFireSelectionContext
{
	std::weak_ptr<BattleInterface> battle;
	BattleID battleID;
	BattleSide side = BattleSide::NONE;
	HeroCommand command = HeroCommand::FOCUS_FIRE;
	std::optional<PlayerColor> player;
	int32_t round = -1;
	std::vector<uint32_t> targets;
	std::optional<uint32_t> selected;
	std::optional<uint32_t> first;
	bool submitted = false;
	bool closed = false;

	explicit FocusFireSelectionContext(const std::shared_ptr<BattleInterface> & owner, HeroCommand requestedCommand)
		: battle(owner), command(requestedCommand)
	{
		if(!owner || CPlayerInterface::battleInt != owner || !owner->curInt || !owner->curInt->cb)
			return;
		battleID = owner->getBattleID();
		side = owner->getBattle()->battleGetMySide();
		player = owner->curInt->cb->getPlayerID();
		round = owner->getBattle()->battleGetRound();
		if(currentBattle() && owner->getBattle()->battleCanBeginHeroCommand(side, command))
			targets = owner->getBattle()->battleGetHeroCommandTargets(side, command);
	}

	std::shared_ptr<BattleInterface> currentBattle() const
	{
		auto owner = battle.lock();
		if(closed || submitted || !owner || CPlayerInterface::battleInt != owner || !owner->curInt
			|| !player || !owner->curInt->cb || !owner->actionsController
			|| owner->getBattleID() != battleID || owner->curInt->cb->getPlayerID() != player
			|| owner->getBattle()->battleGetMySide() != side || owner->getBattle()->battleGetRound() != round
			|| !owner->makingTurn() || owner->curInt->isAutoFightOn || owner->isInTacticsMode()
			|| owner->actionsController->heroSpellcastingModeActive() || !owner->currentHero())
			return {};
		return owner;
	}
};

namespace
{
bool isPairOrder(HeroCommand command)
{
	return command == HeroCommand::PROTECT;
}

std::string commandTitle(HeroCommand command)
{
	return HeroCommandUI::name(command);
}

std::string targetText(const BattleInterface & owner, uint32_t id)
{
	// Pointer exists only during this synchronous display read; no captured hex.
	const auto * unit = owner.getBattle()->battleGetUnitByID(id);
	if(!unit)
		return "Target ID " + std::to_string(id) + " is no longer available";
	const auto position = unit->getPosition(); // Display only; never retained for confirmation.
	const std::string location = position.isValid()
		? " (row " + std::to_string(position.getY() + 1) + ", col " + std::to_string(position.getX() + 1) + ")"
		: " (off battlefield)";
	return "ID " + std::to_string(id) + ": " + std::to_string(unit->getCount()) + " " + unit->unitType()->getNamePluralTranslated() + location;
}

bool prepared(const FocusFireSelectionContext & context, const BattleInterface & owner,
	const std::vector<uint32_t> & ids)
{
	const auto & rules = owner.getBattle()->getBattle()->getHeroCommandRules();
	// Focus Fire predates the canonical eight-Order snapshot. Legacy targeted
	// saves must keep using their original value-only preparation path; the new
	// HeroOrderState validator intentionally rejects those snapshots.
	if(context.command == HeroCommand::FOCUS_FIRE && !heroCommands::isCanonicalRules(rules))
		return ids.size() == 1 && owner.getBattle()->battlePrepareFocusFireState(context.side, ids.front()).has_value();
	return owner.getBattle()->battlePrepareHeroOrderState(context.side, context.command, ids).has_value();
}

class FocusFireTargetRow : public CIntObject
{
	std::weak_ptr<FocusFireSelectionContext> selection;
	const uint32_t id;
	std::shared_ptr<CMultiLineLabel> text;
	std::shared_ptr<LRClickableArea> targetHelp;
	std::shared_ptr<CButton> selectButton;

	void refresh()
	{
		auto context = selection.lock();
		auto owner = context ? context->currentBattle() : nullptr;
		std::string description = owner ? targetText(*owner, id) : "Battle context changed";
		if(context && isPairOrder(context->command))
		{
			if(context->first && *context->first == id)
				description = "Protector: " + description;
			else if(context->selected && *context->selected == id)
				description = "Ward: " + description;
		}
		if(text->getText() != description)
			text->setText(description);
		bool legal = false;
		if(owner && context)
		{
			if(isPairOrder(context->command) && !context->first)
				legal = std::ranges::find(context->targets, id) != context->targets.end();
			else if(isPairOrder(context->command) && context->first && *context->first != id)
				legal = prepared(*context, *owner, {*context->first, id});
			else if(!isPairOrder(context->command))
				legal = prepared(*context, *owner, {id});
		}
		selectButton->block(!owner || !context || !legal);
		const std::string action = context && isPairOrder(context->command)
			? (context->first ? "Select Ward" : "Select Protector") : "Select target";
		selectButton->setHelp(CButton::tooltip(action,
			owner && context
				? (isPairOrder(context->command)
					? (context->first ? "Select an adjacent friendly Ward. The authority rechecks both IDs at Confirm." : "Select the friendly Protector first.")
					: "Select this exact unit ID. The authority rechecks it at Confirm.")
				: "Battle context changed."));
		if(context && (context->selected == id || context->first == id))
			selectButton->setBorderColor(Colors::YELLOW);
		else
			selectButton->setBorderColor(std::nullopt);
	}
public:
	FocusFireTargetRow(std::weak_ptr<FocusFireSelectionContext> context, uint32_t targetId) : selection(context), id(targetId)
	{
		pos.w = 568;
		pos.h = 36;
		OBJECT_CONSTRUCTION;
		text = std::make_shared<CMultiLineLabel>(Rect(0, 2, 476, 32), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
		// Read-only help stays reachable when selection is disabled. Revalidate the
		// weak context at popup time; never capture a unit pointer or issue an Order.
		targetHelp = std::make_shared<LRClickableArea>(Rect(0, 0, 476, 36), nullptr, [context, targetId]
		{
			auto selection = context.lock();
			auto owner = selection ? selection->currentBattle() : nullptr;
			CRClickPopup::createAndPush(owner ? targetText(*owner, targetId)
				: "Battle context changed. Close this list and reopen Orders for current targets.");
		});
		targetHelp->removeUsedEvents(LCLICK);
		selectButton = std::make_shared<CButton>(Point(488, 0), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Select target"), [context, targetId]
			{
				auto selection = context.lock();
				auto owner = selection ? selection->currentBattle() : nullptr;
				if(!owner || !selection)
					return;
				if(isPairOrder(selection->command))
				{
					if(!selection->first)
					{
						if(std::ranges::find(selection->targets, targetId) != selection->targets.end())
							selection->first = targetId;
					}
					else if(selection->first != targetId
						&& prepared(*selection, *owner, {*selection->first, targetId}))
						selection->selected = targetId;
				}
				else if(prepared(*selection, *owner, {targetId}))
					selection->selected = targetId;
			});
		selectButton->setTextOverlay("Select", FONT_SMALL, Colors::WHITE);
		selectButton->setHoverable(true);
		refresh();
	}
	void show(Canvas & to) override { refresh(); CIntObject::show(to); }
	void showAll(Canvas & to) override { refresh(); CIntObject::showAll(to); }
};
}

FocusFireTargetWindow::FocusFireTargetWindow(const std::shared_ptr<BattleInterface> & owner, HeroCommand command)
	: CWindowObject(SHADOW_DISABLED, ImagePath{}), selection(std::make_shared<FocusFireSelectionContext>(owner, command))
{
	pos.w = 640;
	pos.h = 500;
	pos = center();
	OBJECT_CONSTRUCTION;
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, 640, 500), ColorRGBA(24, 29, 35, 255)));
	decoration.push_back(std::make_shared<CLabel>(320, 27, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, commandTitle(command)));
	decoration.push_back(std::make_shared<CLabel>(320, 53, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE,
		isPairOrder(command)
			? "Select a Protector, then an adjacent Ward. One shared hero action; no mana."
			: "Select an exact target, then confirm. One shared hero action; no mana."));
	state = std::make_shared<CLabel>(320, 77, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");
	const std::weak_ptr<FocusFireSelectionContext> weak = selection;
	targets = std::make_shared<CListBox>([weak](size_t index) -> std::shared_ptr<CIntObject>
	{
		auto context = weak.lock();
		if(!context || index >= context->targets.size())
			return std::make_shared<CIntObject>();
		return std::make_shared<FocusFireTargetRow>(weak, context->targets[index]);
	}, Point(16, 96), Point(0, 36), 8, selection->targets.size(), 0,
		selection->targets.size() > 8 ? 1 : 0, Rect(584, 0, 288, 288)); // Vertical slider API uses width as length.
	selectedTarget = std::make_shared<CMultiLineLabel>(Rect(16, 392, 608, 36), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	confirmButton = std::make_shared<CButton>(Point(440, 448), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm " + commandTitle(command), "Confirm the selected ID(s) using current authority checks. No extra shot or creature activation is granted by the selector."),
		[this, weak]
		{
			auto context = weak.lock();
			if(context && !context->closed && !context->submitted)
				confirm();
		});
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
	confirmButton->setHoverable(true);
	cancelButton = std::make_shared<CButton>(Point(536, 448), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Close without issuing an Order or spending the shared hero action."),
		[this, weak]
		{
			auto context = weak.lock();
			if(context && !context->closed && !context->submitted)
			{
				context->closed = true;
				close();
			}
		}, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);
	backButton = std::make_shared<CButton>(Point(16, 448), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Back to Orders", "Discard selection and return to Orders if this battle context is still current. Nothing is spent."),
		[this, weak]
		{
			auto context = weak.lock();
			if(!context || context->closed || context->submitted)
				return;
			auto owner = context->currentBattle();
			context->closed = true;
			close();
			if(owner)
				ENGINE->windows().createAndPushWindow<BattleHeroActionWindow>(owner, true);
		});
	backButton->setTextOverlay("Back", FONT_SMALL, Colors::WHITE);
	backButton->setHoverable(true);
	refresh();
}

void FocusFireTargetWindow::refresh()
{
	auto owner = selection->currentBattle();
	bool legal = false;
	if(owner)
	{
		if(isPairOrder(selection->command))
			legal = selection->first && selection->selected && *selection->first != *selection->selected
				&& prepared(*selection, *owner, {*selection->first, *selection->selected});
		else
			legal = selection->selected
				&& prepared(*selection, *owner, {*selection->selected});
	}
	confirmButton->block(!legal);
	const std::string status = !owner ? "Context changed: cancel and reopen Orders."
		: (selection->targets.empty() ? "No legal targets at entry. Cancel and reopen to refresh."
			: (isPairOrder(selection->command) ? (selection->first && selection->selected
				? (legal ? "Protector and Ward are ready to confirm." : "Protector/Ward pair is no longer legal.")
				: (selection->first ? "Protector selected. Choose an adjacent Ward." : "Choose a Protector first."))
				: "Selection and cancellation spend nothing."));
	if(state->getText() != status)
		state->setText(status);
	const std::string description = !owner
		? "This list is inactive. Back or Cancel to leave; nothing will be issued automatically."
		: (isPairOrder(selection->command)
			? (selection->first
				? ("Protector " + targetText(*owner, *selection->first) + (selection->selected
					? "\nWard " + targetText(*owner, *selection->selected) + (legal ? "" : " - pair currently unavailable")
					: "\nChoose an adjacent Ward."))
				: "No Protector selected. Choose one from the list.")
			: (selection->selected
				? "Selected " + targetText(*owner, *selection->selected) + (legal ? "" : " - currently unavailable; no command sent here")
				: "No target selected. Choose one from the list."));
	if(selectedTarget->getText() != description)
		selectedTarget->setText(description);
}

void FocusFireTargetWindow::confirm()
{
	auto context = selection;
	auto owner = context->currentBattle();
	bool legal = false;
	if(owner)
	{
		if(isPairOrder(context->command))
			legal = context->first && context->selected && *context->first != *context->selected
				&& prepared(*context, *owner, {*context->first, *context->selected});
		else
			legal = context->selected
				&& prepared(*context, *owner, {*context->selected});
	}
	if(!owner || !legal)
	{
		refresh();
		return;
	}
	const auto action = isPairOrder(context->command)
		? BattleAction::makePairedHeroCommand(context->side, context->command, *context->first, *context->selected)
		: BattleAction::makeTargetedHeroCommand(context->side, context->command, *context->selected);
	const auto battleID = context->battleID;
	auto playerCallback = owner->curInt->cb;
	context->submitted = true; // Local duplicate-submit latch, not the authoritative action budget.
	context->closed = true;
	close();
	playerCallback->battleMakeSpellAction(battleID, action);
}

void FocusFireTargetWindow::show(Canvas & to)
{
	refresh();
	CWindowObject::show(to);
}

void FocusFireTargetWindow::showAll(Canvas & to)
{
	refresh();
	CWindowObject::showAll(to);
}
