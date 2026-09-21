/*
 * BattleWindow.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleWindow.h"

#include "BattleActionsController.h"
#include "BattleConsole.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "BattleStacksController.h"
#include "HeroInfoWindow.h"
#include "QuickSpellPanel.h"
#include "BattleHeroActionWindow.h"
#include "StackInfoBasicPanel.h"
#include "StackQueue.h"
#include "UnitActionPanel.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../adventureMap/CInGameConsole.h"
#include "../adventureMap/TurnTimerWidget.h"
#include "../gui/CursorHandler.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "render/CAnimation.h"
#include "render/Canvas.h"
#include "render/IRenderHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../windows/CCreatureWindow.h"
#include "../windows/CMarketWindow.h"
#include "../windows/CMessage.h"
#include "../windows/CSpellWindow.h"
#include "../windows/InfoWindows.h"
#include "../windows/settings/SettingsMainWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/entities/artifact/CArtHandler.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/mapping/CMapHeader.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
constexpr int ordersControlPitch = 51;
}

BattleWindow::BattleWindow(BattleInterface & Owner)
	: owner(Owner)
{
	OBJECT_CONSTRUCTION;
	pos.w = 800;
	pos.h = 600;
	pos = center();

	PlayerColor defenderColor = owner.getBattle()->getBattle()->getSidePlayer(BattleSide::DEFENDER);
	PlayerColor attackerColor = owner.getBattle()->getBattle()->getSidePlayer(BattleSide::ATTACKER);
	bool isDefenderHuman = defenderColor.isValidPlayer() && GAME->interface()->cb->getStartInfo()->playerInfos.at(defenderColor).isControlledByHuman();
	bool isAttackerHuman = attackerColor.isValidPlayer() && GAME->interface()->cb->getStartInfo()->playerInfos.at(attackerColor).isControlledByHuman();
	onlyOnePlayerHuman = isDefenderHuman != isAttackerHuman;

	REGISTER_BUILDER("battleConsole", &BattleWindow::buildBattleConsole);
	
	const JsonNode config(JsonPath::builtin("config/widgets/BattleWindow2.json"));
	
	addShortcut(EShortcut::BATTLE_TOGGLE_QUICKSPELL, [this](){ this->toggleStickyQuickSpellVisibility();});
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_0,  [this](){ useSpellIfPossible(0);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_1,  [this](){ useSpellIfPossible(1);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_2,  [this](){ useSpellIfPossible(2);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_3,  [this](){ useSpellIfPossible(3);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_4,  [this](){ useSpellIfPossible(4);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_5,  [this](){ useSpellIfPossible(5);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_6,  [this](){ useSpellIfPossible(6);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_7,  [this](){ useSpellIfPossible(7);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_8,  [this](){ useSpellIfPossible(8);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_9,  [this](){ useSpellIfPossible(9);  });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_10, [this](){ useSpellIfPossible(10); });
	addShortcut(EShortcut::BATTLE_SPELL_SHORTCUT_11, [this](){ useSpellIfPossible(11); });

	addShortcut(EShortcut::GLOBAL_OPTIONS, std::bind(&BattleWindow::bOptionsf, this));
	addShortcut(EShortcut::BATTLE_SURRENDER, std::bind(&BattleWindow::bSurrenderf, this));
	addShortcut(EShortcut::BATTLE_RETREAT, std::bind(&BattleWindow::bFleef, this));
	addShortcut(EShortcut::BATTLE_AUTOCOMBAT, std::bind(&BattleWindow::bAutofightf, this));
	addShortcut(EShortcut::BATTLE_END_WITH_AUTOCOMBAT, std::bind(&BattleWindow::endWithAutocombat, this));
	addShortcut(EShortcut::BATTLE_CAST_SPELL, std::bind(&BattleWindow::bSpellf, this));
	addShortcut(EShortcut::BATTLE_WAIT, std::bind(&BattleWindow::bWaitf, this));
	addShortcut(EShortcut::BATTLE_DEFEND, std::bind(&BattleWindow::bDefencef, this));
	addShortcut(EShortcut::BATTLE_CONSOLE_UP, std::bind(&BattleWindow::bConsoleUpf, this));
	addShortcut(EShortcut::BATTLE_CONSOLE_DOWN, std::bind(&BattleWindow::bConsoleDownf, this));
	addShortcut(EShortcut::BATTLE_TACTICS_NEXT, std::bind(&BattleWindow::bTacticNextStack, this));
	addShortcut(EShortcut::BATTLE_TACTICS_END, std::bind(&BattleWindow::bTacticPhaseEnd, this));
	addShortcut(EShortcut::BATTLE_OPEN_ACTIVE_UNIT, std::bind(&BattleWindow::bOpenActiveUnit, this));
	addShortcut(EShortcut::BATTLE_OPEN_HOVERED_UNIT, std::bind(&BattleWindow::bOpenHoveredUnit, this));

	addShortcut(EShortcut::BATTLE_TOGGLE_GRID, [this](){ this->toggleBattleSetting("cellBorders"); });
	addShortcut(EShortcut::BATTLE_TOGGLE_MOUSE_SHADOW, [this](){ this->toggleBattleSetting("mouseShadow"); });
	addShortcut(EShortcut::BATTLE_TOGGLE_MOVEMENT_SHADOW, [this](){ this->toggleBattleSetting("stackRange"); });
	addShortcut(EShortcut::BATTLE_TOGGLE_STACK_INFO, [this](){ this->toggleStackInfoWindowsVisibility(); });

	addShortcut(EShortcut::BATTLE_TOGGLE_QUEUE, [this](){ this->toggleQueueVisibility();});
	addShortcut(EShortcut::BATTLE_TOGGLE_HEROES_STATS, [this](){ this->toggleStickyHeroWindowsVisibility();});
	addShortcut(EShortcut::BATTLE_USE_CREATURE_SPELL, [this](){ this->owner.actionsController->enterCreatureCastingMode(); });
	addShortcut(EShortcut::GLOBAL_ACCEPT, [this](){
		if(this->owner.actionsController)
			this->owner.actionsController->confirmLandMinePlacement();
	});
	addShortcut(EShortcut::GLOBAL_BACKSPACE, [this](){
		if(this->owner.actionsController)
			this->owner.actionsController->undoLandMinePlacement();
	});
	addShortcut(EShortcut::GLOBAL_CANCEL, [this]()
	{
		if(this->owner.actionsController->metamagicFollowupModeActive())
			this->owner.declineMetamagicFollowup();
		else
			this->owner.actionsController->endCastingSpell();
	});
	setShortcutBlocked(EShortcut::GLOBAL_ACCEPT, true);
	setShortcutBlocked(EShortcut::GLOBAL_BACKSPACE, true);
	addShortcut(EShortcut::ADVENTURE_QUICK_LOAD, [this](){
		//allow quick load only on player turn while no animations are ongoing
		if (!this->owner.hasAnimations() && this->owner.stacksController->getActiveStack())
			GAME->interface()->proposeQuickLoadingGame(); });

	build(config);
	// Non-modal mouse/touch confirmation for ordered Land Mine placement.  The
	// battlefield must remain clickable, so this is a lightweight child of the
	// battle window rather than a dialog.  Existing artwork is an intentional
	// placeholder until the dedicated placement controls are polished.
	// Reuse the command-panel Wait slot while placement blocks normal unit
	// actions.  This keeps the control outside the battlefield's event region
	// and prevents one click from also selecting an underlying hex.
	landMineConfirmButton = std::make_shared<CButton>(Point(697, 560), AnimationPath::builtin("icm005"),
		CButton::tooltip("Place mines", "Confirm the selected Land Mine hexes."), [this]()
		{
			if(owner.actionsController)
				owner.actionsController->confirmLandMinePlacement();
		});
	addWidget("nhLandMineConfirm", landMineConfirmButton);
	landMineConfirmButton->setEnabled(false);
	metamagicDeclineButton = std::make_shared<CButton>(Point(697, 560), AnimationPath::builtin("NH_hero_actions_entry"),
		CButton::tooltip("Decline / End Metamagic", "End the pending Metamagic sequence without spending another Hero Action."), [this]()
		{
			owner.declineMetamagicFollowup();
		});
	addWidget("nhMetamagicDecline", metamagicDeclineButton);
	metamagicDeclineButton->setEnabled(false);
	metamagicGrandButton = std::make_shared<CButton>(Point(640, 560), AnimationPath::builtin("NH_hero_actions_entry"),
		CButton::tooltip("Grand Metamagic", "Use this pending Metamagic sequence for two additional spells."), [this]()
		{
			owner.toggleMetamagicGrandFollowup();
		});
	addWidget("nhMetamagicGrand", metamagicGrandButton);
	metamagicGrandButton->setEnabled(false);
	metamagicGrandLabel = std::make_shared<CLabel>(0, 0, FONT_TINY, ETextAlignment::CENTER, Colors::YELLOW, "Grand OFF");
	metamagicGrandButton->setOverlay(metamagicGrandLabel);
	if(owner.getBattle()->battleUsesHeroCommands())
	{
		widget<CButton>("consoleUp")->moveBy(Point(-ordersControlPitch, 0));
		widget<CButton>("consoleDown")->moveBy(Point(-ordersControlPitch, 0));
		addShortcut(EShortcut::BATTLE_OPEN_ORDERS, [this] { bOrdersf(); });
		ordersButton = std::make_shared<CButton>(Point(595, 560), AnimationPath::builtin("NH_orders_gauntlet_framed"),
			CButton::tooltip("Orders", ""));
		ordersButton->addPopupCallback([this]
		{
			std::string reason;
			if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || !owner.actionsController)
				reason = "Battle context is no longer available.";
			else if(owner.curInt->isAutoFightOn)
				reason = "Autofight currently controls this battle.";
			else if(owner.isInTacticsMode())
				reason = "Orders are unavailable during tactics.";
			else if(!owner.currentHero())
				reason = "No commanding hero is available.";
			else if(owner.actionsController->heroSpellcastingModeActive())
				reason = "Finish or cancel spell targeting first.";
			else if(!owner.makingTurn())
				reason = "It is not your turn.";
			else if(ordersButton->isBlocked())
				reason = "Battle input is temporarily unavailable.";
			else
				reason = "Open Orders to inspect commands and their current availability.";
			CRClickPopup::createAndPush(reason + "\n\nOrders require no mana or spellbook. Opening or reading the panel spends nothing. Spells and Orders share one hero action per round.");
		});
		// Use the configurable interface's normal single-dispatch path: it defers
		// to an active assigned button. loadButtonHotkey attaches the callback once.
		JsonNode ordersHotkey;
		ordersHotkey.String() = "battleOpenOrders";
		loadButtonHotkey(ordersButton, ordersHotkey);
		addWidget("nhOrders", ordersButton);
		ordersButton->setHoverable(true);
		setShortcutBlocked(EShortcut::BATTLE_OPEN_ORDERS, true);
	}
	
	console = widget<BattleConsole>("console");

	owner.console = console;

	owner.fieldController.reset( new BattleFieldController(owner));
	owner.fieldController->createHeroes();

	createQueue();
	createQuickSpellWindow();
	createStickyHeroInfoWindows();
	createTimerInfoWindows();

	if ( owner.isInTacticsMode() )
		tacticPhaseStarted();
	else
		tacticPhaseEnded();

	addUsedEvents(LCLICK | KEYBOARD);
}

void BattleWindow::createQueue()
{
	OBJECT_CONSTRUCTION;

	//create stack queue and adjust our own position
	bool embedQueue;
	bool showQueue = settings["battle"]["showQueue"].Bool();
	std::string queueSize = settings["battle"]["queueSize"].String();

	if(queueSize == "auto")
		embedQueue = ENGINE->screenDimensions().y < 700;
	else
		embedQueue = ENGINE->screenDimensions().y < 700 || queueSize == "small";

	queue = std::make_shared<StackQueue>(embedQueue, owner);
	if(!embedQueue && showQueue)
	{
		//re-center, taking into account stack queue position
		pos.y -= queue->pos.h;
		pos.h += queue->pos.h;
	}
	if(showQueue)
		pos = center();

	if (!showQueue)
		queue->disable();
}

void BattleWindow::createStickyHeroInfoWindows()
{
	OBJECT_CONSTRUCTION;

	if(owner.defendingHeroInstance)
	{
		InfoAboutHero info;
		info.initFromHero(owner.defendingHeroInstance, InfoAboutHero::EInfoLevel::INBATTLE);
		defenderHeroWindow = std::make_shared<HeroInfoBasicPanel>(info, nullptr, true, true,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::DEFENDER));
	}
	if(owner.attackingHeroInstance)
	{
		InfoAboutHero info;
		info.initFromHero(owner.attackingHeroInstance, InfoAboutHero::EInfoLevel::INBATTLE);
		attackerHeroWindow = std::make_shared<HeroInfoBasicPanel>(info, nullptr, true, true,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::ATTACKER));
	}
	if(attackerHeroWindow)
		attackerCounterspellStatus = std::make_shared<CLabel>(39, 353, EFonts::FONT_TINY,
			ETextAlignment::CENTER,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::ATTACKER) ? Colors::YELLOW : Colors::WHITE,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::ATTACKER) ? "Ward: ARMED" : "Ward: none");
	if(defenderHeroWindow)
		defenderCounterspellStatus = std::make_shared<CLabel>(761, 353, EFonts::FONT_TINY,
			ETextAlignment::CENTER,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::DEFENDER) ? Colors::YELLOW : Colors::WHITE,
			owner.getBattle()->battleWasCounterspellArmed(BattleSide::DEFENDER) ? "Ward: ARMED" : "Ward: none");

	bool showInfoWindows = settings["battle"]["stickyHeroInfoWindows"].Bool();

	if(!showInfoWindows)
	{
		if(attackerHeroWindow)
			attackerHeroWindow->disable();

		if(defenderHeroWindow)
			defenderHeroWindow->disable();
		if(attackerCounterspellStatus)
			attackerCounterspellStatus->enable();
		if(defenderCounterspellStatus)
			defenderCounterspellStatus->enable();
	}
	else
	{
		if(attackerCounterspellStatus)
			attackerCounterspellStatus->disable();
		if(defenderCounterspellStatus)
			defenderCounterspellStatus->disable();
	}

	setPositionInfoWindow();
}

void BattleWindow::createQuickSpellWindow()
{
	OBJECT_CONSTRUCTION;

	quickSpellWindow = std::make_shared<QuickSpellPanel>(owner);
	quickSpellWindow->moveTo(Point(pos.x - 52, pos.y));

	unitActionWindow = std::make_shared<UnitActionPanel>(owner);
	unitActionWindow->moveTo(Point(pos.x + pos.w, pos.y));

	if(settings["battle"]["enableQuickSpellPanel"].Bool())
		showStickyQuickSpellWindow();
	else
		hideStickyQuickSpellWindow();
}

void BattleWindow::toggleStickyQuickSpellVisibility()
{
	if(settings["battle"]["enableQuickSpellPanel"].Bool())
		hideStickyQuickSpellWindow();
	else
		showStickyQuickSpellWindow();
}

void BattleWindow::hideStickyQuickSpellWindow()
{
	Settings showStickyQuickSpellWindow = settings.write["battle"]["enableQuickSpellPanel"];
	showStickyQuickSpellWindow->Bool() = false;

	quickSpellWindow->disable();
	unitActionWindow->disable();

	createTimerInfoWindows();
	setPositionInfoWindow();
	ENGINE->windows().totalRedraw();
}

void BattleWindow::showStickyQuickSpellWindow()
{
	Settings showStickyQuickSpellWindow = settings.write["battle"]["enableQuickSpellPanel"];
	showStickyQuickSpellWindow->Bool() = true;

	auto hero = owner.getBattle()->battleGetMyHero();

	bool quickSpellWindowVisible = hasSpaceForQuickActions() && hero != nullptr && hero->hasSpellbook();
	bool unitActionWindowVisible = hasSpaceForQuickActions();

	quickSpellWindow->setEnabled(quickSpellWindowVisible);
	unitActionWindow->setEnabled(unitActionWindowVisible);

	if(owner.actionsController && unitActionWindowVisible) // needed after resize of window
		owner.actionsController->activateStack();

	createTimerInfoWindows();
	setPositionInfoWindow();
	ENGINE->windows().totalRedraw();
}

void BattleWindow::createTimerInfoWindows()
{
	OBJECT_CONSTRUCTION;

	int xOffsetAttacker = quickSpellWindow->isDisabled() ? 0 : -51;
	int xOffsetDefender = unitActionWindow->isDisabled() ? 0 : 51;

	if(GAME->interface()->cb->getStartInfo()->turnTimerInfo.battleTimer != 0 || GAME->interface()->cb->getStartInfo()->turnTimerInfo.unitTimer != 0)
	{
		PlayerColor attacker = owner.getBattle()->sideToPlayer(BattleSide::ATTACKER);
		PlayerColor defender = owner.getBattle()->sideToPlayer(BattleSide::DEFENDER);

		if (attacker.isValidPlayer())
		{
			if (placeInfoWindowsOutside())
				attackerTimerWidget = std::make_shared<TurnTimerWidget>(Point(-76 + xOffsetAttacker, 0), attacker);
			else
				attackerTimerWidget = std::make_shared<TurnTimerWidget>(Point(1, 135), attacker);
		}

		if (defender.isValidPlayer())
		{
			if (placeInfoWindowsOutside())
				defenderTimerWidget = std::make_shared<TurnTimerWidget>(Point(pos.w + xOffsetDefender, 0), defender);
			else
				defenderTimerWidget = std::make_shared<TurnTimerWidget>(Point(pos.w - 78, 135), defender);
		}
	}
}

std::shared_ptr<BattleConsole> BattleWindow::buildBattleConsole(const JsonNode & config) const
{
	auto rect = readRect(config["rect"]);
	if(owner.getBattle()->battleUsesHeroCommands())
		rect.w -= ordersControlPitch;
	auto offset = readPosition(config["imagePosition"]);
	auto background = widget<CPicture>("menuBattle");
	return std::make_shared<BattleConsole>(owner, background, rect.topLeft(), offset, rect.dimensions() );
}

void BattleWindow::useSpellIfPossible(int slot)
{
	// Revalidate at activation, not only when the shortcut/button was last blocked.
	if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || owner.curInt->isAutoFightOn
		|| owner.isInTacticsMode() || !owner.makingTurn() || !quickSpellWindow)
		return;

	const auto quickSpells = quickSpellWindow->getSpells();
	if(slot < 0 || static_cast<size_t>(slot) >= quickSpells.size())
		return;
	const auto id = std::get<0>(quickSpells[slot]);
	const auto * hero = owner.currentHero();

	if(id.hasValue() && hero && id.toSpell()->canBeCast(owner.getBattle().get(), spells::Mode::HERO, hero))
	{
		owner.castThisSpell(id);
	}
};

void BattleWindow::toggleQueueVisibility()
{
	if(settings["battle"]["showQueue"].Bool())
		hideQueue();
	else
		showQueue();
}

void BattleWindow::toggleBattleSetting(const std::string & name)
{
	Settings setting = settings.write["battle"][name];
	setting->Bool() = !setting->Bool();
	owner.redrawBattlefield();
}

void BattleWindow::toggleStackInfoWindowsVisibility()
{
	Settings setting = settings.write["battle"]["stackInfoBasicPanel"];
	setting->Bool() = !setting->Bool();
	bool show = setting->Bool();

	if(attackerStackWindow)
		attackerStackWindow->setEnabled(show);
	if(defenderStackWindow)
		defenderStackWindow->setEnabled(show);

	ENGINE->windows().totalRedraw();
}

void BattleWindow::hideQueue()
{
	if(settings["battle"]["showQueue"].Bool() == false)
		return;

	Settings showQueue = settings.write["battle"]["showQueue"];
	showQueue->Bool() = false;

	queue->disable();

	if (!queue->embedded)
	{
		//re-center, taking into account stack queue position
		pos.y += queue->pos.h;
		pos.h -= queue->pos.h;
	}
	pos = center();
	setPositionInfoWindow();
	ENGINE->windows().totalRedraw();
}

void BattleWindow::showQueue()
{
	if(settings["battle"]["showQueue"].Bool() == true)
		return;

	Settings showQueue = settings.write["battle"]["showQueue"];
	showQueue->Bool() = true;

	createQueue();
	updateQueue();
	setPositionInfoWindow();
	ENGINE->windows().totalRedraw();
}

void BattleWindow::toggleStickyHeroWindowsVisibility()
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool())
		hideStickyHeroWindows();
	else
		showStickyHeroWindows();
}

void BattleWindow::hideStickyHeroWindows()
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool() == false)
		return;

	Settings showStickyHeroInfoWindows = settings.write["battle"]["stickyHeroInfoWindows"];
	showStickyHeroInfoWindows->Bool() = false;

	if(attackerHeroWindow)
		attackerHeroWindow->disable();

	if(defenderHeroWindow)
		defenderHeroWindow->disable();

	if(attackerCounterspellStatus)
		attackerCounterspellStatus->enable();

	if(defenderCounterspellStatus)
		defenderCounterspellStatus->enable();

	ENGINE->windows().totalRedraw();
}

void BattleWindow::showStickyHeroWindows()
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool() == true)
		return;

	Settings showStickyHeroInfoWindows = settings.write["battle"]["stickyHeroInfoWindows"];
	showStickyHeroInfoWindows->Bool() = true;


	createStickyHeroInfoWindows();
	ENGINE->windows().totalRedraw();
}

void BattleWindow::updateQueue()
{
	queue->update();
	createQuickSpellWindow();
}

void BattleWindow::setPositionInfoWindow()
{
	int xOffsetAttacker = quickSpellWindow->isDisabled() ? 0 : -51;
	int xOffsetDefender = unitActionWindow->isDisabled() ? 0 : 51;

	int yOffsetAttacker = attackerTimerWidget ? attackerTimerWidget->pos.h + 9 : 0;
	int yOffsetDefender = defenderTimerWidget ? defenderTimerWidget->pos.h + 9 : 0;

	if(defenderHeroWindow)
	{
		Point position = placeInfoWindowsOutside()
				? Point(pos.x + pos.w - 1 + xOffsetDefender, pos.y - 1 + yOffsetDefender)
				: Point(pos.x + pos.w -79, pos.y + 195);
		defenderHeroWindow->moveTo(position);
		defenderHeroWindow->setAboveBattlefield(!placeInfoWindowsOutside());
	}
	if(attackerHeroWindow)
	{
		Point position = placeInfoWindowsOutside()
				? Point(pos.x - 77 + xOffsetAttacker, pos.y - 1 + yOffsetAttacker)
				: Point(pos.x + 1, pos.y + 195);
		attackerHeroWindow->moveTo(position);
		attackerHeroWindow->setAboveBattlefield(!placeInfoWindowsOutside());
	}
	if(defenderStackWindow)
	{
		Point position = placeInfoWindowsOutside()
				? Point(pos.x + pos.w - 1 + xOffsetDefender, defenderHeroWindow ? defenderHeroWindow->pos.y + 210 : pos.y - 1 + yOffsetDefender)
				: Point(pos.x + pos.w -79, defenderHeroWindow ? defenderHeroWindow->pos.y : pos.y + 195);
		defenderStackWindow->moveTo(position);
		defenderStackWindow->setAboveBattlefield(!placeInfoWindowsOutside());
	}
	if(attackerStackWindow)
	{
		Point position = placeInfoWindowsOutside()
				? Point(pos.x - 77 + xOffsetAttacker, attackerHeroWindow ? attackerHeroWindow->pos.y + 210 : pos.y - 1 + yOffsetAttacker)
				: Point(pos.x + 1, attackerHeroWindow ? attackerHeroWindow->pos.y : pos.y + 195);
		attackerStackWindow->moveTo(position);
		attackerStackWindow->setAboveBattlefield(!placeInfoWindowsOutside());
	}
}

void BattleWindow::updateHeroInfoWindow(uint8_t side, const InfoAboutHero & hero)
{
	std::shared_ptr<HeroInfoBasicPanel> panelToUpdate = side == 0 ? attackerHeroWindow : defenderHeroWindow;
	if(panelToUpdate)
		panelToUpdate->update(hero, owner.getBattle()->battleWasCounterspellArmed(
			side == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER));
}

void BattleWindow::updateCounterspellStatus()
{
	const bool attackerArmed = owner.getBattle()->battleWasCounterspellArmed(BattleSide::ATTACKER);
	const bool defenderArmed = owner.getBattle()->battleWasCounterspellArmed(BattleSide::DEFENDER);
	if(attackerHeroWindow)
		attackerHeroWindow->setCounterspellStatus(attackerArmed);
	if(defenderHeroWindow)
		defenderHeroWindow->setCounterspellStatus(defenderArmed);
	if(attackerCounterspellStatus)
	{
		attackerCounterspellStatus->setText(attackerArmed ? "Ward: ARMED" : "Ward: none");
		attackerCounterspellStatus->setColor(attackerArmed ? Colors::YELLOW : Colors::WHITE);
	}
	if(defenderCounterspellStatus)
	{
		defenderCounterspellStatus->setText(defenderArmed ? "Ward: ARMED" : "Ward: none");
		defenderCounterspellStatus->setColor(defenderArmed ? Colors::YELLOW : Colors::WHITE);
	}
	if(metamagicDeclineButton)
	{
		const auto side = owner.getBattle()->battleGetMySide();
		const bool pending = side != BattleSide::NONE && owner.getBattle()->battleCanUseMetamagicFollowup(side);
		metamagicDeclineButton->setEnabled(pending);
		metamagicDeclineButton->block(!pending);
	}
	if(metamagicGrandButton)
	{
		const auto side = owner.getBattle()->battleGetMySide();
		const auto * hero = owner.currentHero();
		const bool available = side != BattleSide::NONE && hero
			&& owner.getBattle()->battleMetamagicPendingCount(side) == 1
			&& owner.getBattle()->battleMetamagicSequenceSpells(side).size() == 1
			&& !owner.getBattle()->battleMetamagicGrandUsed(side)
			&& newHorizonsMagic::metamagicRank(hero) >= 3
			&& newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND);
		const bool selected = available && owner.actionsController
			&& owner.actionsController->metamagicGrandModeActive();
		metamagicGrandButton->setEnabled(available);
		metamagicGrandButton->block(!available);
		if(metamagicGrandLabel)
		{
			metamagicGrandLabel->setText(selected ? "Grand ON" : "Grand OFF");
			metamagicGrandLabel->setColor(selected ? Colors::GREEN : Colors::YELLOW);
		}
	}
}

void BattleWindow::updateStackInfoWindow(const CStack * stack)
{
	OBJECT_CONSTRUCTION;

	bool showInfoWindows = settings["battle"]["stackInfoBasicPanel"].Bool();

	if(stack && stack->unitSide() == BattleSide::DEFENDER)
	{
		defenderStackWindow = std::make_shared<StackInfoBasicPanel>(stack, true);
		defenderStackWindow->setEnabled(showInfoWindows);
	}
	else
		defenderStackWindow = nullptr;
	
	if(stack && stack->unitSide() == BattleSide::ATTACKER)
	{
		attackerStackWindow = std::make_shared<StackInfoBasicPanel>(stack, true);
		attackerStackWindow->setEnabled(showInfoWindows);
	}
	else
		attackerStackWindow = nullptr;
	
	createTimerInfoWindows();
	setPositionInfoWindow();
	redraw();
}

void BattleWindow::heroManaPointsChanged(const CGHeroInstance * hero)
{
	if(hero == owner.attackingHeroInstance || hero == owner.defendingHeroInstance)
	{
		InfoAboutHero heroInfo = InfoAboutHero();
		heroInfo.initFromHero(hero, InfoAboutHero::INBATTLE);

		updateHeroInfoWindow(hero == owner.attackingHeroInstance ? 0 : 1, heroInfo);
	}
	else
	{
		logGlobal->error("BattleWindow::heroManaPointsChanged: 'Mana points changed' called for hero not belonging to current battle window");
	}
}

void BattleWindow::activate()
{
	ENGINE->setStatusbar(console);
	CIntObject::activate();
	GAME->interface()->cingconsole->activate();
}

void BattleWindow::deactivate()
{
	ENGINE->setStatusbar(nullptr);
	CIntObject::deactivate();
	GAME->interface()->cingconsole->deactivate();
}

bool BattleWindow::captureThisKey(EShortcut key)
{
	return owner.openingPlaying();
}

void BattleWindow::keyPressed(EShortcut key)
{
	if (owner.openingPlaying())
	{
		owner.openingEnd();
		return;
	}
	InterfaceObjectConfigurable::keyPressed(key);
}

void BattleWindow::clickPressed(const Point & cursorPosition)
{
	if (owner.openingPlaying())
	{
		owner.openingEnd();
		return;
	}
	InterfaceObjectConfigurable::clickPressed(cursorPosition);
}

void BattleWindow::tacticPhaseStarted()
{
	auto menuBattle = widget<CIntObject>("menuBattle");
	auto console = widget<CIntObject>("console");
	auto menuTactics = widget<CIntObject>("menuTactics");
	auto tacticNext = widget<CIntObject>("tacticNext");
	auto tacticEnd = widget<CIntObject>("tacticEnd");

	menuBattle->disable();
	console->disable();
	if(ordersButton)
	{
		ordersButton->block(true);
		ordersButton->disable();
		setShortcutBlocked(EShortcut::BATTLE_OPEN_ORDERS, true);
	}

	menuTactics->enable();
	tacticNext->enable();
	tacticEnd->enable();

	redraw();
}

void BattleWindow::tacticPhaseEnded()
{
	auto menuBattle = widget<CIntObject>("menuBattle");
	auto console = widget<CIntObject>("console");
	auto menuTactics = widget<CIntObject>("menuTactics");
	auto tacticNext = widget<CIntObject>("tacticNext");
	auto tacticEnd = widget<CIntObject>("tacticEnd");

	menuBattle->enable();
	console->enable();
	if(ordersButton)
	{
		ordersButton->block(true);
		ordersButton->enable();
		setShortcutBlocked(EShortcut::BATTLE_OPEN_ORDERS, true);
	}

	menuTactics->disable();
	tacticNext->disable();
	tacticEnd->disable();

	redraw();
}

void BattleWindow::bOptionsf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	ENGINE->cursor().set(Cursor::Map::POINTER);

	ENGINE->windows().createAndPushWindow<SettingsMainWindow>(&owner);
}

void BattleWindow::bSurrenderf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if(ownHeroLossEndsScenario())
	{
		owner.curInt->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.battle.escapeImpossibleCritical"));
		return;
	}

	int cost = owner.getBattle()->battleGetSurrenderCost();
	if(cost >= 0)
	{
		MetaString surrenderMessage = MetaString::createFromTextID("core.genrltxt.32"); //%s states: "I will accept your surrender and grant you and your troops safe passage for the price of %d gold."
		const InfoAboutHero enemyHero = owner.getBattle()->battleGetEnemyHero();
		if(enemyHero.name.empty())
		{
			// army without a hero, e.g. neutral monsters - strongest of their remaining units speaks on their behalf
			auto enemyStacks = owner.getBattle()->battleGetStacks(CPlayerBattleCallback::ONLY_ENEMY);
			auto strongest = vstd::maxElementByFun(enemyStacks, [](const CStack * stack){ return stack->unitType()->getAIValue(); });

			if(strongest != enemyStacks.end())
				surrenderMessage.replaceNameSingular((*strongest)->unitType()->getId());
			else
				surrenderMessage.replaceRawString("");
		}
		else
			surrenderMessage.replaceRawString(enemyHero.name.toString(&GAME->translator()));

		surrenderMessage.replaceNumber(cost);
		owner.curInt->showYesNoDialog(surrenderMessage.toString(&GAME->translator()), [this](){ reallySurrender(); }, nullptr);
	}
}

bool BattleWindow::ownHeroLossEndsScenario() const
{
	const CGHeroInstance * ownHero = nullptr;
	if(owner.attackingHeroInstance && owner.attackingHeroInstance->tempOwner == owner.curInt->cb->getPlayerID())
		ownHero = owner.attackingHeroInstance;
	if(owner.defendingHeroInstance && owner.defendingHeroInstance->tempOwner == owner.curInt->cb->getPlayerID())
		ownHero = owner.defendingHeroInstance;

	if(ownHero && ownHero->isMissionCritical())
		return true;

	return owner.curInt->cb->howManyTowns() == 0 && owner.curInt->cb->howManyHeroes() == 1;
}

void BattleWindow::bFleef()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if(ownHeroLossEndsScenario())
	{
		owner.curInt->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.battle.escapeImpossibleCritical"));
		return;
	}

	if ( owner.getBattle()->battleCanFlee() )
	{
		auto ony = std::bind(&BattleWindow::reallyFlee,this);
		owner.curInt->showYesNoDialog(LIBRARY->generaltexth->allTexts[28], ony, nullptr); //Are you sure you want to retreat?
	}
	else
	{
		std::vector<std::shared_ptr<CComponent>> comps;
		std::string heroNameTextID;
		//calculating fleeing hero's name
		if (owner.attackingHeroInstance)
			if (owner.attackingHeroInstance->tempOwner == owner.curInt->cb->getPlayerID())
				heroNameTextID = owner.attackingHeroInstance->getNameTextID();
		if (owner.defendingHeroInstance)
			if (owner.defendingHeroInstance->tempOwner == owner.curInt->cb->getPlayerID())
				heroNameTextID = owner.defendingHeroInstance->getNameTextID();
		//calculating text
		MetaString txt = MetaString::createFromTextID("core.genrltxt.340"); //The Shackles of War are present.  %s can not retreat!
		txt.replaceTextID(heroNameTextID);

		//printing message
		owner.curInt->showInfoDialog(txt.toString(&GAME->translator()), comps);
	}
}

void BattleWindow::reallyFlee()
{
	owner.giveCommand(EActionType::RETREAT);
	ENGINE->cursor().set(Cursor::Map::POINTER);
}

const CGTownInstance * BattleWindow::findTownWithMarketplace() const
{
	for(const CGTownInstance * town : owner.curInt->cb->getTownsInfo())
	{
		if(town->hasBuilt(BuildingID::MARKETPLACE))
			return town;
	}

	return nullptr;
}

bool BattleWindow::canOfferMarketplaceForSurrender() const
{
	// The feature can be granted either to the hero (e.g. artifact) or to the player
	// (e.g. global/player-wide config bonus). Accept either source.
	const CGHeroInstance * hero = owner.getBattle()->battleGetMyHero();
	return hero && hero->hasBonusOfType(BonusType::SURRENDER_MARKETPLACE_ACCESS);
}

void BattleWindow::offerMarketplaceForSurrender()
{
	const CGTownInstance * townWithMarket = findTownWithMarketplace();
	if(!townWithMarket)
	{
		owner.curInt->showInfoDialog(LIBRARY->generaltexth->allTexts[29]); //You don't have enough gold!
		return;
	}

	const CGHeroInstance * hero = owner.getBattle()->battleGetMyHero();
	const int goldBeforeMarketplace = owner.curInt->cb->getResourceAmount(EGameResID::GOLD);

	owner.curInt->showYesNoDialog(
		LIBRARY->generaltexth->translate("vcmi.battle.surrender.tryMarketplace"),
		[this, townWithMarket, hero, goldBeforeMarketplace]()
		{
			ENGINE->windows().createAndPushWindow<CMarketWindow>(
				townWithMarket,
				hero,
				[this, goldBeforeMarketplace]()
				{
					const bool soldResourcesForGold = owner.curInt->cb->getResourceAmount(EGameResID::GOLD) > goldBeforeMarketplace;
					reallySurrender(false, soldResourcesForGold);
				},
				EMarketMode::RESOURCE_RESOURCE,
				true,
				owner.curInt.get());
		},
		nullptr);
}

void BattleWindow::reallySurrender(bool allowMarketplaceOffer, bool marketplaceSaleFailed)
{
	if (owner.curInt->cb->getResourceAmount(EGameResID::GOLD) < owner.getBattle()->battleGetSurrenderCost())
	{
		if(allowMarketplaceOffer && canOfferMarketplaceForSurrender())
			offerMarketplaceForSurrender();
		else if(marketplaceSaleFailed)
			owner.curInt->showInfoDialog(LIBRARY->generaltexth->translate("vcmi.battle.surrender.marketplaceFailed"));
		else
			owner.curInt->showInfoDialog(LIBRARY->generaltexth->allTexts[29]); //You don't have enough gold!
	}
	else
	{
		owner.giveCommand(EActionType::SURRENDER);
		ENGINE->cursor().set(Cursor::Map::POINTER);
	}
}

void BattleWindow::setPossibleActions(const std::vector<PossiblePlayerBattleAction> & actions)
{
	unitActionWindow->setPossibleActions(actions);
}

void BattleWindow::bAutofightf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if(settings["battle"]["endWithAutocombat"].Bool() && onlyOnePlayerHuman)
	{
		endWithAutocombat();
		return;
	}

	//Stop auto-fight mode
	if(owner.curInt->isAutoFightOn)
	{
		assert(owner.curInt->autofightingAI);
		owner.curInt->isAutoFightOn = false;
		logGlobal->trace("Stopping the autofight...");
	}
	else if(!owner.curInt->autofightingAI)
	{
		owner.curInt->isAutoFightOn = true;
		blockUI(true);

		owner.curInt->prepareAutoFightingAI(owner.getBattleID(), owner.army1, owner.army2, int3(0,0,0), owner.attackingHeroInstance, owner.defendingHeroInstance, owner.getBattle()->battleGetMySide());
		owner.requestAutofightingAIToTakeAction();
	}
}

void BattleWindow::bSpellf()
{
	if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || owner.curInt->isAutoFightOn || owner.isInTacticsMode())
		return;
	openSpellbook();
}

void BattleWindow::bOrdersf()
{
	if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || owner.curInt->isAutoFightOn)
		return;
	if(!owner.getBattle()->battleUsesHeroCommands() || owner.actionsController->heroSpellcastingModeActive()
		|| !owner.makingTurn() || owner.isInTacticsMode() || !owner.currentHero())
		return;
	owner.actionsController->cancelHeroOrderTargeting();
	ENGINE->windows().createAndPushWindow<BattleHeroActionWindow>(CPlayerInterface::battleInt, true);
}

void BattleWindow::openSpellbook()
{
	if(owner.actionsController->heroOrderTargetingModeActive())
		owner.actionsController->cancelHeroOrderTargeting();
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if (!owner.makingTurn())
		return;

	auto myHero = owner.currentHero();
	if(!myHero)
		return;
	if(owner.getBattle()->battleCanUseMetamagicFollowup(owner.getBattle()->battleGetMySide()))
		owner.actionsController->beginMetamagicFollowup();

	ENGINE->cursor().set(Cursor::Map::POINTER);

	ESpellCastProblem spellCastProblem = owner.getBattle()->battleCanCastSpell(myHero, spells::Mode::HERO);

	if(spellCastProblem == ESpellCastProblem::OK || spellCastProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT)
	{
		// The spellbook remains useful as a read-only reference after the hero has
		// spent this round's cast. SpellArea revalidates the selected spell before
		// entering target selection, so opening it never bypasses the cast limit.
		ENGINE->windows().createAndPushWindow<CSpellWindow>(myHero, owner.curInt.get());
	}
	else if (spellCastProblem == ESpellCastProblem::MAGIC_IS_BLOCKED)
	{
		//TODO: move to spell mechanics, add more information to spell cast problem
		//Handle Orb of Inhibition-like effects -> we want to display dialog with info, why casting is impossible
		auto blockingBonus = owner.currentHero()->getFirstBonus(Selector::type()(BonusType::BLOCK_ALL_MAGIC));
		if (!blockingBonus)
			return;

		if (blockingBonus->source == BonusSource::ARTIFACT)
		{
			const auto artID = blockingBonus->sid.as<ArtifactID>();

			//%s wields the %s, an ancient artifact which creates a pocket dead to all magic.
			MetaString message = MetaString::createFromTextID("core.genrltxt.683");
			//If we have artifact, put name of our hero. Otherwise assume it's the enemy.
			//TODO check who *really* is source of bonus
			if(myHero->hasArt(artID, true))
				message.replaceTextID(myHero->getNameTextID());
			else
				message.replaceRawString(owner.enemyHero().name.toString(&GAME->translator()));
			message.replaceName(artID);

			GAME->interface()->showInfoDialog(message.toString(&GAME->translator()));
		}
		else if(blockingBonus->source == BonusSource::OBJECT_TYPE)
		{
			if(blockingBonus->sid.as<MapObjectID>() == Obj::GARRISON || blockingBonus->sid.as<MapObjectID>() == Obj::GARRISON2)
				GAME->interface()->showInfoDialog(LIBRARY->generaltexth->allTexts[684]);
		}
	}
	else
	{
		logGlobal->warn("Unexpected problem with readiness to cast spell");
	}
}

void BattleWindow::openMetamagicSpellbook()
{
	if(!owner.actionsController)
		return;
	owner.actionsController->beginMetamagicFollowup();
	if(owner.actionsController->metamagicFollowupModeActive())
	{
		const auto side = owner.getBattle()->battleGetMySide();
		const int remaining = std::max(0,
			newHorizonsMagic::metamagicRank(owner.currentHero())
			- owner.getBattle()->battleMetamagicUsesConsumed(side));
		const bool grandAvailable = owner.getBattle()->battleMetamagicPendingCount(side) == 1
			&& owner.getBattle()->battleMetamagicSequenceSpells(side).size() == 1
			&& !owner.getBattle()->battleMetamagicGrandUsed(side)
			&& newHorizonsMagic::metamagicRank(owner.currentHero()) >= 3
			&& newHorizonsMagic::hasMetamagicPerk(owner.currentHero(), newHorizonsMagic::METAMAGIC_GRAND);
		owner.appendBattleLog("Metamagic: choose an immediate additional spell (remaining uses this combat: "
			+ std::to_string(remaining) + ")."
			+ (grandAvailable ? " Grand Metamagic is optional; use its button for two additional spells." : "")
			+ " Decline / End is available beside Wait.");
		openSpellbook();
	}
}

void BattleWindow::bWaitf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if (owner.stacksController->getActiveStack() != nullptr)
		owner.giveCommand(EActionType::WAIT);
}

void BattleWindow::bDefencef()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	if (owner.stacksController->getActiveStack() != nullptr)
		owner.giveCommand(EActionType::DEFEND);
}

void BattleWindow::bConsoleUpf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	console->scrollUp();
}

void BattleWindow::bConsoleDownf()
{
	if (owner.actionsController->heroSpellcastingModeActive())
		return;

	console->scrollDown();
}

void BattleWindow::bTacticNextStack()
{
	owner.tacticNextStack(nullptr);
}

void BattleWindow::bTacticPhaseEnd()
{
	owner.tacticPhaseEnd();
}

void BattleWindow::blockUI(bool on)
{
	bool canCastSpells = false;
	auto hero = owner.getBattle()->battleGetMyHero();

	if(hero)
	{
		ESpellCastProblem spellcastingProblem = owner.getBattle()->battleCanCastSpell(hero, spells::Mode::HERO);

		//if magic is blocked, we leave button active, so the message can be displayed after button click
		canCastSpells = spellcastingProblem == ESpellCastProblem::OK
			|| spellcastingProblem == ESpellCastProblem::MAGIC_IS_BLOCKED
			|| spellcastingProblem == ESpellCastProblem::CASTS_PER_TURN_LIMIT;
	}

	// Orders remain independently readable without a book/mana and after spending
	// the shared action. Each actual command still revalidates before submission.
	if(ordersButton)
	{
		const bool ordersBlocked = on || !owner.curInt || owner.curInt->isAutoFightOn
			|| owner.isInTacticsMode() || !owner.currentHero() || owner.actionsController->heroSpellcastingModeActive();
		ordersButton->block(ordersBlocked);
		// Disabled issuance must not hide read-only help. Do not restore click/key events.
		ordersButton->addUsedEvents(SHOW_POPUP);
		setShortcutBlocked(EShortcut::BATTLE_OPEN_ORDERS, ordersBlocked);
	}

	bool canWait = owner.stacksController->getActiveStack() ? !owner.stacksController->getActiveStack()->waitedThisTurn : false;
	bool tacticsMode = owner.isInTacticsMode();

	setShortcutBlocked(EShortcut::GLOBAL_OPTIONS, on);
	setShortcutBlocked(EShortcut::BATTLE_OPEN_ACTIVE_UNIT, on);
	setShortcutBlocked(EShortcut::BATTLE_OPEN_HOVERED_UNIT, on);
	setShortcutBlocked(EShortcut::BATTLE_RETREAT, on || !owner.getBattle()->battleCanFlee());
	setShortcutBlocked(EShortcut::BATTLE_SURRENDER, on || owner.getBattle()->battleGetSurrenderCost() < 0);
	setShortcutBlocked(EShortcut::BATTLE_CAST_SPELL, on || tacticsMode || !canCastSpells);
	setShortcutBlocked(EShortcut::BATTLE_WAIT, on || tacticsMode || !canWait);
	setShortcutBlocked(EShortcut::BATTLE_DEFEND, on || tacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_AUTOCOMBAT, (settings["battle"]["endWithAutocombat"].Bool() && onlyOnePlayerHuman) ? on || tacticsMode || owner.actionsController->heroSpellcastingModeActive() : owner.actionsController->heroSpellcastingModeActive());
	setShortcutBlocked(EShortcut::BATTLE_END_WITH_AUTOCOMBAT, on || !onlyOnePlayerHuman || owner.actionsController->heroSpellcastingModeActive());
	setShortcutBlocked(EShortcut::BATTLE_TACTICS_END, on || !tacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_TACTICS_NEXT, on || !tacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_CONSOLE_DOWN, on && !tacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_CONSOLE_UP, on && !tacticsMode);
	updateLandMinePlacementControls();

	quickSpellWindow->setInputEnabled(!on);
	unitActionWindow->setInputEnabled(!on);
}

void BattleWindow::updateLandMinePlacementControls()
{
	const bool active = owner.actionsController && owner.actionsController->landMinePlacementModeActive();
	const bool ready = active && owner.actionsController->landMinePlacementReady();
	const bool canUndo = active && !owner.actionsController->landMinePlacementSelectedHexes().empty();
	setShortcutBlocked(EShortcut::GLOBAL_ACCEPT, !ready);
	setShortcutBlocked(EShortcut::GLOBAL_BACKSPACE, !canUndo);
	if(landMineConfirmButton)
	{
		widget<CButton>("wait")->setEnabled(!active);
		landMineConfirmButton->setEnabled(active);
		landMineConfirmButton->block(!ready);
	}
}

void BattleWindow::bOpenActiveUnit()
{
	const auto * unit = owner.stacksController->getActiveStack();

	if (unit)
		ENGINE->windows().createAndPushWindow<CStackWindow>(unit, false);
}

void BattleWindow::bOpenHoveredUnit()
{
	const auto units = owner.stacksController->getHoveredStacksUnitIds();

	if (!units.empty())
	{
		const auto * unit = owner.getBattle()->battleGetStackByID(units[0]);
		if (unit)
			ENGINE->windows().createAndPushWindow<CStackWindow>(unit, false);
	}
}

std::optional<uint32_t> BattleWindow::getQueueHoveredUnitId()
{
	return queue->getHoveredUnitIdIfAny();
}

void BattleWindow::endWithAutocombat() 
{
	if(!owner.makingTurn())
		return;

	GAME->interface()->showYesNoDialog(
		LIBRARY->generaltexth->translate("vcmi.battleWindow.endWithAutocombat"),
		[this]()
		{
			owner.curInt->isAutoFightEndBattle = true;
			owner.curInt->prepareAutoFightingAI(owner.getBattleID(), owner.army1, owner.army2, int3(0,0,0), owner.attackingHeroInstance, owner.defendingHeroInstance, owner.getBattle()->battleGetMySide());

			owner.requestAutofightingAIToTakeAction();

			close();

			owner.curInt->battleInt.reset();
		},
		nullptr
	);
}

void BattleWindow::showAll(Canvas & to)
{
	if(owner.curInt->cb->getMapHeader()->battleOnly)
		to.fillTexture(ENGINE->renderHandler().loadImage(ImagePath::builtin("DiBoxBck"), EImageBlitMode::OPAQUE));
	CIntObject::showAll(to);

	if (ENGINE->screenDimensions().x != 800 || ENGINE->screenDimensions().y !=600)
		to.drawBorder(Rect(pos.x-1, pos.y - (queue && queue->embedded ? 1 : 0), pos.w+2, pos.h+1 + (queue && queue->embedded ? 1 : 0)), Colors::BRIGHT_YELLOW);
}

void BattleWindow::show(Canvas & to)
{
	CIntObject::show(to);
	GAME->interface()->cingconsole->show(to);
}

void BattleWindow::onScreenResize()
{
	if(settings["battle"]["showQueue"].Bool())
	{
		hideQueue();
		showQueue();
	}
	if(settings["battle"]["enableQuickSpellPanel"].Bool())
	{
		hideStickyQuickSpellWindow();
		showStickyQuickSpellWindow();
	}
	if(settings["battle"]["stickyHeroInfoWindows"].Bool())
	{
		hideStickyHeroWindows();
		showStickyHeroWindows();
	}
}

void BattleWindow::close()
{
	if(!ENGINE->windows().isTopWindow(this))
		logGlobal->error("Only top interface must be closed");
	ENGINE->windows().popWindows(1);
}

bool BattleWindow::hasSpaceForQuickActions() const
{
	constexpr int widthWithQuickActions = 800 + 50*2;

	return ENGINE->screenDimensions().x >= widthWithQuickActions;
}

bool BattleWindow::placeInfoWindowsOutside() const
{
	constexpr int widthWithQuickActions = 800 + 50*2 + 75*2;
	constexpr int widthBaseWindow = 800 + 75*2;

	if (quickActionsPanelActive())
		return ENGINE->screenDimensions().x >= widthWithQuickActions;
	else
		return ENGINE->screenDimensions().x >= widthBaseWindow;
}

bool BattleWindow::quickActionsPanelActive() const
{
	return unitActionWindow->isActive();
}
