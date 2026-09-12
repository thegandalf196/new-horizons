/*
 * BattleHeroActionWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"
#include "BattleWindow.h"
#include "BattleActionsController.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../gui/WindowHandler.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
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
const std::array<CommandDisplay, 5> commandDisplays = {{
	{HeroCommand::CHARGE, "NH_charge_button", "Charge", "Increase melee damage for this round. Costs one hero action, no mana.", Point(77, 216)},
	{HeroCommand::HOLD_THE_LINE, "NH_holdTheLine_button", "Hold the Line", "Reduce physical damage this round. This preview does not require standing still.", Point(287, 216)},
	{HeroCommand::ADVANCE, "NH_advance_button", "Advance", "Increase troop movement for this round. Costs one hero action, no mana.", Point(497, 216)},
	{HeroCommand::AGGRESSIVE, "NH_aggressive_button", "Aggressive", "Increase melee and ranged damage, but also physical damage taken. Persists in this battle. Waiting remains allowed.", Point(115, 363)},
	{HeroCommand::DEFENSIVE, "NH_defensive_button", "Defensive", "Reduce physical damage taken and movement. Persists in this battle until changed.", Point(375, 363)}
}};

}

std::string HeroCommandUI::name(HeroCommand command)
{
	switch(command)
	{
		case HeroCommand::CHARGE: return "Charge";
		case HeroCommand::HOLD_THE_LINE: return "Hold the Line";
		case HeroCommand::ADVANCE: return "Advance";
		case HeroCommand::AGGRESSIVE: return "Aggressive";
		case HeroCommand::DEFENSIVE: return "Defensive";
		default: return "None";
	}
}

BattleHeroActionWindow::BattleHeroActionWindow(const std::shared_ptr<BattleInterface> & owner, bool ordersOnlyMode)
	: CWindowObject(0, ImagePath::builtin("NH_hero_actions_back")), battle(owner), ordersOnly(ordersOnlyMode)
{
	OBJECT_CONSTRUCTION;
	labels.push_back(std::make_shared<CLabel>(320, 29, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, ordersOnly ? "Orders and Doctrines" : "Hero action"));
	labels.push_back(std::make_shared<CLabel>(320, 57, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE, "One per round: Spell, Order or Doctrine change"));
	state = std::make_shared<CLabel>(320, 80, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "");

	if(ordersOnly)
	{
		labels.push_back(std::make_shared<CMultiLineLabel>(Rect(36, 110, 568, 66), FONT_SMALL, ETextAlignment::TOPLEFT,
			Colors::WHITE, "Orders require no mana or spellbook. Issuing one spends the same hero action as a spell or Doctrine change.\nUse the separate Spellbook control for magic. Reading this panel or cancelling spends nothing."));
	}
	else
	{
		spellButton = std::make_shared<CButton>(Point(36, 110), AnimationPath::builtin("NH_spells_button"),
			CButton::tooltip("Spells", "Open the existing spellbook. Casting shares the hero's action with Orders and Doctrine changes."),
			[this] { chooseSpell(); });
		spellButton->setHoverable(true);
		labels.push_back(std::make_shared<CLabel>(120, 115, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::WHITE, "Spells"));
		labels.push_back(std::make_shared<CMultiLineLabel>(Rect(120, 143, 470, 38), FONT_SMALL, ETextAlignment::TOPLEFT,
			Colors::WHITE, "Your learned magic. Normal spellbook, mana and targeting requirements still apply."));
	}

	for(const auto & display : commandDisplays)
	{
		const auto command = display.command;
		auto button = std::make_shared<CButton>(display.position, AnimationPath::builtin(display.image),
			CButton::tooltip(display.name, display.description), [this, command] { chooseCommand(command); });
		button->setHoverable(true);
		commands.emplace_back(command, button);
		if(command == HeroCommand::AGGRESSIVE || command == HeroCommand::DEFENSIVE)
		{
			labels.push_back(std::make_shared<CLabel>(display.position.x + 75, 370, FONT_MEDIUM, ETextAlignment::TOPLEFT, Colors::WHITE, display.name));
			effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(display.position.x + 75, 394, 163, 64), FONT_SMALL, ETextAlignment::TOPLEFT,
				Colors::WHITE, ""));
		}
		else
		{
			labels.push_back(std::make_shared<CLabel>(display.position.x + 32, 202, FONT_MEDIUM, ETextAlignment::CENTER, Colors::WHITE, display.name));
			effectLabels.push_back(std::make_shared<CMultiLineLabel>(Rect(display.position.x - 54, 287, 172, 42), FONT_SMALL, ETextAlignment::TOPLEFT,
				Colors::WHITE, ""));
		}
	}
	labels.push_back(std::make_shared<CLabel>(320, 348, FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW, "Doctrines persist on troops present when issued"));
	labels.push_back(std::make_shared<CMultiLineLabel>(Rect(28, 463, 472, 41), FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Commands affect current troops only; no war machines.\nLater summons and clones do not inherit effects."));
	cancel = std::make_shared<CButton>(Point(548, 445), AnimationPath::builtin("NH_cancel_button"),
		CButton::tooltip("Cancel", "Return to battle without spending a hero action."), [this] { close(); }, EShortcut::GLOBAL_CANCEL);
	cancel->setHoverable(true);
	refresh();
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
		const std::string doctrineHelp = heroCommands::isDoctrine(commands[i].first)
			? " Switch to the other Doctrine to affect new arrivals. The active Doctrine cannot be selected again."
			: "";
		commands[i].second->setHelp(CButton::tooltip(commandDisplays[i].name,
			std::string(commandDisplays[i].description) + "\n\n" + effects +
			"\nCurrent hero values; one shared hero action, no mana." + coverage + doctrineHelp));
	}
}

void BattleHeroActionWindow::refresh()
{
	auto owner = currentBattle();
	if(!owner)
	{
		if(spellButton)
			spellButton->block(true);
		for(auto & entry : commands)
			entry.second->block(true);
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
	const auto doctrine = callback->battleGetActiveDoctrine(side);
	const auto order = callback->battleGetActiveOrder(side);
	setStateText("Doctrine: " + HeroCommandUI::name(doctrine) + " | Order: " + HeroCommandUI::name(order) + " | " +
		(ordersOnly ? (anyCommand ? "Order available" : "No Order currently available")
			: ((anyCommand || (canAct && canSpell)) ? "Hero action available" : "Hero action spent or unavailable")));
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
	// Never set the shared action budget, bonuses or Doctrine in the frontend.
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
