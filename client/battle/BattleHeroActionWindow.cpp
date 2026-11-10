/*
 * BattleHeroActionWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/Unit.h"
#include "BattleInterface.h"
#include "BattleWindow.h"
#include "BattleActionsController.h"
#include "NewHorizonsBattleStatus.h"
#include "NewHorizonsOrderPresentation.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../windows/InfoWindows.h"
#include "../render/Colors.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

#include <algorithm>

using namespace newHorizonsOrderPresentation;

std::string HeroCommandUI::name(HeroCommand command)
{
	switch(command)
	{
		case HeroCommand::CHARGE: return "Charge";
		case HeroCommand::HOLD_THE_LINE: return "Hold the Line";
		case HeroCommand::FOCUS_FIRE: return "Focus Fire";
		case HeroCommand::RIPOSTE: return "Riposte";
		case HeroCommand::BRACE: return "Brace";
		case HeroCommand::PROTECT: return "Protect";
		case HeroCommand::FLANK: return "Flank";
		case HeroCommand::SECOND_WIND: return "Second Wind";
		case HeroCommand::FEINT: return "Feint";
		case HeroCommand::VANGUARD: return "Vanguard";
		default: return "None";
	}
}

BattleHeroActionWindow::BattleHeroActionWindow(const std::shared_ptr<BattleInterface> & owner, bool ordersOnlyMode)
	: CWindowObject(ordersOnlyMode ? SHADOW_DISABLED : 0,
		ordersOnlyMode ? ImagePath::builtin("newHorizonsOrdersBackground.png") : ImagePath::builtin("NH_hero_actions_back")), battle(owner), ordersOnly(ordersOnlyMode)
{
	if(ordersOnly)
	{
		pos.w = 640;
		pos.h = 500;
		pos = center();
	}
	OBJECT_CONSTRUCTION;
	if(ordersOnly)
	{
		createOrdersLayout();
		refresh();
		return;
	}
	labels.push_back(std::make_shared<CLabel>(320, 29, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Hero action"));
	labels.push_back(std::make_shared<CLabel>(320, 57, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Orders use the shared Hero Action; no Mana or Command is spent."));
	state = std::make_shared<CLabel>(320, 80, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");

	spellButton = std::make_shared<CButton>(Point(36, 110), AnimationPath::builtin("NH_spells_button"),
		CButton::tooltip("Spells", "Open the existing spellbook. Casting shares the hero's action with Orders."),
		[this] { chooseSpell(); });
	spellButton->setHoverable(true);
	labels.push_back(std::make_shared<CLabel>(120, 115, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::WHITE, "Spells"));
	labels.push_back(std::make_shared<CMultiLineLabel>(Rect(120, 143, 470, 38), FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "Your learned magic. Normal spellbook, mana and targeting requirements still apply."));

	const auto battleOwner = currentBattle();
	const auto visible = visibleCommandDisplays(battleOwner ? &battleOwner->getBattle()->getBattle()->getHeroCommandRules() : nullptr);
	for(size_t i = 0; i < visible.size(); ++i)
	{
		const auto & display = visible[i];
		const auto command = display.command;
		const Point position(30 + static_cast<int>(i % 4) * 150, 216 + static_cast<int>(i / 4) * 90);
		auto button = std::make_shared<CButton>(position, AnimationPath::builtin(display.image),
			CButton::tooltip(display.name, display.description), [this, command]
			{
				if(isTargeted(command))
					chooseTargetedCommand(command);
				else
					chooseCommand(command);
			});
		button->setHoverable(true);
		commands.emplace_back(command, button);
		labels.push_back(std::make_shared<CLabel>(position.x + 32, position.y - 14, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, display.name));
		effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(position.x - 34, position.y + 66, 140, 34), FONT_SMALL, ETextAlignment::TOPLEFT,
			Colors::WHITE, ""));
		addOrderDetailsButton(command, position + Point(74, 22), button, effectLabels.back());
	}
	labels.push_back(std::make_shared<CMultiLineLabel>(Rect(28, 463, 472, 41), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Commands affect current troops only; no war machines.\nLater summons and clones do not inherit effects."));
	cancel = std::make_shared<CButton>(Point(548, 445), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle without spending an action."), [this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	refresh();
}

void BattleHeroActionWindow::createOrdersLayout()
{
	// Orders share the native 64px four-state button contract.
	labels.push_back(std::make_shared<CLabel>(320, 27, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Orders"));
	labels.push_back(std::make_shared<CLabel>(320, 53, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "Orders use the shared Hero Action; no Mana or Command is spent."));
	state = std::make_shared<CLabel>(320, 78, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");
	const int orderWidth = 148;
	const int orderHeight = 126;
	const int orderStride = 156;
	const int orderTop = 93;
	const auto battleOwner = currentBattle();
	const auto visible = visibleCommandDisplays(battleOwner ? &battleOwner->getBattle()->getBattle()->getHeroCommandRules() : nullptr);
	for(size_t i = 0; i < visible.size(); ++i)
	{
		const auto & display = visible[i];
		const auto command = display.command;
		const int left = 8 + orderStride * static_cast<int>(i % 4);
		const int top = orderTop + 140 * static_cast<int>(i / 4);
		const Rect card(left, top, orderWidth, orderHeight);
		labels.push_back(std::make_shared<TransparentFilledRectangle>(card, ColorRGBA(35, 46, 56, 255), ColorRGBA(99, 111, 122, 255)));
		labels.push_back(std::make_shared<CLabel>(left + orderWidth / 2, top + 10, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, display.name));
		const Point icon(left + (orderWidth - 64) / 2, top + 27);
		auto button = std::make_shared<CButton>(icon, AnimationPath::builtin(display.image),
			CButton::tooltip(display.name, display.description), [this, command]
			{
				if(isTargeted(command))
					chooseTargetedCommand(command);
				else
					chooseCommand(command);
			});
		button->setHoverable(true);
		commands.emplace_back(command, button);
		effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(left + 6, top + 92, orderWidth - 12, 28), FONT_SMALL,
			ETextAlignment::TOPLEFT, Colors::WHITE, ""));
		addOrderDetailsButton(command, Point(left + orderWidth - 22, top + 5), button, effectLabels.back());
	}
	targetReadback = std::make_shared<CMultiLineLabel>(Rect(16, 378, 516, 30), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	labels.push_back(targetReadback);
	orderInstructions = std::make_shared<CMultiLineLabel>(Rect(16, 412, 516, 45), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Targeted Orders select stacks directly on the battlefield.\nRight-click/Escape cancels without spending an action.");
	const auto footer = battleOwner && heroCommands::isRevisedRules(
		battleOwner->getBattle()->getBattle()->getHeroCommandRules())
		? "Most Orders end this round; Vanguard uses displayed duration"
		: "Orders end with this round";
	labels.push_back(std::make_shared<CLabel>(320, 463, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, footer));
	cancel = std::make_shared<CButton>(Point(548, 443), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle without spending an action."), [this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
}

void BattleHeroActionWindow::cancelSelection()
{
	close(); // Inspecting or cancelling an ordinary Order never spends resources.
}

std::shared_ptr<BattleInterface> BattleHeroActionWindow::currentBattle() const
{
	auto result = battle.lock();
	if(!result || CPlayerInterface::battleInt != result || !result->curInt)
		return {};
	return result;
}

void BattleHeroActionWindow::setStateText(const std::string & text)
{
	// CLabel::setText schedules a parent redraw even when its text is unchanged.
	// refresh runs during show, so do not perpetually dirty the whole chooser.
	if(state->getText() != text)
		state->setText(text);
}

void BattleHeroActionWindow::refreshEffects(const CGHeroInstance & hero, const JsonNode & rules,
	int divineMandateEfficiencyBonusPercent)
{
	// Command and standing perks also affect these values. Re-read the shared
	// calculation on refresh; only changed labels request a redraw.
	for(size_t i = 0; i < commands.size(); ++i)
	{
		if(commands[i].first == HeroCommand::FEINT || commands[i].first == HeroCommand::VANGUARD)
			continue; // Refreshed from live preparation below, outside the legacy rating cache.
		// The snapshot's coefficient helper is the single source of truth for
		// the preview. The client only formats the returned numbers.
		const auto effects = commandEffects(rules, commands[i].first, hero,
			divineMandateEfficiencyBonusPercent);
		if(i < effectLabels.size() && effectLabels[i]->getText() != effects)
			effectLabels[i]->setText(effects);
	}
}

void BattleHeroActionWindow::chooseTargetedCommand(HeroCommand command)
{
	auto owner = currentBattle();
	if(!owner || !ordersOnly)
	{
		refresh();
		return;
	}
	if(!owner->makingTurn() || owner->curInt->isAutoFightOn || owner->isInTacticsMode()
		|| owner->actionsController->heroSpellcastingModeActive() || !owner->currentHero())
	{
		refresh();
		return;
	}
	const auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	if(!callback->battleCanBeginHeroCommand(side, command))
	{
		if(command == HeroCommand::PROTECT)
			owner->curInt->showInfoDialog("Protect unavailable. No legal Protector/Ward pair is available; the two friendly unit footprints must touch.");
		else
			owner->curInt->showInfoDialog(HeroCommandUI::name(command) + " unavailable. No legal target is available right now.");
		refresh();
		return;
	}
	if(!owner->actionsController->beginHeroOrderTargeting(command))
	{
		owner->curInt->showInfoDialog(HeroCommandUI::name(command) + " unavailable. Reopen Orders and try again.");
		refresh();
		return;
	}
	close();
}

void BattleHeroActionWindow::chooseCommand(HeroCommand command)
{
	auto owner = currentBattle();
	if(!owner)
	{
		close();
		return;
	}
	auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	if(!owner->makingTurn() || owner->curInt->isAutoFightOn || owner->isInTacticsMode() || owner->actionsController->heroSpellcastingModeActive() ||
		!callback->battleCanUseHeroCommand(side, command))
	{
		refresh();
		return;
	}
	const auto action = BattleAction::makeHeroCommand(side, command);
	const auto battleID = owner->getBattleID();
	auto playerCallback = owner->curInt->cb;
	// Never set the shared action budget or bonuses in the frontend.
	// The original hero-action request path validates again on the authority.
	close();
	playerCallback->battleMakeSpellAction(battleID, action);
}

void BattleHeroActionWindow::chooseSpell()
{
	if(ordersOnly)
		return;
	auto owner = currentBattle();
	if(!owner)
	{
		close();
		return;
	}
	// Button availability may have changed since the chooser was last drawn.
	// Preserve the chooser on refusal, just as chooseCommand does.
	auto callback = owner->getBattle();
	const auto * hero = owner->currentHero();
	const auto spellProblem = hero ? callback->battleCanCastSpell(hero, spells::Mode::HERO) : ESpellCastProblem::INVALID;
	if(!owner->makingTurn() || owner->curInt->isAutoFightOn || owner->isInTacticsMode() || owner->actionsController->heroSpellcastingModeActive() ||
		!hero || (spellProblem != ESpellCastProblem::OK && spellProblem != ESpellCastProblem::CASTS_PER_TURN_LIMIT))
	{
		refresh();
		return;
	}
	close();
	owner->windowObject->openSpellbook();
}

void BattleHeroActionWindow::show(Canvas & canvas)
{
	refresh();
	CWindowObject::show(canvas);
}

void BattleHeroActionWindow::showAll(Canvas & canvas)
{
	refresh();
	CWindowObject::showAll(canvas);
}
