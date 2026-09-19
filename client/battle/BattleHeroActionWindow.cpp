/*
 * BattleHeroActionWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "FocusFireTargetWindow.h"
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
	Point position;
};
const std::array<CommandDisplay, 2> commandDisplays = {{
	{HeroCommand::CHARGE, "NH_charge_button", "Charge", "Increase melee damage for this round. Costs one hero action, no mana.", Point(77, 216)},
	{HeroCommand::HOLD_THE_LINE, "NH_holdTheLine_button", "Hold the Line", "Reduce physical damage this round. This preview does not require standing still.", Point(392, 216)}
}};

}

std::string HeroCommandUI::name(HeroCommand command)
{
	switch(command)
	{
		case HeroCommand::CHARGE: return "Charge";
		case HeroCommand::HOLD_THE_LINE: return "Hold the Line";
		case HeroCommand::FOCUS_FIRE: return "Focus Fire";
		default: return "None";
	}
}

BattleHeroActionWindow::BattleHeroActionWindow(const std::shared_ptr<BattleInterface> & owner, bool ordersOnlyMode)
	: CWindowObject(ordersOnlyMode ? SHADOW_DISABLED : 0,
		ordersOnlyMode ? ImagePath{} : ImagePath::builtin("NH_hero_actions_back")), battle(owner), ordersOnly(ordersOnlyMode)
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

	for(const auto & display : commandDisplays)
	{
		const auto command = display.command;
		auto button = std::make_shared<CButton>(display.position, AnimationPath::builtin(display.image),
			CButton::tooltip(display.name, display.description), [this, command] { chooseCommand(command); });
		button->setHoverable(true);
		commands.emplace_back(command, button);
		labels.push_back(std::make_shared<CLabel>(display.position.x + 32, 202, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, display.name));
		effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(display.position.x - 54, 287, 172, 42), FONT_SMALL, ETextAlignment::TOPLEFT,
			Colors::WHITE, ""));
	}
	labels.push_back(std::make_shared<CMultiLineLabel>(Rect(28, 463, 472, 41), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Commands affect current troops only; no war machines.\nLater summons and clones do not inherit effects."));
	cancel = std::make_shared<CButton>(Point(548, 445), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle without spending a hero action."), [this] { close(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	refresh();
}

void BattleHeroActionWindow::createOrdersLayout()
{
	// Code-only layout prototype. Replace materials only after independent art
	// review; no baked labels or use of the old 520px backdrop in this mode.
	labels.push_back(std::make_shared<TransparentFilledRectangle>(Rect(0, 0, 640, 500), ColorRGBA(24, 30, 37, 255), ColorRGBA(156, 132, 85, 255)));
	labels.push_back(std::make_shared<CLabel>(320, 27, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Orders"));
	labels.push_back(std::make_shared<CLabel>(320, 53, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "One shared hero action: Spell or Order"));
	state = std::make_shared<CLabel>(320, 78, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");
	const auto owner = currentBattle();
	const bool showFocus = owner && owner->getBattle()->getBattle()
		&& heroCommands::supportedByRules(owner->getBattle()->getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE);
	const int orderWidth = showFocus ? 140 : 192;
	const int orderStride = showFocus ? 156 : 208;
	int orderIndex = 0;
	for(const auto & display : commandDisplays)
	{
		const auto command = display.command;
		const int left = 16 + orderStride * orderIndex++;
		const Rect card(left, 94, orderWidth, 150);
		labels.push_back(std::make_shared<TransparentFilledRectangle>(card, ColorRGBA(35, 46, 56, 255), ColorRGBA(99, 111, 122, 255)));
		const Point icon(left + (orderWidth - 64) / 2, 122);
		auto button = std::make_shared<CButton>(icon, AnimationPath::builtin(display.image),
			CButton::tooltip(display.name, display.description), [this, command] { chooseCommand(command); });
		button->setHoverable(true);
		commands.emplace_back(command, button);
		labels.push_back(std::make_shared<CLabel>(left + orderWidth / 2, 104, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, display.name));
		effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(left + 8, 196, orderWidth - 16, 44), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, ""));
	}
	if(showFocus)
	{
		labels.push_back(std::make_shared<TransparentFilledRectangle>(Rect(484, 94, 140, 150), ColorRGBA(35, 46, 56, 255), ColorRGBA(99, 111, 122, 255)));
		labels.push_back(std::make_shared<CLabel>(554, 104, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, "Focus Fire"));
		// Existing core generated button is a prototype control, NOT original final Focus artwork.
		focusButton = std::make_shared<CButton>(Point(514, 138), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Choose Focus Fire target", "Select an exact enemy unit, then confirm. Adds at the Archery stage for primary physical ranged hits this round, not as a final damage multiplier. The eligible current friendly ordinary shooter cohort is frozen at issue, including blocked, empty-ammo and already-acted shooters. Later arrivals do not join. At least one legal shot is required at issue; this grants no shot or activation. No mana or spellbook required."),
			[this] { chooseFocusFire(); });
		focusButton->setTextOverlay("Targets", FONT_SMALL, Colors::WHITE);
		focusButton->setHoverable(true);
		focusEffect = std::make_shared<CMultiLineLabel>(Rect(492, 196, 124, 44), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
		focusReadback = std::make_shared<CMultiLineLabel>(Rect(16, 378, 516, 54), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
		labels.push_back(std::make_shared<CMultiLineLabel>(Rect(16, 438, 516, 48), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
			"Help/cancel and Focus target selection are free.\nOther command icons issue immediately; Focus requires Confirm.\nAccepted commands use the shared action, no mana/book. See help for coverage."));
	}
	else
		labels.push_back(std::make_shared<CMultiLineLabel>(Rect(16, 378, 516, 102), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
			"Orders need no mana or spellbook. Use the separate Spellbook control for magic.\n\nCommands affect living ordinary friendly troops present when issued, not war machines. Later summons and clones do not inherit effects. Reading or cancelling spends nothing."));
	labels.push_back(std::make_shared<CLabel>(320, 257, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Orders end with this round"));
	cancel = std::make_shared<CButton>(Point(548, 416), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle without spending a hero action."), [this] { close(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
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

	const auto signedPercent = [](int value)
	{
		return (value > 0 ? "+" : "") + std::to_string(value) + "%";
	};
	for(size_t i = 0; i < commands.size(); ++i)
	{
		std::string effects;
		// Use the authority's coefficient, rounding and safety-cap implementation
		// against this battle's saved rules, never a second frontend formula.
		for(const auto & bonus : heroCommands::bonuses(rules, commands[i].first, hero))
		{
			std::string line;
			if(bonus.type == BonusType::PERCENTAGE_DAMAGE_BOOST)
				line = std::string(bonus.subtype == BonusCustomSubtype::damageTypeRanged ? "Ranged damage " : "Melee damage ") + signedPercent(static_cast<int>(bonus.val));
			else if(bonus.type == BonusType::GENERAL_DAMAGE_REDUCTION)
				line = "Physical taken " + signedPercent(-static_cast<int>(bonus.val));
			else if(bonus.type == BonusType::STACKS_SPEED)
				line = "Base speed " + signedPercent(static_cast<int>(bonus.val));
			if(!line.empty())
			{
				if(!effects.empty())
					effects += '\n';
				effects += line;
			}
		}
		effectLabels[i]->setText(effects);
		const std::string coverage = "\n\nAffects only living ordinary friendly troops present when issued; war machines are excluded. Later summons and clones do not inherit these effects.";
		commands[i].second->setHelp(CButton::tooltip(commandDisplays[i].name,
			std::string(commandDisplays[i].description) + "\n\n" + effects +
			"\nCurrent hero values; one shared hero action, no mana." + coverage));
	}
}

void BattleHeroActionWindow::refresh()
{
	auto owner = currentBattle();
	if(!owner)
	{
		if(spellButton)
			spellButton->block(true);
		if(focusButton)
		{
			focusButton->block(true);
			focusButton->setBorderColor(std::nullopt);
			if(focusReadback->getText() != "Battle no longer available")
				focusReadback->setText("Battle no longer available");
		}
		for(auto & entry : commands)
		{
			entry.second->block(true);
			entry.second->setBorderColor(std::nullopt);
		}
		setStateText("Battle no longer available. Close this window.");
		return;
	}
	auto callback = owner->getBattle();
	const auto side = callback->battleGetMySide();
	const bool canAct = owner->makingTurn() && !owner->curInt->isAutoFightOn && !owner->isInTacticsMode() && !owner->actionsController->heroSpellcastingModeActive();
	const auto * hero = owner->currentHero();
	if(hero)
		refreshEffects(*hero, callback->getBattle()->getHeroCommandRules());
	const bool canSpell = spellButton && hero && callback->battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK;
	if(spellButton)
		spellButton->block(!canAct || !canSpell);
	bool anyCommand = false;
	for(auto & entry : commands)
	{
		const bool available = canAct && callback->battleCanUseHeroCommand(side, entry.first);
		entry.second->block(!available);
		anyCommand |= available;
	}
	const auto order = callback->battleGetActiveOrder(side);
	if(focusButton)
	{
		const bool available = canAct && hero && callback->battleCanBeginHeroCommand(side, HeroCommand::FOCUS_FIRE);
		focusButton->block(!available);
		anyCommand |= available;
		if(order == HeroCommand::FOCUS_FIRE)
			focusButton->setBorderColor(Colors::YELLOW);
		else
			focusButton->setBorderColor(std::nullopt);
		std::string effect = "No hero available";
		if(hero)
		{
			const auto & formula = callback->getBattle()->getHeroCommandRules()["commands"][heroCommands::key(HeroCommand::FOCUS_FIRE)]["effects"]["rangedDamagePercent"];
			const int percent = heroCommands::coefficient(formula, hero->getPrimSkillLevel(PrimarySkill::ATTACK), hero->getPrimSkillLevel(PrimarySkill::DEFENSE));
			effect = "Ranged " + std::string(percent >= 0 ? "+" : "") + std::to_string(percent) + "%\nArchery stage";
		}
		if(focusEffect->getText() != effect)
			focusEffect->setText(effect);
		std::string readback = "Focus Fire: no retained mark. Targets opens selection without spending.";
		if(const auto mark = callback->battleGetFocusFireState(side))
		{
			const auto * target = callback->battleGetUnitByID(mark->targetUnitId);
			const std::string name = target ? target->unitType()->getNamePluralTranslated() : "unavailable unit";
			readback = "Focus Fire: ID " + std::to_string(mark->targetUnitId) + " " + name
				+ " | round " + std::to_string(mark->issuedRound) + " | " + std::to_string(mark->rangedDamagePercent)
				+ "% | cohort " + std::to_string(mark->recipientUnitIds.size()) + " at issue\n"
				+ (callback->battleIsFocusFireTargetActive(side) ? "Target active; no promise of ammunition or remaining activations." : "Mark retained but target currently inactive; no target substitution.");
		}
		if(focusReadback->getText() != readback)
			focusReadback->setText(readback);
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
		for(auto & entry : commands)
		{
			if(entry.first == order)
				entry.second->setBorderColor(Colors::YELLOW);
			else
				entry.second->setBorderColor(std::nullopt);
		}
	}
	setStateText("Order: " + HeroCommandUI::name(order) + " | " + availability);
}

void BattleHeroActionWindow::chooseFocusFire()
{
	auto owner = currentBattle();
	if(!owner || !ordersOnly || !focusButton || !owner->makingTurn() || owner->curInt->isAutoFightOn
		|| owner->isInTacticsMode() || owner->actionsController->heroSpellcastingModeActive() || !owner->currentHero()
		|| !owner->getBattle()->battleCanBeginHeroCommand(owner->getBattle()->battleGetMySide(), HeroCommand::FOCUS_FIRE))
	{
		refresh();
		return;
	}
	close();
	ENGINE->windows().createAndPushWindow<FocusFireTargetWindow>(owner);
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
	if(!owner->makingTurn() || owner->curInt->isAutoFightOn || owner->isInTacticsMode() || owner->actionsController->heroSpellcastingModeActive() ||
		!hero || callback->battleCanCastSpell(hero, spells::Mode::HERO) != ESpellCastProblem::OK)
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
