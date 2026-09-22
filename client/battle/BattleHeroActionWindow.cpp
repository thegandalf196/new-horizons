/*
 * BattleHeroActionWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/battle/Unit.h"
#include "BattleInterface.h"
#include "BattleWindow.h"
#include "BattleActionsController.h"
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

namespace
{
struct CommandDisplay
{
	HeroCommand command;
	const char * image;
	const char * name;
	const char * description;
};
const std::array<CommandDisplay, 8> commandDisplays = {{
	{HeroCommand::CHARGE, "NH_charge_button", "Charge", "The first melee attack after moving at least 3 hexes gains bonus damage; extra movement adds more."},
	{HeroCommand::FOCUS_FIRE, "NH_focusFire_button", "Focus Fire", "Choose one enemy stack. Friendly shooters gain damage against it and reduce range and obstacle penalties."},
	{HeroCommand::RIPOSTE, "NH_riposte_button", "Riposte", "Friendly stacks take less melee damage and deal increased retaliation damage this round."},
	{HeroCommand::HOLD_THE_LINE, "NH_holdTheLine_button", "Hold the Line", "Friendly stacks take reduced physical damage while they remain in their issued positions."},
	{HeroCommand::BRACE, "NH_brace_button", "Brace", "Friendly stacks pre-emptively attack enemies that moved at least 3 hexes before a melee attack."},
	{HeroCommand::PROTECT, "NH_protect_button", "Protect", "Choose a Protector and adjacent Ward. The first melee attack against the Ward is redirected."},
	{HeroCommand::FLANK, "NH_flank_button", "Flank", "Choose one enemy stack. Friendly melee damage increases from additional distinct attack sides."},
	{HeroCommand::SECOND_WIND, "NH_secondWind_button", "Second Wind", "Choose a friendly stack that already completed its normal activation for an additional activation at reduced direct damage."}
}};

const CommandDisplay & commandDisplay(HeroCommand command)
{
	for(const auto & display : commandDisplays)
		if(display.command == command)
			return display;
	return commandDisplays.front();
}

std::vector<CommandDisplay> visibleCommandDisplays(const JsonNode * rules)
{
	std::vector<CommandDisplay> result;
	for(const auto & display : commandDisplays)
	{
		if(!rules || heroCommands::supportedByRules(*rules, display.command))
			result.push_back(display);
	}
	return result;
}

bool isTargeted(HeroCommand command)
{
	return command == HeroCommand::FOCUS_FIRE || command == HeroCommand::PROTECT
		|| command == HeroCommand::FLANK || command == HeroCommand::SECOND_WIND;
}

std::string percentText(int value)
{
	return (value > 0 ? "+" : "") + std::to_string(value) + "%";
}

std::string effectLabel(const std::string & key, int value)
{
	if(key == "meleeDamagePercent")
		return "Melee " + percentText(value);
	if(key == "rangedDamagePercent")
		return "Ranged " + percentText(value);
	if(key == "damageReductionPercent")
		return "Taken " + percentText(-value);
	if(key == "meleeDamageReductionPercent")
		return "Melee taken " + percentText(-value);
	if(key == "speedPercent")
		return "Speed " + percentText(value);
	if(key == "retaliationDamagePercent")
		return "Retaliation " + percentText(value);
	if(key == "preemptiveAttackPercent")
		return "Pre-emptive " + std::to_string(value) + "%";
	if(key == "preemptiveDamagePercent")
		return "Pre-emptive " + std::to_string(value) + "%";
	if(key == "interceptedDamageReductionPercent")
		return "Intercepted taken " + percentText(-value);
	if(key == "secondWindDamagePercent")
		return "Extra activation " + std::to_string(value) + "%";
	if(key == "additionalActivationDamagePercent")
		return "Extra activation " + std::to_string(value) + "%";
	if(key == "sideDamagePercent")
		return "Each extra side " + percentText(value);
	if(key == "additionalSidePercent")
		return "Each extra side " + percentText(value);
	return key + " " + percentText(value);
}

std::string commandEffects(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero)
{
	const auto & effects = rules["commands"][heroCommands::key(command)]["effects"];
	if(!effects.isStruct())
		return {};
	std::string result;
	for(const auto & [key, formula] : effects.Struct())
	{
		if(!formula.isStruct() || !formula["base"].isNumber() || !formula["attack"].isNumber() || !formula["defense"].isNumber())
			continue;
		try
		{
			const int value = command == HeroCommand::SECOND_WIND && key == "additionalActivationDamagePercent"
				? heroCommands::secondWindPercent(hero)
				: heroCommands::coefficient(formula, hero);
			const auto line = effectLabel(key, value);
			if(!result.empty())
				result += '\n';
			result += line;
		}
		catch(const std::exception &)
		{
			// The battle snapshot has already been validated by the authority. A
			// malformed optional preview must never make the client window fail.
		}
	}
	return result;
}

}

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
		CButton::tooltip("Cancel", "Return to battle and clear any Perfect Moment declaration without spending an action."), [this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	createPerfectMomentControl();
	refresh();
}

void BattleHeroActionWindow::createOrdersLayout()
{
	// Every Order has a distinct provisional painted icon. The golden gauntlet
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
	}
	targetReadback = std::make_shared<CMultiLineLabel>(Rect(16, 378, 516, 30), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	labels.push_back(targetReadback);
	orderInstructions = std::make_shared<CMultiLineLabel>(Rect(16, 412, 516, 45), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Targeted Orders select stacks directly on the battlefield. Protect uses two clicks: Protector, then adjacent Ward.\nRight-click/Escape cancels without spending the shared hero action.");
	labels.push_back(std::make_shared<CLabel>(320, 463, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Orders end with this round"));
	cancel = std::make_shared<CButton>(Point(548, 443), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle and clear any Perfect Moment declaration without spending an action."), [this] { cancelSelection(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	createPerfectMomentControl();
}

void BattleHeroActionWindow::createPerfectMomentControl()
{
	// Reuse the standard checkbox in the existing footer slot. This is a troop
	// attack declaration, independent of the shared Spell/Order action budget.
	perfectMomentToggle = std::make_shared<CToggleButton>(Point(16, 414), AnimationPath::builtin("sysopchk.def"),
		CButton::tooltip("Perfect Moment — next attack",
			"Once per combat, declare your next eligible melee or ranged attack a Lucky Strike. Select to return to battle armed; select again to disarm. No Hero Action is spent. Escape, right-click, another action or a stack change cancels the declaration. The authority consumes the use only when the declared strike happens."),
		[this](bool selected)
		{
			auto owner = currentBattle();
			if(!owner || !owner->canArmPerfectMoment())
			{
				refresh();
				return;
			}
			owner->setPerfectMomentArmed(selected);
			close(); // selecting is not Cancel: retain the local declaration
		});
	perfectMomentToggle->setHoverable(true);
	perfectMomentLabel = std::make_shared<CMultiLineLabel>(Rect(48, 414, 480, 32), FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "Perfect Moment — next attack\nOnce per combat; no Hero Action.");
}

void BattleHeroActionWindow::cancelSelection()
{
	if(auto owner = currentBattle())
		owner->clearPerfectMoment();
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

void BattleHeroActionWindow::refreshEffects(const CGHeroInstance & hero, const JsonNode & rules)
{
	const auto ratings = std::make_pair(hero.getPrimSkillLevel(PrimarySkill::ATTACK), hero.getPrimSkillLevel(PrimarySkill::DEFENSE));
	if(effectsInitialized && displayedRatings == ratings)
		return;
	displayedRatings = ratings;
	effectsInitialized = true;

	for(size_t i = 0; i < commands.size(); ++i)
	{
		// The snapshot's coefficient helper is the single source of truth for
		// the preview. The client only formats the returned numbers.
		const auto effects = commandEffects(rules, commands[i].first, hero);
		if(i < effectLabels.size())
			effectLabels[i]->setText(effects);
		const auto & display = commandDisplay(commands[i].first);
		const std::string coverage = isTargeted(commands[i].first)
			? "\n\nTarget selection is revalidated by the authority at Confirm."
			: "\n\nThe authority applies the Order to eligible current troops; later summons and clones do not inherit it.";
		commands[i].second->setHelp(CButton::tooltip(display.name,
			std::string(display.description) + (effects.empty() ? "" : "\n\nCurrent effect: " + effects)
			+ "\nOne shared hero action; no mana." + coverage));
	}
}

void BattleHeroActionWindow::refresh()
{
	auto owner = currentBattle();
	const bool perfectMomentAvailable = owner && owner->canArmPerfectMoment();
	if(perfectMomentToggle->isDisabled() == perfectMomentAvailable)
		perfectMomentToggle->CIntObject::setEnabled(perfectMomentAvailable);
	if(perfectMomentLabel->isDisabled() == perfectMomentAvailable)
		perfectMomentLabel->setEnabled(perfectMomentAvailable);
	const bool selected = perfectMomentAvailable && owner->isPerfectMomentArmed();
	if(perfectMomentToggle->isSelected() != selected)
		perfectMomentToggle->setSelectedSilent(selected);
	perfectMomentToggle->block(!perfectMomentAvailable);
	if(orderInstructions && orderInstructions->isDisabled() != perfectMomentAvailable)
		orderInstructions->setEnabled(!perfectMomentAvailable);
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
	const bool canAct = owner->makingTurn() && !owner->curInt->isAutoFightOn && !owner->isInTacticsMode() && !owner->actionsController->heroSpellcastingModeActive();
	const auto * hero = owner->currentHero();
	if(hero)
		refreshEffects(*hero, callback->getBattle()->getHeroCommandRules());
	const auto spellProblem = hero ? callback->battleCanCastSpell(hero, spells::Mode::HERO) : ESpellCastProblem::INVALID;
	const bool canSpell = spellButton && hero
		&& (spellProblem == ESpellCastProblem::OK || spellProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT);
	if(spellButton)
		spellButton->block(!canAct || !canSpell);
	bool anyCommand = false;
	const auto & rules = callback->getBattle()->getHeroCommandRules();
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
		if(callback->getBattle()->getHeroCommandUsed(side) || callback->battleCastSpells(side) != 0)
			return std::string("The shared hero action has already been spent.");
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
		const auto & display = commandDisplay(entry.first);
		entry.second->setHelp(CButton::tooltip(display.name,
			std::string(display.description) + (reason.empty() ? "\n\nReady: choose this Order." : "\n\nDisabled: " + reason)
			+ "\nOne shared hero action; no mana."));
		if(protectPairUnavailable)
			entry.second->setBorderColor(Colors::ORANGE);
		else if(entry.first == callback->battleGetActiveOrder(side))
			entry.second->setBorderColor(Colors::YELLOW);
		else
			entry.second->setBorderColor(std::nullopt);
	}
	const auto order = callback->battleGetActiveOrder(side);
	if(targetReadback)
	{
		std::string readback = "Targeted Orders: Focus Fire/Flank choose an enemy; Protect chooses Protector then Ward; Second Wind chooses a spent friendly activation.";
		if(const auto active = callback->battleGetHeroOrderState(side))
		{
			readback = "Active Order: " + HeroCommandUI::name(active->command) + " (round " + std::to_string(active->issuedRound) + ")";
			if(active->command == HeroCommand::HOLD_THE_LINE)
				readback += " | anchored stacks: " + std::to_string(active->anchors.size());
			else if(active->command == HeroCommand::FOCUS_FIRE)
				readback += " | target " + std::to_string(active->primaryTargetUnitId);
			else if(active->command == HeroCommand::PROTECT)
				readback += " | Protector " + std::to_string(active->primaryTargetUnitId) + " -> Ward " + std::to_string(active->secondaryTargetUnitId)
					+ (active->protectIntercepted ? " | interception spent" : " | interception ready");
			else if(active->command == HeroCommand::FLANK && !active->flankTargets.empty())
			{
				unsigned sides = active->flankTargets.front().sideMask;
				int sideCount = 0;
				while(sides)
				{
					sideCount += sides & 1u;
					sides >>= 1;
				}
				readback += " | target " + std::to_string(active->flankTargets.front().unitId)
					+ " | sides " + std::to_string(sideCount);
			}
			else if(active->command == HeroCommand::SECOND_WIND)
				readback += " | stack " + std::to_string(active->primaryTargetUnitId)
					+ (active->secondWindActive ? " | extra activation active" : " | pending activation");
		}
		else if(order != HeroCommand::NONE)
			readback = "Active Order: " + HeroCommandUI::name(order) + ". Targeted effects remain subject to current unit state.";
		if(targetReadback->getText() != readback)
			targetReadback->setText(readback);
	}
	std::string availability = (anyCommand || (canAct && canSpell))
		? "Hero action available" : "Hero action spent or unavailable";
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
		else if(callback->getBattle()->getHeroCommandUsed(side) || callback->battleCastSpells(side) != 0)
			availability = "Shared hero action spent";
		else
			availability = anyCommand ? "Order available" : "No Order currently available";
	}
	setStateText("Order: " + HeroCommandUI::name(order) + " | " + availability);
}

void BattleHeroActionWindow::chooseTargetedCommand(HeroCommand command)
{
	auto owner = currentBattle();
	if(owner)
		owner->clearPerfectMoment();
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
			CRClickPopup::createAndPush("Protect unavailable. No legal Protector/Ward pair is available; the two friendly unit footprints must touch.");
		else
			CRClickPopup::createAndPush(HeroCommandUI::name(command) + " unavailable. No legal target is available right now.");
		refresh();
		return;
	}
	if(!owner->actionsController->beginHeroOrderTargeting(command))
	{
		CRClickPopup::createAndPush(HeroCommandUI::name(command) + " unavailable. Reopen Orders and try again.");
		refresh();
		return;
	}
	close();
}

void BattleHeroActionWindow::chooseCommand(HeroCommand command)
{
	auto owner = currentBattle();
	if(owner)
		owner->clearPerfectMoment();
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
	if(owner)
		owner->clearPerfectMoment();
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
