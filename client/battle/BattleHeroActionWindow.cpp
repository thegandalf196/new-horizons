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
	labels.push_back(std::make_shared<CLabel>(320, 57, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "One per round: Spell or Order"));
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
	// Feint and Vanguard use the provisional neutral Orders control until their
	// dedicated art is selected. Historical Orders retain their painted icons. The golden gauntlet
	// remains the shared Orders entry button in the battle bar. The window's
	// generated H3 dialog texture supplies the frame and parchment-like depth;
	// do not cover it with the old flat grey rectangle.
	labels.push_back(std::make_shared<CLabel>(320, 27, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Orders"));
	labels.push_back(std::make_shared<CLabel>(320, 53, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "One shared hero action: Spell or Order"));
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
		const bool neutralIcon = command == HeroCommand::FEINT || command == HeroCommand::VANGUARD;
		const Point icon(left + (orderWidth - (neutralIcon ? 48 : 64)) / 2, top + 27 + (neutralIcon ? 14 : 0));
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
	if(auto owner = currentBattle())
	{
		const auto callback = owner->getBattle();
		const auto & crisis = callback->getBattle()->getCrisisCommandState();
		if(crisis.choice())
		{
			const auto * anchor = callback->battleGetStackByID(crisis.returns.back().anchor);
			if(!anchor) return;
			auto decline = BattleAction::makeNoAction(anchor);
			decline.side = crisis.chooser();
			close();
			owner->sendCommand(decline, anchor);
			return;
		}
		if(callback->battleHasPendingDoubleCommand(callback->battleGetMySide())
			|| callback->battleHasPendingPreCombatOrder(callback->battleGetMySide()))
			return;
	}
	close();
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
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent)
{
	const auto ratings = std::make_pair(hero.getPrimSkillLevel(PrimarySkill::ATTACK), hero.getPrimSkillLevel(PrimarySkill::DEFENSE));
	if(effectsInitialized && displayedRatings == ratings && displayedWarcastingBonus == warcastingBonusPercent
		&& displayedDivineMandateBonus == divineMandateEfficiencyBonusPercent)
		return;
	displayedRatings = ratings;
	displayedWarcastingBonus = warcastingBonusPercent;
	displayedDivineMandateBonus = divineMandateEfficiencyBonusPercent;
	effectsInitialized = true;

	for(size_t i = 0; i < commands.size(); ++i)
	{
		if(commands[i].first == HeroCommand::FEINT || commands[i].first == HeroCommand::VANGUARD)
			continue; // Refreshed from live preparation below, outside the legacy rating cache.
		// The snapshot's coefficient helper is the single source of truth for
		// the preview. The client only formats the returned numbers.
		const auto effects = commandEffects(rules, commands[i].first, hero, warcastingBonusPercent,
			divineMandateEfficiencyBonusPercent);
		if(i < effectLabels.size())
			effectLabels[i]->setText(effects);
		const auto & display = commandDisplay(commands[i].first);
		const std::string coverage = isTargeted(commands[i].first)
			? "\n\nTarget selection is revalidated by the authority at Confirm."
			: "\n\nThe authority applies the Order to eligible current troops; later summons and clones do not inherit it.";
		commands[i].second->setHelp(CButton::tooltip(display.name,
			std::string(display.description) + (effects.empty() ? "" : "\n\nCurrent effect: " + effects)
			+ combinedArmsHelp(rules, commands[i].first, &hero)
			+ "\nOne shared hero action; no mana." + coverage));
	}
}

void BattleHeroActionWindow::refresh()
{
	auto owner = currentBattle();
	const bool pendingDoubleCommand = owner && owner->getBattle()->battleHasPendingDoubleCommand(
		owner->getBattle()->battleGetMySide());
	const bool pendingPreCombatOrder = owner && owner->getBattle()->battleHasPendingPreCombatOrder(
		owner->getBattle()->battleGetMySide());
	const bool pendingCrisis = owner && owner->getBattle()->getBattle()->getCrisisCommandState().choice();
	const bool pendingOrder = pendingDoubleCommand || pendingPreCombatOrder || pendingCrisis;
	if(cancel)
		cancel->block(pendingOrder && !pendingCrisis);
	if(!owner)
	{
		if(spellButton)
			spellButton->block(true);
		for(auto & entry : commands)
		{
			entry.second->block(true);
			entry.second->setBorderColor(std::nullopt);
			entry.second->setHelp(CButton::tooltip(HeroCommandUI::name(entry.first),
				"Battle no longer available. Close this window."));
		}
		if(targetReadback)
			targetReadback->setText("Battle no longer available");
		setStateText("Battle no longer available. Close this window.");
		return;
	}
	auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	const auto divineMandateStatus = callback->battleGetDivineMandateStatus(side);
	const auto & divineMandateFollowup = divineMandateStatus.pendingFollowup;
	const bool pendingDivineMandateSpell = divineMandateFollowup
		&& divineMandateFollowup->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
		&& divineMandateFollowup->allowance == HeroActionAllowanceState::AllowanceKind::SPELL;
	std::string actionOpportunityContext;
	const auto orderSelection = callback->battleUsesHeroCommands()
		? callback->battleGetOrderActionAllowance(side)
		: std::optional<HeroActionAllowanceState::Selection>{};
	if(orderSelection)
		actionOpportunityContext = orderActionOpportunityText(*orderSelection);
	else if(pendingDivineMandateSpell)
		actionOpportunityContext = "Divine Mandate Light Spell follow-up available through the end of round "
			+ std::to_string(divineMandateFollowup->expiryRound) + ".";
	const bool canAct = owner->makingTurn() && !owner->curInt->isAutoFightOn && !owner->isInTacticsMode() && !owner->actionsController->heroSpellcastingModeActive();
	const auto * hero = owner->currentHero();
	const auto & rules = callback->getBattle()->getHeroCommandRules();
	if(hero)
	{
		const auto preparedHold = callback->battlePrepareHeroOrderState(side, HeroCommand::HOLD_THE_LINE, {});
		refreshEffects(*hero, rules, preparedHold ? preparedHold->warcastingBonusPercent : 0,
			preparedHold ? preparedHold->divineMandateEfficiencyBonusPercent() : 0);
	}
	const auto spellProblem = hero ? callback->battleCanCastSpell(hero, spells::Mode::HERO) : ESpellCastProblem::INVALID;
	const bool canSpell = spellButton && hero
		&& (spellProblem == ESpellCastProblem::OK || spellProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT);
	if(spellButton)
		spellButton->block(!canAct || !canSpell || pendingCrisis);
	const auto actionCounts = callback->battleUsesHeroCommands()
		? callback->battleHeroActionAllowanceCounts(side)
		: HeroActionAllowanceState::Counts{};
	const bool normalHeroActionAvailable = callback->battleUsesHeroCommands() && actionCounts.heroActions > 0;
	bool anyCommand = false;
	const auto orderBudgetSpent = [&]
	{
		if(callback->battleUsesHeroCommands())
		{
			const auto round = callback->battleGetRound();
			const auto & allowances = callback->getBattle()->getHeroActionAllowances(side);
			return round < 0 || allowances.currentRound != round
				|| !allowances.eligibleAllowance(HeroActionAllowanceState::ActionKind::ORDER, round);
		}
		return callback->getBattle()->getHeroCommandUsed(side) || callback->battleCastSpells(side) != 0;
	};
	const auto commonReason = [&]
	{
		if(!callback->battleUsesHeroCommands())
			return std::string("Orders are not enabled in this battle.");
		if(!hero)
			return std::string("No commanding hero is available.");
		if(owner->curInt->isAutoFightOn)
			return std::string("Autofight controls this battle.");
		if(owner->isInTacticsMode())
			return std::string("Orders are unavailable during tactics.");
		if(owner->actionsController->heroSpellcastingModeActive())
			return std::string("Finish or cancel spell targeting first.");
		if(!owner->makingTurn())
			return std::string("It is not your turn.");
		if(orderBudgetSpent())
		{
			if(!actionOpportunityContext.empty())
				return actionOpportunityContext;
			return std::string("No Hero or Order action is available.");
		}
		return std::string();
	};
	for(size_t i = 0; i < commands.size(); ++i)
	{
		auto & entry = commands[i];
		const bool supported = heroCommands::supportedByRules(rules, entry.first);
		const bool targeted = isTargeted(entry.first);
		const bool commonAvailable = commonReason().empty();
		const bool targetReady = !targeted || !supported || !canAct || !hero
			? false : callback->battleCanBeginHeroCommand(side, entry.first);
		const bool hasTargets = targeted && supported && canAct && hero
			&& !callback->battleGetHeroCommandTargets(side, entry.first).empty();
		const bool available = supported && canAct && hero
			&& (targeted ? targetReady : callback->battleCanUseHeroCommand(side, entry.first));
		const bool protectPairUnavailable = entry.first == HeroCommand::PROTECT && targeted && supported
			&& commonAvailable && canAct && hero && !targetReady;
		// Keep the Protect control clickable in this one disabled state so its
		// activation-time recheck can explain that no legal footprint pair exists.
		// It remains visibly marked unavailable and never submits a packet.
		entry.second->block(!available && !protectPairUnavailable);
		// CButton::block removes SHOW_POPUP along with left-click/key input.
		// Restore only right-click help so unavailable Orders remain inspectable
		// without making a disabled command issuable.
		entry.second->addUsedEvents(SHOW_POPUP);
		anyCommand |= available;
		std::string reason = commonReason();
		if(reason.empty() && !supported)
			reason = "This Order is not available in the battle's saved rules.";
		if(reason.empty() && targeted && !targetReady)
			reason = entry.first == HeroCommand::PROTECT
			? "No legal Protector/Ward pair is available; the two friendly unit footprints must touch."
			: (hasTargets ? "The authority currently rejects this target requirement."
				: "No legal targets are available right now.");
		if(reason.empty() && !available)
			reason = "The authority currently rejects this Order's requirements.";
		// Re-prepare revised Orders on every refresh so Leadership, perk, saved-rule
		// and allowance changes cannot leave a stale numerical preview.
		const auto revisedPreview = revisedOrderPreview(*callback, side, entry.first, hero);
		if(!revisedPreview.label.empty() && i < effectLabels.size()
			&& effectLabels[i]->getText() != revisedPreview.label)
			effectLabels[i]->setText(revisedPreview.label);
		const auto & display = commandDisplay(entry.first);
		std::string flankReadback;
		if(entry.first == HeroCommand::FLANK)
		{
			const auto activeFlank = callback->battleGetHeroOrderState(side, HeroCommand::FLANK);
			if(activeFlank)
			{
				const auto * target = activeFlank->flankFor(activeFlank->primaryTargetUnitId);
				flankReadback = "\n\nContributing attack sides relative to the marked target: ";
				flankReadback += newHorizonsFlankReadback::formatDirections(target ? target->sideMask : 0);
				flankReadback += ".";
			}
		}
		entry.second->setHelp(CButton::tooltip(display.name,
			std::string(display.description) + (reason.empty() ? "\n\nReady: choose this Order." : "\n\nDisabled: " + reason)
			+ flankReadback + revisedPreview.help
			+ revisedActiveHelp(callback->battleGetHeroOrderState(side, entry.first), callback->battleGetRound())
			+ combinedArmsHelp(rules, entry.first, hero)
			+ (actionOpportunityContext.empty() ? "" : "\n\n" + actionOpportunityContext)
			+ "\nSpends an available Hero or Order action; no mana."));
		if(protectPairUnavailable)
			entry.second->setBorderColor(Colors::ORANGE);
		else if(callback->battleGetHeroOrderState(side, entry.first))
			entry.second->setBorderColor(Colors::YELLOW);
		else
			entry.second->setBorderColor(std::nullopt);
	}
	const auto order = callback->battleGetActiveOrder(side);
	if(targetReadback)
	{
		const auto readback = activeOrderReadback(*callback, side, rules, order);
		if(targetReadback->getText() != readback)
			targetReadback->setText(readback);
	}
	std::string availability = normalHeroActionAvailable
		? "Normal Hero Action available" : "Normal Hero Action spent";
	if(ordersOnly)
	{
		// These are observed UI phases and authoritative shared-budget fields,
		// not reconstructed eligibility rules or a promise that issuance succeeds.
		if(!callback->battleUsesHeroCommands())
			availability = "Orders unavailable in this battle";
		else if(!hero)
			availability = "No hero available";
		else if(owner->curInt->isAutoFightOn)
			availability = "Autofight controls this battle";
		else if(owner->isInTacticsMode())
			availability = "Unavailable during tactics";
		else if(owner->actionsController->heroSpellcastingModeActive())
			availability = "Finish or cancel spell targeting";
		else if(!owner->makingTurn())
			availability = "Not your turn";
		else if(orderBudgetSpent())
			availability = "No Hero or Order action available";
		else
			availability = anyCommand ? "Order available" : "No Order currently available";
	}
	std::string stateText = pendingCrisis
		? "Crisis Command: choose one free Order now, or Cancel to decline"
		: pendingPreCombatOrder
		? "Battle Plan: choose a free opening Order now"
		: pendingDoubleCommand
		? "Double Command: choose a different Order now"
		: "Order: " + HeroCommandUI::name(order) + " | " + availability;
	if(!actionOpportunityContext.empty())
		stateText += " | " + actionOpportunityContext;
	setStateText(stateText);
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
