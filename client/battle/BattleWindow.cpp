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
#include "render/IFont.h"
#include "render/IRenderHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "../windows/CCreatureWindow.h"
#include "../windows/CMarketWindow.h"
#include "../windows/CMessage.h"
#include "../windows/CSpellWindow.h"
#include "../windows/InfoWindows.h"
#include "../windows/settings/SettingsMainWindow.h"

#include "../../lib/CConfigHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsWarcasting.h"
#include "../../lib/battle/NewHorizonsBloodrage.h"
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
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace
{
constexpr int ordersControlPitch = 51;
// StackInfoBasicPanel's tallest child is the CCRPOP background at y=37 with
// height 286. Keep the outside hero/status/stack column inside short screens.
constexpr int outsideStackInfoPanelExtent = 37 + 286;

bool hasActiveWarcasting(const AlternatingHeroActionState & state, int round)
{
	return state.bonusFor(state.nextEligibleAction, round) > 0;
}

std::string warcastingIconName(AlternatingHeroActionState::Action action)
{
	if(action == AlternatingHeroActionState::Action::SPELL)
		return "NH_perk_arcane_channeling_normal.png";
	if(action == AlternatingHeroActionState::Action::ORDER)
		return "NH_perk_martial_channeling_normal.png";
	return {};
}

}

namespace
{
class RealityWarpPreviewWindow final : public CWindowObject
{
	std::vector<std::shared_ptr<CIntObject>> widgets;
public:
	RealityWarpPreviewWindow(const std::string & text, std::function<void()> confirm,
		std::function<void()> cancel) : CWindowObject(BORDERED)
	{
		pos = Rect(0, 0, 640, 420);
		center();
		OBJECT_CONSTRUCTION;
		widgets.push_back(std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, 640, 420)));
		widgets.push_back(std::make_shared<CLabel>(320, 12, FONT_SMALL,
			ETextAlignment::TOPCENTER, Colors::YELLOW, "Reality Warp — complete exchange"));
		widgets.push_back(std::make_shared<CTextBox>(text, Rect(16, 38, 608, 320), 0,
			FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE));
		auto yes = std::make_shared<CButton>(Point(224, 376), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Confirm", "Revalidate the complete exchange and cast."), [this, confirm]()
		{
			close();
			confirm();
		});
		yes->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);
		yes->assignedKey = EShortcut::GLOBAL_ACCEPT;
		widgets.push_back(yes);
		auto no = std::make_shared<CButton>(Point(336, 376), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip("Cancel", "Cancel Reality Warp without casting."), [this, cancel]()
		{
			close();
			cancel();
		});
		no->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
		no->assignedKey = EShortcut::GLOBAL_CANCEL;
		widgets.push_back(no);
	}
};
}

void BattleWindow::showRealityWarpPreview(const std::string & text, std::function<void()> confirm,
	std::function<void()> cancel)
{
	ENGINE->windows().pushWindow(std::make_shared<RealityWarpPreviewWindow>(text, std::move(confirm), std::move(cancel)));
}

class BattleTargetSelectionPanel final : public CIntObject
{
	BattleInterface & owner;
	bool active = false;
	std::vector<std::shared_ptr<CIntObject>> decoration;
	std::shared_ptr<CLabel> heading;
	std::shared_ptr<CMultiLineLabel> targetReadback;
	std::shared_ptr<CButton> undoButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CButton> confirmButton;

	void refresh()
	{
		const auto * controller = owner.actionsController.get();
		const bool repeatedPlacement = controller && controller->repeatedPlacementModeActive();
		const bool stormOfDaggers = controller && controller->stormOfDaggersTargetSelectionModeActive();
		const bool soulChain = controller && controller->soulChainTargetSelectionModeActive();
		const bool shouldShow = repeatedPlacement || stormOfDaggers || soulChain;
		active = shouldShow;
		if(!controller || !shouldShow)
		{
			undoButton->setEnabled(false);
			cancelButton->setEnabled(false);
			confirmButton->setEnabled(false);
			return;
		}

		if(repeatedPlacement)
		{
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
			return;
		}

		if(soulChain)
		{
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
			return;
		}

		const auto preview = controller->getStormOfDaggersSelectionPreview();
		std::string title = "Storm of Daggers  |  " + std::to_string(preview.selectedTargetCount)
			+ "/" + std::to_string(preview.maximumTargetCount) + " targets";
		if(preview.poolAvailable)
			title += "  |  Pool " + std::to_string(preview.totalDamagePool)
				+ " (" + std::to_string(preview.rawDamagePerTarget) + " each before resistance)";
		if(heading->getText() != title)
			heading->setText(title);

		std::string readback;
		for(size_t index = 0; index < preview.targets.size(); ++index)
		{
			const auto & target = preview.targets[index];
			if(!readback.empty())
				readback += ";  ";
			readback += "#" + std::to_string(index + 1) + " " + target.name + ": ";
			if(target.projectedDamage)
				readback += "about " + std::to_string(*target.projectedDamage) + " damage";
			else if(preview.poolAvailable)
				readback += std::to_string(preview.rawDamagePerTarget) + " raw damage; estimate unavailable";
			else
				readback += "damage estimate unavailable";
		}

		if(readback.empty())
			readback = preview.status;
		else if(!preview.canConfirm)
			readback += "; " + preview.status;
		else if(preview.selectedTargetCount >= preview.maximumTargetCount)
			readback += ". Target limit reached; Confirm, or use Undo to revise.";
		else
			readback += ". Click another enemy, or Confirm. Backspace undoes; Esc cancels.";

		if(targetReadback->getText() != readback)
			targetReadback->setText(readback);
		undoButton->setEnabled(true);
		undoButton->block(preview.selectedTargetCount == 0);
		cancelButton->setEnabled(true);
		confirmButton->setEnabled(true);
		confirmButton->block(!preview.canConfirm);
	}

public:
	explicit BattleTargetSelectionPanel(BattleInterface & owner_)
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

	void update() { refresh(); }
	void show(Canvas & to) override { if(active) CIntObject::show(to); }
	void showAll(Canvas & to) override { if(active) CIntObject::showAll(to); }
};

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
		{
			if(this->owner.actionsController->stormOfDaggersTargetSelectionModeActive())
				this->owner.actionsController->confirmStormOfDaggersTargets();
			else if(this->owner.actionsController->soulChainTargetSelectionModeActive())
				this->owner.actionsController->confirmSoulChainTargets();
			else
				this->owner.actionsController->confirmRepeatedPlacement();
		}
	});
	addShortcut(EShortcut::GLOBAL_BACKSPACE, [this](){
		if(this->owner.actionsController)
		{
			if(this->owner.actionsController->realityWarpTargetSelectionModeActive())
				this->owner.actionsController->undoRealityWarpTarget();
			else if(this->owner.actionsController->vengefulVinesTargetSelectionModeActive())
				this->owner.actionsController->undoVengefulVinesSelection();
			else if(this->owner.actionsController->stormOfDaggersTargetSelectionModeActive())
				this->owner.actionsController->undoStormOfDaggersTarget();
			else if(this->owner.actionsController->soulChainTargetSelectionModeActive())
				this->owner.actionsController->undoSoulChainTarget();
			else
				this->owner.actionsController->undoRepeatedPlacement();
		}
	});
	addShortcut(EShortcut::GLOBAL_CANCEL, [this]()
	{
		if(this->owner.actionsController->heroOrderTargetingModeActive()
			&& (this->owner.getBattle()->battleHasPendingDoubleCommand(this->owner.getBattle()->battleGetMySide())
				|| this->owner.getBattle()->battleHasPendingPreCombatOrder(this->owner.getBattle()->battleGetMySide())))
		{
			this->owner.actionsController->cancelHeroOrderTargeting();
			this->owner.presentPendingHeroOrderChoice();
			return;
		}
		this->owner.actionsController->endCastingSpell();
	});
	setShortcutBlocked(EShortcut::GLOBAL_ACCEPT, true);
	setShortcutBlocked(EShortcut::GLOBAL_BACKSPACE, true);
	addShortcut(EShortcut::ADVENTURE_QUICK_LOAD, [this](){
		//allow quick load only on player turn while no animations are ongoing
		if (!this->owner.hasAnimations() && this->owner.stacksController->getActiveStack())
			GAME->interface()->proposeQuickLoadingGame(); });

	build(config);
	// Share one coherent leather selection strip across ordered targeting and
	// repeated spell placement. Its inset readback leaves the battlefield open
	// while native button faces provide Undo, Cancel, and Confirm.
	battleTargetSelectionPanel = std::make_shared<BattleTargetSelectionPanel>(owner);
	addWidget("nhBattleTargetSelection", battleTargetSelectionPanel);
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
			else if(owner.isDeploymentPhase())
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

	if (owner.isDeploymentPhase())
		tacticPhaseStarted(owner.isInTacticsMode());
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
		defenderHeroWindow = std::make_shared<HeroInfoBasicPanel>(info, nullptr, true, true);
	}
	if(owner.attackingHeroInstance)
	{
		InfoAboutHero info;
		info.initFromHero(owner.attackingHeroInstance, InfoAboutHero::EInfoLevel::INBATTLE);
		attackerHeroWindow = std::make_shared<HeroInfoBasicPanel>(info, nullptr, true, true);
	}
	if(attackerHeroWindow)
		attackerHeroStatus = std::make_shared<HeroBattleStatusArea>(
			Point(pos.x + 1 + HeroInfoPanelLayout::effectAreaLeft,
				pos.y + HeroInfoPanelLayout::compactPanelOffsetY + HeroInfoPanelLayout::effectAreaTop));
	if(defenderHeroWindow)
		defenderHeroStatus = std::make_shared<HeroBattleStatusArea>(
			Point(pos.x + pos.w - 79 + HeroInfoPanelLayout::effectAreaLeft,
				pos.y + HeroInfoPanelLayout::compactPanelOffsetY + HeroInfoPanelLayout::effectAreaTop));

	refreshHeroBattleStatus(BattleSide::ATTACKER);
	refreshHeroBattleStatus(BattleSide::DEFENDER);

	bool showInfoWindows = settings["battle"]["stickyHeroInfoWindows"].Bool();

	if(!showInfoWindows)
	{
		if(attackerHeroWindow)
			attackerHeroWindow->disable();

		if(defenderHeroWindow)
			defenderHeroWindow->disable();
		if(attackerHeroStatus)
			attackerHeroStatus->enable();
		if(defenderHeroStatus)
			defenderHeroStatus->enable();
	}
	else
	{
		if(attackerHeroStatus)
			attackerHeroStatus->disable();
		if(defenderHeroStatus)
			defenderHeroStatus->disable();
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
		|| owner.isDeploymentPhase() || !owner.makingTurn() || !quickSpellWindow)
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

	if(attackerHeroStatus)
		attackerHeroStatus->enable();

	if(defenderHeroStatus)
		defenderHeroStatus->enable();

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
	const bool placeOutside = placeInfoWindowsOutside();
	int xOffsetAttacker = quickSpellWindow->isDisabled() ? 0 : -51;
	int xOffsetDefender = unitActionWindow->isDisabled() ? 0 : 51;

	int yOffsetAttacker = attackerTimerWidget ? attackerTimerWidget->pos.h + 9 : 0;
	int yOffsetDefender = defenderTimerWidget ? defenderTimerWidget->pos.h + 9 : 0;
	const auto compactPanelY = [this](BattleSide side)
	{
		const int contentHeight = std::max(HeroInfoPanelLayout::height,
			HeroInfoPanelLayout::effectAreaTop + heroBattleStatusHeight(side));
		const int top = pos.y;
		const int bottom = std::max(top, ENGINE->screenDimensions().y - contentHeight);
		return std::clamp(pos.y + HeroInfoPanelLayout::compactPanelOffsetY, top, bottom);
	};

	if(defenderHeroWindow)
	{
		Point position = placeOutside
				? Point(pos.x + pos.w - 1 + xOffsetDefender, pos.y - 1 + yOffsetDefender)
				: Point(pos.x + pos.w -79, compactPanelY(BattleSide::DEFENDER));
		defenderHeroWindow->moveTo(position);
		const bool aboveBattlefield = !placeOutside;
		defenderHeroWindow->setAboveBattlefield(aboveBattlefield);
		defenderHeroWindow->setBattleStatusRenderDuringShow(!aboveBattlefield);
	}
	if(attackerHeroWindow)
	{
		Point position = placeOutside
				? Point(pos.x - 77 + xOffsetAttacker, pos.y - 1 + yOffsetAttacker)
				: Point(pos.x + 1, compactPanelY(BattleSide::ATTACKER));
		attackerHeroWindow->moveTo(position);
		const bool aboveBattlefield = !placeOutside;
		attackerHeroWindow->setAboveBattlefield(aboveBattlefield);
		attackerHeroWindow->setBattleStatusRenderDuringShow(!aboveBattlefield);
	}
	if(defenderHeroStatus && defenderHeroWindow)
		defenderHeroStatus->moveTo(Point(defenderHeroWindow->pos.x + HeroInfoPanelLayout::effectAreaLeft,
			defenderHeroWindow->pos.y + HeroInfoPanelLayout::effectAreaTop));
	if(attackerHeroStatus && attackerHeroWindow)
		attackerHeroStatus->moveTo(Point(attackerHeroWindow->pos.x + HeroInfoPanelLayout::effectAreaLeft,
			attackerHeroWindow->pos.y + HeroInfoPanelLayout::effectAreaTop));
	if(defenderStackWindow)
	{
		Point position = placeOutside
				? Point(pos.x + pos.w - 1 + xOffsetDefender,
					defenderHeroWindow ? defenderHeroWindow->pos.y + HeroInfoPanelLayout::effectAreaTop
						+ heroBattleStatusHeight(BattleSide::DEFENDER) + 3 : pos.y - 1 + yOffsetDefender)
				: Point(pos.x + pos.w -79,
					defenderHeroWindow ? compactPanelY(BattleSide::DEFENDER) : pos.y + HeroInfoPanelLayout::compactPanelOffsetY);
		defenderStackWindow->moveTo(position);
		defenderStackWindow->setAboveBattlefield(!placeOutside);
	}
	if(attackerStackWindow)
	{
		Point position = placeOutside
				? Point(pos.x - 77 + xOffsetAttacker,
					attackerHeroWindow ? attackerHeroWindow->pos.y + HeroInfoPanelLayout::effectAreaTop
						+ heroBattleStatusHeight(BattleSide::ATTACKER) + 3 : pos.y - 1 + yOffsetAttacker)
				: Point(pos.x + 1,
					attackerHeroWindow ? compactPanelY(BattleSide::ATTACKER) : pos.y + HeroInfoPanelLayout::compactPanelOffsetY);
		attackerStackWindow->moveTo(position);
		attackerStackWindow->setAboveBattlefield(!placeOutside);
	}
}

void BattleWindow::updateHeroInfoWindow(uint8_t side, const InfoAboutHero & hero)
{
	std::shared_ptr<HeroInfoBasicPanel> panelToUpdate = side == 0 ? attackerHeroWindow : defenderHeroWindow;
	if(panelToUpdate)
		panelToUpdate->update(hero);
	refreshHeroBattleStatus(side == 0 ? BattleSide::ATTACKER : BattleSide::DEFENDER);
	setPositionInfoWindow();
}

void BattleWindow::refreshHeroBattleStatus(BattleSide side)
{
	const auto battleCallback = owner.getBattle();
	if(!battleCallback)
		return;
	const auto * battle = battleCallback->getBattle();
	if(!battle)
		return;

	std::vector<CombatStatusEntry> entries;
	const bool counterspellArmed = battleCallback->battleWasCounterspellArmed(side);
	if(counterspellArmed)
	{
		entries.push_back({{}, "Ward", "ARMED",
			CInfoWindow::genText("Counterspell Ward", "The Counterspell ward is armed for this side.")});
	}

	const auto & warcasting = battle->getWarcastingState(side);
	const auto round = battle->getRound();
	const auto * visibleHero = battleCallback->battleGetFightingHero(side);
	if(hasActiveWarcasting(warcasting, round))
	{
		const auto action = warcasting.nextEligibleAction;
		// Only apply hero-specific Master Synthesis when this client can see the
		// fighting hero. Readiness itself remains a public saved battle state.
		const auto empowerment = visibleHero
			? newHorizonsWarcasting::effectiveBonus(visibleHero, warcasting, action, round)
			: warcasting.bonusFor(action, round);
		const auto actionName = action == AlternatingHeroActionState::Action::SPELL ? "Spell" : "Order";
		const auto amount = action == AlternatingHeroActionState::Action::SPELL
			? "+" + std::to_string(empowerment) + "%"
			: "+" + std::to_string(empowerment) + "pp";
		const auto actionEffect = action == AlternatingHeroActionState::Action::SPELL
			? "to its Spell Power-derived numerical component."
			: "to efficiency on attribute-derived components.";
		const auto amountDescription = action == AlternatingHeroActionState::Action::SPELL
			? "+" + std::to_string(empowerment) + "%"
			: "+" + std::to_string(empowerment) + " percentage points";
		const auto tooltip = CInfoWindow::genText("Warcasting",
			std::string("Next eligible action: ") + actionName + ". It gains " + amountDescription + " " + actionEffect
			+ " Available through round " + std::to_string(warcasting.expiryRound) + " (inclusive).");
		entries.push_back({warcastingIconName(action), actionName, amount, tooltip});
	}

	if(const auto * hero = visibleHero)
	{
		for(const auto & skill : LIBRARY->skillh->objects)
		{
			if(!skill || skill->getCombatStatusProvider() == CSkill::CombatStatusProvider::NONE)
				continue;
			const int skillRank = hero->getPerkSkillRank(skill->getJsonKey());
			if(skillRank <= 0)
				continue;

			std::string value;
			std::string providerDetails;
			switch(skill->getCombatStatusProvider())
			{
				case CSkill::CombatStatusProvider::METAMAGIC_USES:
				{
					const int total = newHorizonsMagic::metamagicRank(hero);
					if(total <= 0)
						continue;
					const int consumed = std::clamp(battleCallback->battleMetamagicUsesConsumed(side), 0, total);
					value = std::to_string(total - consumed) + " / " + std::to_string(total);
					break;
				}
				case CSkill::CombatStatusProvider::BLOODRAGE_DAMAGE:
				{
					const int cap = battle->getBloodrageCapPercent(side);
					if(cap <= 0)
						continue;
					const int current = std::clamp(battle->getBloodrageDamagePercent(side), 0, cap);
					// Keep current/cap legible in the compact 42 px value column.
					// The localized tooltip carries the fully expanded explanation.
					value = "+" + std::to_string(current) + "/" + std::to_string(cap) + "%";
					break;
				}
				case CSkill::CombatStatusProvider::DIVINE_MANDATE_USES:
				{
					const auto mandate = battleCallback->battleGetDivineMandateStatus(side);
					if(!mandate.active || mandate.maximumPairs == 0)
						continue;
					const auto completed = static_cast<unsigned>(mandate.completedPairs);
					const auto remaining = completed < mandate.maximumPairs
						? static_cast<unsigned>(mandate.maximumPairs) - completed : 0u;
					value = std::to_string(remaining) + " / " + std::to_string(mandate.maximumPairs);
					std::string details = "Completed Spell/Order pairs this combat: "
						+ std::to_string(completed) + " of " + std::to_string(mandate.maximumPairs)
						+ "; remaining pairs: " + std::to_string(remaining) + ".";
					if(mandate.pendingFollowup
						&& mandate.pendingFollowup->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
					{
						const auto followupName = mandate.pendingFollowup->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
							? "Light Spell" : "Order";
						details += "\nSource: Divine Mandate. Pending " + std::string(followupName)
							+ " follow-up through the end of round "
							+ std::to_string(mandate.pendingFollowup->expiryRound) + ".";
					}
					providerDetails = std::move(details);
					break;
				}
				case CSkill::CombatStatusProvider::NONE:
					continue;
			}
			const auto description = skill->getCombatStatusDescriptionTranslated();
			const auto details = providerDetails.empty()
				? (description.empty() ? value : description + "\n\n" + value)
				: (description.empty() ? providerDetails : description + "\n\n" + providerDetails);
			entries.push_back({skill->at(std::clamp(skillRank, 1, 3)).iconSmall,
				skill->getNameTranslated(), value, CInfoWindow::genText(skill->getNameTranslated(), details)});
		}
	}

	const bool showNormalHeroAction = battleCallback->battleUsesHeroCommands()
		&& battle->getSideHero(side) != nullptr;
	const auto actionCounts = showNormalHeroAction
		? battleCallback->battleHeroActionAllowanceCounts(side)
		: HeroActionAllowanceState::Counts{};
	const bool normalHeroActionAvailable = showNormalHeroAction && actionCounts.heroActions > 0;

	const auto panel = side == BattleSide::ATTACKER ? attackerHeroWindow : defenderHeroWindow;
	if(panel)
		panel->setBattleStatus(entries, normalHeroActionAvailable, showNormalHeroAction);

	const auto statusArea = side == BattleSide::ATTACKER ? attackerHeroStatus : defenderHeroStatus;
	if(statusArea)
		statusArea->setStatus(entries, normalHeroActionAvailable, showNormalHeroAction);
}

int BattleWindow::heroBattleStatusHeight(BattleSide side) const
{
	if(settings["battle"]["stickyHeroInfoWindows"].Bool())
	{
		const auto panel = side == BattleSide::ATTACKER ? attackerHeroWindow : defenderHeroWindow;
		return panel ? panel->battleStatusHeight() : 0;
	}

	const auto statusArea = side == BattleSide::ATTACKER ? attackerHeroStatus : defenderHeroStatus;
	return statusArea ? statusArea->statusHeight() : 0;
}

void BattleWindow::updateCounterspellStatus()
{
	refreshHeroBattleStatus(BattleSide::ATTACKER);
	refreshHeroBattleStatus(BattleSide::DEFENDER);
	setPositionInfoWindow();
}

void BattleWindow::updateStackInfoWindow(const CStack * stack)
{
	OBJECT_CONSTRUCTION;

	bool showInfoWindows = settings["battle"]["stackInfoBasicPanel"].Bool();
	const auto battleCallback = owner.getBattle();

	if(stack && stack->unitSide() == BattleSide::DEFENDER)
	{
		defenderStackWindow = std::make_shared<StackInfoBasicPanel>(stack, battleCallback, true);
		defenderStackWindow->setEnabled(showInfoWindows);
	}
	else
		defenderStackWindow = nullptr;
	
	if(stack && stack->unitSide() == BattleSide::ATTACKER)
	{
		attackerStackWindow = std::make_shared<StackInfoBasicPanel>(stack, battleCallback, true);
		attackerStackWindow->setEnabled(showInfoWindows);
	}
	else
		attackerStackWindow = nullptr;
	
	createTimerInfoWindows();
	setPositionInfoWindow();
	redraw();
}

void BattleWindow::refreshHoveredStackStatus(const CStack * stack)
{
	if(!stack)
		return;

	auto panel = stack->unitSide() == BattleSide::DEFENDER ? defenderStackWindow : attackerStackWindow;
	if(panel)
		panel->refreshDefendStatus(stack);
}

bool BattleWindow::cursorOverStackInfoWindow() const
{
	const Point cursor = ENGINE->getCursorPosition();
	const auto containsCursor = [&cursor](const std::shared_ptr<StackInfoBasicPanel> & panel)
	{
		return panel && !panel->isDisabled() && panel->containsPoint(cursor);
	};
	return containsCursor(attackerStackWindow) || containsCursor(defenderStackWindow);
}

bool BattleWindow::hasStackInfoWindow() const
{
	return attackerStackWindow || defenderStackWindow;
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

void BattleWindow::tacticPhaseStarted(bool localController)
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

	if(localController)
	{
		menuTactics->enable();
		tacticNext->enable();
		tacticEnd->enable();
	}
	else
	{
		menuTactics->disable();
		tacticNext->disable();
		tacticEnd->disable();
	}

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
	if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || owner.curInt->isAutoFightOn || owner.isDeploymentPhase())
		return;
	openSpellbook();
}

void BattleWindow::bOrdersf()
{
	if(CPlayerInterface::battleInt.get() != &owner || !owner.curInt || owner.curInt->isAutoFightOn)
		return;
	if(!owner.getBattle()->battleUsesHeroCommands() || owner.actionsController->heroSpellcastingModeActive()
		|| !owner.makingTurn() || owner.isDeploymentPhase() || !owner.currentHero())
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
			|| owner.isDeploymentPhase() || !owner.currentHero() || owner.actionsController->heroSpellcastingModeActive();
		ordersButton->block(ordersBlocked);
		// Disabled issuance must not hide read-only help. Do not restore click/key events.
		ordersButton->addUsedEvents(SHOW_POPUP);
		setShortcutBlocked(EShortcut::BATTLE_OPEN_ORDERS, ordersBlocked);
	}

	bool canWait = owner.stacksController->getActiveStack() ? !owner.stacksController->getActiveStack()->waitedThisTurn : false;
	const auto * activeStack = owner.stacksController->getActiveStack();
	const auto activeStackState = activeStack ? activeStack->acquireState() : nullptr;
	const bool rangedFollowUpPending = activeStackState
		&& activeStackState->rangedFollowUpDamagePercent > 0;
	const bool deploymentPhase = owner.isDeploymentPhase();
	const bool localTacticsMode = owner.isInTacticsMode();

	setShortcutBlocked(EShortcut::GLOBAL_OPTIONS, on);
	setShortcutBlocked(EShortcut::BATTLE_OPEN_ACTIVE_UNIT, on);
	setShortcutBlocked(EShortcut::BATTLE_OPEN_HOVERED_UNIT, on);
	setShortcutBlocked(EShortcut::BATTLE_RETREAT, on || deploymentPhase || !owner.getBattle()->battleCanFlee());
	setShortcutBlocked(EShortcut::BATTLE_SURRENDER, on || deploymentPhase || owner.getBattle()->battleGetSurrenderCost() < 0);
	setShortcutBlocked(EShortcut::BATTLE_CAST_SPELL, on || deploymentPhase || !canCastSpells);
	setShortcutBlocked(EShortcut::BATTLE_WAIT, on || deploymentPhase || !canWait || rangedFollowUpPending);
	setShortcutBlocked(EShortcut::BATTLE_DEFEND, on || deploymentPhase);
	const bool autoCombatShortcutDisabled = settings["battle"]["endWithAutocombat"].Bool() && onlyOnePlayerHuman;
	setShortcutBlocked(EShortcut::BATTLE_AUTOCOMBAT, deploymentPhase
		|| (autoCombatShortcutDisabled ? on || owner.actionsController->heroSpellcastingModeActive()
			: owner.actionsController->heroSpellcastingModeActive()));
	setShortcutBlocked(EShortcut::BATTLE_END_WITH_AUTOCOMBAT,
		on || deploymentPhase || !onlyOnePlayerHuman || owner.actionsController->heroSpellcastingModeActive());
	setShortcutBlocked(EShortcut::BATTLE_TACTICS_END, on || !localTacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_TACTICS_NEXT, on || !localTacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_CONSOLE_DOWN, on && !localTacticsMode);
	setShortcutBlocked(EShortcut::BATTLE_CONSOLE_UP, on && !localTacticsMode);
	updateBattleTargetSelectionControls();

	quickSpellWindow->setInputEnabled(!on && !deploymentPhase);
	unitActionWindow->setInputEnabled(!on && !deploymentPhase);
}

void BattleWindow::updateBattleTargetSelectionControls()
{
	const bool active = owner.actionsController && owner.actionsController->repeatedPlacementModeActive();
	const bool ready = active && owner.actionsController->repeatedPlacementReady();
	const bool canUndo = active && !owner.actionsController->getRepeatedPlacementSelectedHexes().empty();
	const bool vengefulVinesActive = owner.actionsController
		&& owner.actionsController->vengefulVinesTargetSelectionModeActive();
	const bool vengefulVinesCanUndo = vengefulVinesActive
		&& !owner.actionsController->getVengefulVinesSelectedHexes().empty();
	const bool stormActive = owner.actionsController
		&& owner.actionsController->stormOfDaggersTargetSelectionModeActive();
	const bool stormCanConfirm = stormActive
		&& !owner.actionsController->stormOfDaggersSelectedTargetIds().empty();
	const bool stormCanUndo = stormCanConfirm;
	const bool soulChainActive = owner.actionsController
		&& owner.actionsController->soulChainTargetSelectionModeActive();
	const bool soulChainCanConfirm = soulChainActive
		&& !owner.actionsController->soulChainSelectedTargetIds().empty();
	const bool soulChainCanUndo = soulChainCanConfirm;
	const auto * activeStack = owner.stacksController->getActiveStack();
	const auto activeStackState = activeStack ? activeStack->acquireState() : nullptr;
	const bool rangedFollowUpPending = activeStackState
		&& activeStackState->rangedFollowUpDamagePercent > 0;
	setShortcutBlocked(EShortcut::GLOBAL_ACCEPT,
		!ready && !stormCanConfirm && !soulChainCanConfirm);
	setShortcutBlocked(EShortcut::GLOBAL_BACKSPACE,
		!canUndo && !vengefulVinesCanUndo && !stormCanUndo && !soulChainCanUndo
			&& !(owner.actionsController && owner.actionsController->realityWarpTargetSelectionModeActive()));
	widget<CButton>("wait")->setEnabled(!rangedFollowUpPending
		&& !active && !vengefulVinesActive && !stormActive && !soulChainActive);
	const auto * overwatchStack = owner.stacksController->getActiveStack();
	if(overwatchStack && overwatchStack->isShooter()
		&& newHorizonsBattlecraft::hasOverwatch(owner.getBattle()->battleGetOwnerHero(overwatchStack)))
		widget<CButton>("wait")->setHelp(CButton::tooltip("Wait / Overwatch",
			"Wait arms one 50%-damage ranged reaction against the first legal enemy voluntarily entering range "
			+ std::to_string(newHorizonsBattlecraft::overwatchRange(overwatchStack))
			+ " before the delayed activation. Uses one shot; once per round."));
	else
		widget<CButton>("wait")->setHelp(CButton::tooltipLocalized("core.help.386"));
	if(rangedFollowUpPending)
		widget<CButton>("defence")->setHelp(CButton::tooltip(
			"End activation", "Decline the pending Master Gunner second shot and end this activation."));
	else
		widget<CButton>("defence")->setHelp(CButton::tooltipLocalized("core.help.387"));
	if(battleTargetSelectionPanel)
		battleTargetSelectionPanel->update();
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

	const bool enoughWidth = quickActionsPanelActive()
		? ENGINE->screenDimensions().x >= widthWithQuickActions
		: ENGINE->screenDimensions().x >= widthBaseWindow;
	if(!enoughWidth)
		return false;

	int timerOffset = 0;
	const auto & turnTimers = GAME->interface()->cb->getStartInfo()->turnTimerInfo;
	if(turnTimers.battleTimer != 0 || turnTimers.unitTimer != 0)
	{
		const int bigFontHeight = static_cast<int>(ENGINE->renderHandler().loadFont(FONT_BIG)->getLineHeight());
		timerOffset = 6 + bigFontHeight - 4;
		if(turnTimers.battleTimer != 0)
			timerOffset += bigFontHeight;
		if(!turnTimers.accumulatingUnitTimer && turnTimers.unitTimer != 0)
			timerOffset += bigFontHeight;
		timerOffset += 9;
	}

	// placeInfoWindowsOutside() is shared by the hero and stack panel layouts.
	// If the complete outside column would extend past the viewport, use the
	// existing compact overlay layout rather than hiding the bottom of the stack
	// readout below the screen.
	const int attackerOffset = attackerHeroWindow
		? HeroInfoPanelLayout::effectAreaTop + heroBattleStatusHeight(BattleSide::ATTACKER) + 3 : 0;
	const int defenderOffset = defenderHeroWindow
		? HeroInfoPanelLayout::effectAreaTop + heroBattleStatusHeight(BattleSide::DEFENDER) + 3 : 0;
	const int stackPanelBottom = pos.y - 1 + timerOffset
		+ std::max(attackerOffset, defenderOffset) + outsideStackInfoPanelExtent;
	return stackPanelBottom <= ENGINE->screenDimensions().y;
}

bool BattleWindow::quickActionsPanelActive() const
{
	return unitActionWindow->isActive();
}
