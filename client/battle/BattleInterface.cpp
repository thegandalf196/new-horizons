/*
 * BattleInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleHeroActionWindow.h"
#include "BattleInterface.h"

#include "BattleActionsController.h"
#include "BattleAnimationClasses.h"
#include "BattleConsole.h"
#include "BattleEffectsController.h"
#include "BattleFieldController.h"
#include "BattleHero.h"
#include "BattleObstacleController.h"
#include "BattleProjectileController.h"
#include "BattleRenderer.h"
#include "BattleResultWindow.h"
#include "BattleSiegeController.h"
#include "BattleStacksController.h"
#include "BattleWindow.h"
#include "CreatureAnimation.h"
#include "TemporalFieldWindow.h"

#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../CServerHandler.h"
#include "../GameInstance.h"
#include "../adventureMap/AdventureMapInterface.h"
#include "../gui/CursorHandler.h"
#include "../gui/WindowHandler.h"
#include "media/IMusicPlayer.h"
#include "media/ISoundPlayer.h"
#include "render/Canvas.h"
#include "../windows/CTutorialWindow.h"

#include "../../lib/BattleFieldHandler.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/CThreadHelper.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/TerrainHandler.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/InfoAboutArmy.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/texts/CGeneralTextHandler.h"

BattleInterface::BattleInterface(const BattleID & battleID, const CCreatureSet *army1, const CCreatureSet *army2,
		const CGHeroInstance *hero1, const CGHeroInstance *hero2,
		std::shared_ptr<CPlayerInterface> att,
		std::shared_ptr<CPlayerInterface> defen,
		std::shared_ptr<CPlayerInterface> spectatorInt)
	: attackingHeroInstance(hero1)
	, defendingHeroInstance(hero2)
	, attackerInt(att)
	, defenderInt(defen)
	, curInt(att)
	, battleID(battleID)
	, battleOpeningDelayActive(true)
	, round(0)
{
	if(spectatorInt)
	{
		curInt = spectatorInt;
	}
	else if(!curInt)
	{
		//May happen when we are defending during network MP game -> attacker interface is just not present
		curInt = defenderInt;
	}

	//hot-seat -> check tactics for both players (defender may be local human)
	if(attackerInt && attackerInt->cb->getBattle(getBattleID())->battleGetTacticDist())
		tacticianInterface = attackerInt;
	else if(defenderInt && defenderInt->cb->getBattle(getBattleID())->battleGetTacticDist())
		tacticianInterface = defenderInt;

	//initializing armies
	this->army1 = army1;
	this->army2 = army2;

	const CGTownInstance *town = getBattle()->battleGetDefendedTown();
	if(town && town->fortificationsLevel().wallsHealth > 0)
		siegeController.reset(new BattleSiegeController(*this, town));

	windowObject = std::make_shared<BattleWindow>(*this);
	projectilesController.reset(new BattleProjectileController(*this));
	stacksController.reset( new BattleStacksController(*this));
	actionsController.reset( new BattleActionsController(*this));
	effectsController.reset(new BattleEffectsController(*this));
	obstacleController.reset(new BattleObstacleController(*this));
	installMagicArrowOverchargeUI();
	installSelectiveDispelUI();
	installTemporalFieldUI();

	adventureInt->onAudioPaused();
	ongoingAnimationsState.setBusy();

	ENGINE->windows().pushWindow(windowObject);
	windowObject->blockUI(true);
	windowObject->updateQueue();

	playIntroSoundAndUnlockInterface();
}

void BattleInterface::installMagicArrowOverchargeUI()
{
	if(!actionsController)
		return;

	actionsController->setMagicArrowOverchargeFactory(
		[this](const BattleAction & pending, const BattleHex & targetHex, const CStack * initialTarget)
			-> std::optional<MagicArrowOverchargeContext>
		{
			if(!curInt || !curInt->cb || !initialTarget)
				return std::nullopt;

			const auto callback = curInt->cb->getBattle(getBattleID());
			if(!callback || !callback->getBattle())
				return std::nullopt;

			const auto * spell = pending.spell.toSpell();
			const auto * hero = currentHero();
			if(!spell || spell->id != SpellID(SpellID::MAGIC_ARROW) || !hero
				|| !newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), spell->id))
				return std::nullopt;

			const uint32_t targetUnitID = initialTarget->unitId();
			const BattleID localBattleID = getBattleID();
			const int32_t spellPower = hero->getEffectPower(spell);
			const int formulaMaximumOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
				callback->getBattle()->getMagicRules(), spell->id, spellPower,
				newHorizonsMagic::magicArrowOverchargeModifiers(hero));
			const int baseMana = callback->battleGetSpellCost(spell, hero);
			const int maximumOvercharge = std::min(formulaMaximumOvercharge, std::max(0, hero->mana - baseMana));

			// battleGetSpellCost is the authoritative ordinary cost.  Runtime owns
			// Wisdom and other modifiers; the panel only labels this value as the
			// Wisdom-adjusted base and adds the optional surcharge separately.
			const auto evaluate = [this, localBattleID, targetUnitID, spell, maximumOvercharge]
				(int overcharge)
				-> MagicArrowOverchargeValues
			{
				MagicArrowOverchargeValues values;
				values.maximumOvercharge = maximumOvercharge;
				values.overcharge = std::clamp(overcharge, 0, maximumOvercharge);

				if(!curInt || !curInt->cb || curInt->cb->getBattle(localBattleID) == nullptr)
					return values;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				const auto * target = callback->battleGetUnitByID(targetUnitID);
				if(!callback || !callback->getBattle() || !hero || !target)
					return values;

				values.baseMana = callback->battleGetSpellCost(spell, hero);
				values.additionalMana = values.overcharge;
				values.totalMana = values.baseMana + values.additionalMana;
				values.availableMana = hero->mana;
				values.affordable = values.totalMana <= values.availableMana;
				values.targetDescription = "Target: " + std::to_string(target->getCount()) + " "
					+ target->unitType()->getNamePluralTranslated();

				const auto previewDamage = [&](int selectedOvercharge)
				{
					spells::BattleCast preview(callback.get(), hero, spells::Mode::HERO, spell);
					preview.setOvercharge(selectedOvercharge);
					auto mechanics = spell->battleMechanics(&preview);
					return static_cast<int>(mechanics->adjustEffectValue(target));
				};
				values.baseDamage = previewDamage(0);
				values.projectedDamage = previewDamage(values.overcharge);
				return values;
			};

			MagicArrowOverchargeContext context;
			context.anchor = ENGINE->getCursorPosition();
			context.initial = evaluate(0);
			context.evaluate = evaluate;
			context.confirm = [this, pending, localBattleID, targetUnitID, spell, formulaMaximumOvercharge](int overcharge)
				-> bool
			{
				if(!curInt || !curInt->cb || curInt->cb->getBattle(localBattleID) == nullptr)
					return false;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				const auto * target = callback->battleGetUnitByID(targetUnitID);
				if(!callback || !callback->getBattle() || !hero || !target || overcharge < 0 || overcharge > formulaMaximumOvercharge
					|| !newHorizonsMagic::magicArrowOverchargeEnabled(callback->getBattle()->getMagicRules(), spell->id))
					return false;

				const int baseCost = callback->battleGetSpellCost(spell, hero);
				if(baseCost < 0 || baseCost + overcharge > hero->mana)
					return false;

				BattleAction action = pending;
				action.target.clear();
				action.aimToUnit(target);
				action.spellOvercharge = overcharge;
				curInt->cb->battleMakeSpellAction(localBattleID, action);
				if(actionsController)
					actionsController->endCastingSpell();
				return true;
			};
			context.cancel = [this]
			{
				if(actionsController)
					actionsController->endCastingSpell();
			};
			return context;
		});
}

void BattleInterface::installSelectiveDispelUI()
{
	if(!actionsController)
		return;

	actionsController->setSelectiveDispelFactory(
		[this](const BattleAction & pending, const CStack * initialTarget)
			-> std::optional<SelectiveDispelContext>
		{
			if(!curInt || !curInt->cb)
				return std::nullopt;
			const BattleID localBattleID = getBattleID();
			const auto callback = curInt->cb->getBattle(localBattleID);
			const auto * hero = currentHero();
			if(!callback || !callback->getBattle() || !hero || pending.spell != SpellID(SpellID::DISPEL)
				|| !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"))
				return std::nullopt;

			const std::optional<uint32_t> targetUnitID = initialTarget
				? std::make_optional(initialTarget->unitId()) : std::nullopt;
			SelectiveDispelContext context;
			context.anchor = ENGINE->getCursorPosition();
			context.targetDescription = initialTarget
				? "Target: " + std::to_string(initialTarget->getCount()) + " " + initialTarget->unitType()->getNamePluralTranslated()
				: "Target: all affected stacks";
			context.confirm = [this, pending, localBattleID, targetUnitID](bool selective)
				-> bool
			{
				if(!curInt || !curInt->cb)
					return false;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				const auto * target = callback && targetUnitID ? callback->battleGetUnitByID(*targetUnitID) : nullptr;
				const auto * spell = SpellID(SpellID::DISPEL).toSpell();
				if(!callback || !callback->getBattle() || !hero || (targetUnitID && !target) || !spell
					|| (selective && !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel")))
					return false;

				spells::BattleCast preview(callback.get(), hero, spells::Mode::HERO, spell);
				preview.setSelectiveDispel(selective);
				auto mechanics = spell->battleMechanics(&preview);
				spells::detail::ProblemImpl problem;
				if(!mechanics->canBeCast(problem))
					return false;
				if(target)
				{
					battle::Target targetCheck;
					targetCheck.emplace_back(target, target->getPosition());
					if(!mechanics->canBeCastAt(targetCheck, problem))
						return false;
				}

				BattleAction action = pending;
				action.target.clear();
				if(target)
					action.aimToUnit(target);
				else
					action.aimToHex(BattleHex::INVALID);
				action.spellSelectiveDispel = selective;
				curInt->cb->battleMakeSpellAction(localBattleID, action);
				if(actionsController)
					actionsController->endCastingSpell();
				return true;
			};
			context.cancel = [this]
			{
				if(actionsController)
					actionsController->endCastingSpell();
			};
			return context;
		});
}

void BattleInterface::installTemporalFieldUI()
{
	if(!actionsController)
		return;

	actionsController->setTemporalFieldFactory(
		[this](const BattleAction & pending)
			-> std::optional<TemporalFieldContext>
		{
			if(!curInt || !curInt->cb || pending.spell != SpellID(SpellID::SLOW))
				return std::nullopt;

			const BattleID localBattleID = getBattleID();
			const auto callback = curInt->cb->getBattle(localBattleID);
			const auto * hero = currentHero();
			const auto * spell = pending.spell.toSpell();
			if(!callback || !callback->getBattle() || !hero || !spell
				|| !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField"))
				return std::nullopt;

			const auto casterSide = callback->battleGetMySide();
			if(casterSide == BattleSide::NONE)
				return std::nullopt;

			auto evaluate = [this, localBattleID, spell]() -> TemporalFieldValues
			{
				TemporalFieldValues values;
				if(!curInt || !curInt->cb)
					return values;

				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				if(!callback || !callback->getBattle() || !hero || !spell)
					return values;

				values.ordinaryMana = callback->battleGetSpellCost(spell, hero);
				values.massMana = callback->battleGetSpellCost(spell, hero, 3);
				values.availableMana = hero->mana;
				const auto side = callback->battleGetMySide();
				values.remaining = side != BattleSide::NONE && !callback->battleWasTemporalFieldUsed(side);

				spells::BattleCast massCast(callback.get(), hero, spells::Mode::HERO, spell);
				massCast.setMassSlow(true);
				auto mechanics = spell->battleMechanics(&massCast);
				spells::Target noTarget;
				noTarget.emplace_back(BattleHex::INVALID);
				if(mechanics)
				{
					spells::detail::ProblemImpl problem;
					values.massAffordable = values.remaining && mechanics->canBeCast(problem);
					const auto affected = mechanics->getAffectedStacks(noTarget);
					values.eligibleEnemyCount = static_cast<int>(affected.size());

					std::string names;
					constexpr size_t maxPreviewNames = 3;
					for(size_t index = 0; index < affected.size() && index < maxPreviewNames; ++index)
					{
						if(!names.empty())
							names += ", ";
						names += affected[index]->unitType()->getNamePluralTranslated();
					}
					if(affected.size() > maxPreviewNames)
						names += ", ...";
					values.eligibleEnemyDescription = "Eligible enemies (" + std::to_string(values.eligibleEnemyCount) + "): " + names;
				}
				return values;
			};

			TemporalFieldContext context;
			context.anchor = ENGINE->getCursorPosition();
			context.initial = evaluate();
			context.evaluate = evaluate;
			context.confirmOrdinary = [this]
			{
				return actionsController && actionsController->continueOrdinarySpellcast();
			};
			context.confirmMass = [this, pending, localBattleID, spell]() -> bool
			{
				if(!curInt || !curInt->cb)
					return false;
				const auto callback = curInt->cb->getBattle(localBattleID);
				const auto * hero = currentHero();
				if(!callback || !callback->getBattle() || !hero || !spell
					|| !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField"))
					return false;

				const auto side = callback->battleGetMySide();
				if(side == BattleSide::NONE || callback->battleWasTemporalFieldUsed(side))
					return false;

				spells::BattleCast massCast(callback.get(), hero, spells::Mode::HERO, spell);
				massCast.setMassSlow(true);
				auto mechanics = spell->battleMechanics(&massCast);
				if(!mechanics)
					return false;
				spells::detail::ProblemImpl problem;
				if(!mechanics->canBeCast(problem))
					return false;
				spells::Target noTarget;
				noTarget.emplace_back(BattleHex::INVALID);
				if(mechanics->getAffectedStacks(noTarget).empty())
					return false;

				BattleAction action = pending;
				action.target.clear();
				action.aimToHex(BattleHex::INVALID);
				action.spellMassSlow = true;
				curInt->cb->battleMakeSpellAction(localBattleID, action);
				if(actionsController)
					actionsController->endCastingSpell();
				return true;
			};
			context.cancel = [this]
			{
				if(actionsController)
					actionsController->endCastingSpell();
			};
			return context;
		});
}

bool BattleInterface::isInTacticsMode()
{
	return tacticianInterface && tacticianInterface->cb->getBattle(getBattleID())->battleGetTacticDist() > 0;
}

void BattleInterface::playIntroSoundAndUnlockInterface()
{
	auto onIntroPlayed = [this]()
	{
		// Make sure that battle have not ended while intro was playing AND that a different one has not started
		if(GAME->interface()->battleInt.get() == this)
			onIntroSoundPlayed();
	};

	auto bfieldType = getBattle()->battleGetBattlefieldType();
	const auto & battlefieldSound = bfieldType.getInfo()->musicFilename;

	std::vector<soundBase::soundID> battleIntroSounds =
	{
		soundBase::battle00, soundBase::battle01,
		soundBase::battle02, soundBase::battle03, soundBase::battle04,
		soundBase::battle05, soundBase::battle06, soundBase::battle07
	};

	int battleIntroSoundChannel = -1;

	if (!battlefieldSound.empty())
		battleIntroSoundChannel = ENGINE->sound().playSound(battlefieldSound);
	else
		battleIntroSoundChannel = ENGINE->sound().playSoundFromSet(battleIntroSounds);

	if (battleIntroSoundChannel != -1)
	{
		ENGINE->sound().setCallback(battleIntroSoundChannel, onIntroPlayed);

		// a replay is watched, not played - the intro is always skipped there
		if (settings["gameTweaks"]["skipBattleIntroMusic"].Bool() || GAME->server().isReplayActive())
			openingEnd();
	}
	else // failed to play sound
	{
		onIntroSoundPlayed();
	}
}

bool BattleInterface::openingPlaying() const
{
	return battleOpeningDelayActive;
}

void BattleInterface::onIntroSoundPlayed()
{
	if (openingPlaying())
		openingEnd();

	auto bfieldType = getBattle()->battleGetBattlefieldType();
	const auto & battlefieldMusic = bfieldType.getInfo()->musicFilename;

	if (!battlefieldMusic.empty())
		ENGINE->music().playMusic(battlefieldMusic, true, true);
	else
		ENGINE->music().playMusicFromSet("battle", true, true);
}

void BattleInterface::openingEnd()
{
	assert(openingPlaying());
	if (!openingPlaying())
		return;

	onAnimationsFinished();
	if(isInTacticsMode())
	{
		// h3 tactics phase tutorial
		if(!persistentStorage["gui"]["tacticsPhaseHintShown"].Bool())
		{
			curInt->showInfoDialog(LIBRARY->generaltexth->translate("core.genrltxt.372"));
			Settings s = persistentStorage.write["gui"]["tacticsPhaseHintShown"];
			s->Bool() = true;
		}
		tacticNextStack(nullptr);
	}
	activateStack();
	battleOpeningDelayActive = false;

	CTutorialWindow::openWindowFirstTime(TutorialMode::TOUCH_BATTLE);
}

BattleInterface::~BattleInterface()
{
	CPlayerInterface::battleInt = nullptr;

	if (adventureInt)
		adventureInt->onAudioResumed();

	awaitingEvents.clear();
	onAnimationsFinished();
}

void BattleInterface::redrawBattlefield()
{
	fieldController->redrawBackgroundWithHexes();
	ENGINE->windows().totalRedraw();
}

void BattleInterface::stackReset(const CStack * stack)
{
	stacksController->stackReset(stack);
}

void BattleInterface::stackAdded(const CStack * stack)
{
	stacksController->stackAdded(stack, false);
}

void BattleInterface::stackRemoved(uint32_t stackID)
{
	stacksController->stackRemoved(stackID);
	fieldController->redrawBackgroundWithHexes();
	windowObject->updateQueue();
}

void BattleInterface::stackActivated(const CStack *stack)
{
	stacksController->stackActivated(stack);
}

void BattleInterface::stackMoved(const CStack *stack, const BattleHexArray & destHex, int distance, bool teleport)
{
	if (teleport)
		stacksController->stackTeleported(stack, destHex, distance);
	else
		stacksController->stackMoved(stack, destHex, distance);
}

void BattleInterface::stacksAreAttacked(std::vector<StackAttackedInfo> attackedInfos)
{
	stacksController->stacksAreAttacked(attackedInfos);

	BattleSideArray<int> killedBySide{0,0};

	for(const StackAttackedInfo & attackedInfo : attackedInfos)
	{
		BattleSide side = attackedInfo.defender->unitSide();
		killedBySide.at(side) += attackedInfo.amountKilled;
	}

	for(BattleSide side : { BattleSide::ATTACKER, BattleSide::DEFENDER })
	{
		if(killedBySide.at(side) > killedBySide.at(getBattle()->otherSide(side)))
			setHeroAnimation(side, EHeroAnimType::DEFEAT);
		else if(killedBySide.at(side) < killedBySide.at(getBattle()->otherSide(side)))
			setHeroAnimation(side, EHeroAnimType::VICTORY);
	}
}

void BattleInterface::stackAttacking( const StackAttackInfo & attackInfo )
{
	stacksController->stackAttacking(attackInfo);
}

void BattleInterface::newRoundFirst()
{
	waitForAnimations();
}

void BattleInterface::newRound()
{
	console->addText(LIBRARY->generaltexth->allTexts[412]);
	round++;
}

void BattleInterface::giveCommand(EActionType action, const BattleHex & tile, SpellID spell)
{
	std::vector<BattleHex> tiles = {tile};
	giveCommand(action, tiles, spell);
}

void BattleInterface::giveCommand(EActionType action, const std::vector<BattleHex> & tiles,  SpellID spell)
{
	const CStack * actor = nullptr;
	if(action != EActionType::HERO_SPELL && action != EActionType::RETREAT && action != EActionType::SURRENDER)
	{
		actor = stacksController->getActiveStack();
	}

	auto side = getBattle()->playerToSide(curInt->playerID);
	if(side == BattleSide::NONE)
	{
		logGlobal->error("Player %s is not in battle", curInt->playerID.toString());
		return;
	}

	BattleAction ba;
	ba.side = side;
	ba.actionType = action;

	for(auto & tile : tiles)
		ba.aimToHex(tile);
	ba.spell = spell;

	sendCommand(ba, actor);
}

void BattleInterface::sendCommand(BattleAction command, const CStack * actor)
{
	command.stackNumber = actor ? actor->unitId() : ((command.side == BattleSide::ATTACKER) ? -1 : -2);

	if(!isInTacticsMode())
	{
		logGlobal->trace("Setting command for %s", (actor ? actor->nodeName() : "hero"));
		stacksController->setActiveStack(nullptr);
		curInt->cb->battleMakeUnitAction(battleID, command);
	}
	else
	{
		curInt->cb->battleMakeTacticAction(battleID, command);
		stacksController->setActiveStack(nullptr);
		//next stack will be activated when action ends
	}
	ENGINE->cursor().set(Cursor::Combat::POINTER);
}

const CGHeroInstance * BattleInterface::getActiveHero()
{
	const CStack *attacker = stacksController->getActiveStack();
	if(!attacker)
	{
		return nullptr;
	}

	if(attacker->unitSide() == BattleSide::ATTACKER)
	{
		return attackingHeroInstance;
	}

	return defendingHeroInstance;
}

void BattleInterface::stackIsCatapulting(const CatapultAttack & ca)
{
	if (!siegeController)
		return;

	// a spell-caused catapult (earthquake, attacker == -1) applied while a hero cast is mid-flight must play
	// at the caster's climax (HIT stage); otherwise the wall explosions appear before the hero's cast animation
	if (ca.attacker == -1 && !awaitingEvents.empty())
		addToAnimationStage(EAnimationEvents::HIT, [this, ca](){ siegeController->stackIsCatapulting(ca); });
	else
		siegeController->stackIsCatapulting(ca);
}

void BattleInterface::gateStateChanged(const EGateState state)
{
	if (siegeController)
		siegeController->gateStateChanged(state);
}

void BattleInterface::battleFinished(const BattleResult& br, QueryID queryID)
{
	checkForAnimations();
	stacksController->setActiveStack(nullptr);

	ENGINE->cursor().set(Cursor::Map::POINTER);
	curInt->waitWhileDialog();

	if(settings["session"]["spectate"].Bool() && settings["session"]["spectate-skip-battle-result"].Bool())
	{
		curInt->cb->selectionMade(0, queryID);
		windowObject->close();
		return;
	}

	// a replay has nothing to ask, so the result window is skipped like every other dialog
	if(GAME->server().isReplayActive())
	{
		windowObject->close();
		CPlayerInterface::battleInt.reset();
		return;
	}

	auto wnd = std::make_shared<BattleResultWindow>(br, *(this->curInt));
	wnd->resultCallback = [this, queryID](ui32 selection)
	{
		curInt->cb->selectionMade(selection, queryID);
	};
	ENGINE->windows().pushWindow(wnd);

	curInt->waitWhileDialog(); // Avoid freeze when AI end turn after battle. Check bug #1897
	CPlayerInterface::battleInt.reset();
}

void BattleInterface::spellCast(const BattleSpellCast * sc)
{
	waitForAnimations();
	if(windowObject)
		windowObject->updateCounterspellStatus();

	// Do not deactivate anything in tactics mode
	// This is battlefield setup spells
	if(!isInTacticsMode())
	{
		windowObject->blockUI(true);

		// Disable current active stack duing the cast
		// Store the current activeStack to stackToActivate
		stacksController->deactivateStack();
	}

	ENGINE->cursor().set(Cursor::Combat::BLOCKED);

	const SpellID spellID = sc->spellID;

	if(!spellID.hasValue())
		return;

	const CSpell * spell = spellID.toSpell();
	auto targetedTile = sc->tile;

	const AudioPath & castSoundPath = spell->getCastSound();

	if (!castSoundPath.empty())
	{
		auto group = spell->animationInfo.projectile.empty() ?
					EAnimationEvents::HIT:
					EAnimationEvents::BEFORE_HIT;//FIXME: recheck whether this should be on projectile spawning

		addToAnimationStage(group, [=]() {
			ENGINE->sound().playSound(castSoundPath);
		});
	}

	if ( sc->activeCast )
	{
		const CStack * casterStack = getBattle()->battleGetStackByID(sc->casterStack);

		if(casterStack != nullptr )
		{
			// mass spells (RANGE:X) have no target hex, so there is no direction to turn towards
			if (targetedTile.isValid() && stacksController->shouldRotate(casterStack, casterStack->getPosition(), targetedTile))
			{
				addToAnimationStage(EAnimationEvents::MOVEMENT, [this, casterStack]()
				{
					stacksController->addNewAnim(new ReverseAnimation(*this, casterStack, casterStack->getPosition()));
				});
			}

			addToAnimationStage(EAnimationEvents::BEFORE_HIT, [this, casterStack, targetedTile, spell]()
			{
				stacksController->addNewAnim(new CastAnimation(*this, casterStack, targetedTile, getBattle()->battleGetStackByPos(targetedTile), spell));
				displaySpellCast(spell, casterStack->getPosition());
			});
		}
		else
		{
			auto hero = sc->side == BattleSide::DEFENDER ? defendingHero : attackingHero;
			assert(hero);

			addToAnimationStage(EAnimationEvents::BEFORE_HIT, [this, hero, targetedTile, spell]()
			{
				stacksController->addNewAnim(new HeroCastAnimation(*this, hero, targetedTile, getBattle()->battleGetStackByPos(targetedTile), spell));
			});
		}
	}

	addToAnimationStage(EAnimationEvents::HIT, [this, spell, targetedTile](){
		displaySpellHit(spell, targetedTile);
	});

	const bool usesChainRay = !spell->animationInfo.ray.empty() && !sc->affectedCres.empty();

	//queuing affect animation
	if(usesChainRay)
	{
		// only the primary (first) target gets the full affect; the rest get the trailing spark frames
		const auto & affect = spell->animationInfo.affect;
		SpellAnimationQueue sparks(affect.begin() + (affect.empty() ? 0 : 1), affect.end());

		std::vector<Point> targetPoints;
		for(size_t i = 0; i < sc->affectedCres.size(); ++i)
		{
			auto stack = getBattle()->battleGetStackByID(sc->affectedCres[i], false);
			if(!stack)
				continue;

			BattleHex hex = stack->getPosition();
			if(i == 0)
				addToAnimationStage(EAnimationEvents::HIT, [this, spell, hex](){ displaySpellEffect(spell, hex); });
			else
				addToAnimationStage(EAnimationEvents::HIT, [this, spell, sparks, hex](){ displaySpellAnimationQueue(spell, sparks, hex, false); });

			Point directionOffset(30, 0);
			targetPoints.push_back(stacksController->getStackPositionAtHex(hex, stack) + Point(225, 225) + (stacksController->facingRight(stack) ? -directionOffset : directionOffset));
		}

		const CStack * casterStack = getBattle()->battleGetStackByID(sc->casterStack);
		addToAnimationStage(EAnimationEvents::HIT, [this, casterStack, targetPoints, spell](){
			stacksController->addNewAnim(new ChainLightningAnimation(*this, casterStack, targetPoints, spell));
		});
	}
	else
	{
		size_t affectedIndex = 0;
		for(auto & elem : sc->affectedCres)
		{
			auto stack = getBattle()->battleGetStackByID(elem, false);
			assert(stack);
			if(stack)
			{
				// secondary affected targets (e.g. the sacrificed unit) use a distinct effect if the spell defines one
				bool useSecondary = affectedIndex > 0 && !spell->animationInfo.affectSecondary.empty();
				addToAnimationStage(EAnimationEvents::HIT, [this, stack, spell, useSecondary](){
					if(useSecondary)
						displaySpellAnimationQueue(spell, spell->animationInfo.affectSecondary, stack->getPosition(), false);
					else
						displaySpellEffect(spell, stack->getPosition());
				});
			}
			++affectedIndex;
		}
	}

	for(auto & elem : sc->reflectedCres)
	{
		auto stack = getBattle()->battleGetStackByID(elem, false);
		assert(stack);
		addToAnimationStage(EAnimationEvents::HIT, [this, stack](){
			effectsController->displayEffect(EBattleEffect::MAGIC_MIRROR, stack->getPosition());
		});
	}

	if (!sc->resistedCres.empty())
	{
		addToAnimationStage(EAnimationEvents::HIT, [](){
			ENGINE->sound().playSound(AudioPath::builtin("MAGICRES"));
		});
	}

	for(auto & elem : sc->resistedCres)
	{
		auto stack = getBattle()->battleGetStackByID(elem, false);
		assert(stack);
		addToAnimationStage(EAnimationEvents::HIT, [this, stack](){
			effectsController->displayEffect(EBattleEffect::RESISTANCE, stack->getPosition());
		});
	}

	//mana absorption
	if (sc->manaGained > 0)
	{
		Point leftHero = Point(15, 30);
		Point rightHero = Point(755, 30);
		BattleSide side = sc->side;

		addToAnimationStage(EAnimationEvents::AFTER_HIT, [this, side, leftHero, rightHero](){
			stacksController->addNewAnim(new EffectAnimation(*this, AnimationPath::builtin(side == BattleSide::DEFENDER ? "SP07_A.DEF" : "SP07_B.DEF"), leftHero));
			stacksController->addNewAnim(new EffectAnimation(*this, AnimationPath::builtin(side == BattleSide::DEFENDER ? "SP07_B.DEF" : "SP07_A.DEF"), rightHero));
		});
	}

	// animations will be executed by spell effects
}

void BattleInterface::battleStacksEffectsSet(const SetStackEffect & sse)
{
	if(stacksController->getActiveStack() != nullptr)
		fieldController->redrawBackgroundWithHexes();
}

void BattleInterface::setHeroAnimation(BattleSide side, EHeroAnimType phase)
{
	if(side == BattleSide::ATTACKER)
	{
		if(attackingHero)
			attackingHero->setPhase(phase);
	}
	else
	{
		if(defendingHero)
			defendingHero->setPhase(phase);
	}
}

void BattleInterface::displayBattleLog(const std::vector<MetaString> & battleLog)
{
	for(const auto & line : battleLog)
	{
		std::string formatted = line.toString(&GAME->translator());
		boost::algorithm::trim(formatted);
		appendBattleLog(formatted);
	}
}

void BattleInterface::displaySpellAnimationQueue(const CSpell * spell, const SpellAnimationQueue & q, const BattleHex & destinationTile, bool isHit)
{
	for(const auto & animation : q)
	{
		if(animation.pause > 0)
			stacksController->addNewAnim(new DummyAnimation(*this, animation.pause));

		if (!animation.effectName.empty())
		{
			const CStack * destStack = getBattle()->battleGetStackByPos(destinationTile, false);

			if (destStack)
				stacksController->addNewAnim(new ColorTransformAnimation(*this, destStack, animation.effectName, spell ));
		}

		if(!animation.resourceName.empty())
		{
			int flags = 0;

			if (isHit)
				flags |= EffectAnimation::FORCE_ON_TOP;

			if (animation.verticalPosition == VerticalPosition::BOTTOM)
				flags |= EffectAnimation::ALIGN_TO_BOTTOM;

			if (!destinationTile.isValid())
				flags |= EffectAnimation::SCREEN_FILL;

			if (!destinationTile.isValid())
				stacksController->addNewAnim(new EffectAnimation(*this, animation.resourceName, flags, animation.transparency));
			else
				stacksController->addNewAnim(new EffectAnimation(*this, animation.resourceName, destinationTile, flags, animation.transparency));
		}
	}
}

void BattleInterface::displaySpellCast(const CSpell * spell, const BattleHex & destinationTile)
{
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.cast, destinationTile, false);
}

void BattleInterface::displaySpellEffect(const CSpell * spell, const BattleHex & destinationTile)
{
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.affect, destinationTile, false);
}

void BattleInterface::displaySpellHit(const CSpell * spell, const BattleHex & destinationTile)
{
	if(spell)
		displaySpellAnimationQueue(spell, spell->animationInfo.hit, destinationTile, true);
}

CPlayerInterface *BattleInterface::getCurrentPlayerInterface() const
{
	return curInt.get();
}

void BattleInterface::trySetActivePlayer( PlayerColor player )
{
	if ( attackerInt && attackerInt->playerID == player )
		curInt = attackerInt;
	else if ( defenderInt && defenderInt->playerID == player )
		curInt = defenderInt;
}

void BattleInterface::activateStack()
{
	stacksController->activateStack();

	const CStack * s = stacksController->getActiveStack();
	if(!s)
		return;

	windowObject->updateQueue();
	windowObject->blockUI(false);
	fieldController->redrawBackgroundWithHexes();
	actionsController->activateStack();
	ENGINE->fakeMouseMove();
}

bool BattleInterface::makingTurn() const
{
	return stacksController->getActiveStack() != nullptr;
}

BattleID BattleInterface::getBattleID() const
{
	return battleID;
}

std::shared_ptr<CPlayerBattleCallback> BattleInterface::getBattle() const
{
	return curInt->cb->getBattle(battleID);
}

void BattleInterface::endAction(const BattleAction &action)
{
	// deferred spell hit reactions (e.g. chain lightning) are left undriven; start them so the wait below completes them
	if(!awaitingEvents.empty() && !hasAnimations())
		executeStagedAnimations();

	// it is possible that tactics mode ended while opening music is still playing
	waitForAnimations();

	const CStack * stack = action.isUnitAction() ? getBattle()->battleGetStackByID(action.stackNumber) : nullptr;

	// Activate stack from stackToActivate because this might have been temporary disabled, e.g., during spell cast
	activateStack();

	stacksController->endAction(action);
	windowObject->updateQueue();
	// StartAction is delivered before its authoritative state packet is applied.
	// Refresh after EndAction instead, so HERO_COMMAND can no longer leave a stale
	// armed-ward indicator behind after the server clears the ward.
	if(windowObject)
		windowObject->updateCounterspellStatus();

	//stack ended movement in tactics phase -> select the next one
	if (isInTacticsMode())
		tacticNextStack(stack);

	//we have activated next stack after sending request that has been just realized -> blockmap due to movement has changed
	if(action.actionType == EActionType::HERO_SPELL || action.actionType == EActionType::HERO_COMMAND)
		fieldController->redrawBackgroundWithHexes();

	if(action.actionType == EActionType::HERO_COMMAND)
		appendBattleLog("Order: " + HeroCommandUI::name(action.command) + " (this round).");
}

void BattleInterface::appendBattleLog(const std::string & newEntry)
{
	console->addText(newEntry);
}

void BattleInterface::startAction(const BattleAction & action)
{
	if(action.actionType == EActionType::END_TACTIC_PHASE)
	{
		windowObject->tacticPhaseEnded();
		return;
	}

	stacksController->startAction(action);

	if (!action.isUnitAction())
		return;

	assert(getBattle()->battleGetStackByID(action.stackNumber));
	windowObject->updateQueue();
	effectsController->startAction(action);
}

void BattleInterface::tacticPhaseEnd()
{
	stacksController->setActiveStack(nullptr);

	auto side = tacticianInterface->cb->getBattle(battleID)->playerToSide(tacticianInterface->playerID);
	auto action = BattleAction::makeEndOFTacticPhase(side);

	tacticianInterface->cb->battleMakeTacticAction(battleID, action);
}

static bool immobile(const CStack *s)
{
	return s->getMovementRange() == 0; //should bound stacks be immobile?
}

void BattleInterface::tacticNextStack(const CStack * current)
{
	if (!current)
		current = stacksController->getActiveStack();

	//no switching stacks when the current one is moving
	checkForAnimations();

	TStacks stacksOfMine = tacticianInterface->cb->getBattle(battleID)->battleGetStacks(CPlayerBattleCallback::ONLY_MINE);
	vstd::erase_if (stacksOfMine, &immobile);
	if (stacksOfMine.empty())
	{
		tacticPhaseEnd();
		return;
	}

	auto it = vstd::find(stacksOfMine, current);
	if (it != stacksOfMine.end() && ++it != stacksOfMine.end())
		stackActivated(*it);
	else
		stackActivated(stacksOfMine.front());

}

void BattleInterface::obstaclePlaced(const std::shared_ptr<const CObstacleInstance> & oi)
{
	// if a spell cast is mid-flight, show the obstacle after the caster's animation reaches its climax (HIT stage)
	if(!awaitingEvents.empty())
		addToAnimationStage(EAnimationEvents::HIT, [this, oi](){ obstacleController->obstaclePlaced(oi); });
	else
		obstacleController->obstaclePlaced(oi);
}

void BattleInterface::obstacleRemoved(const ObstacleChanges & obstacle)
{
	obstacleController->obstacleRemoved(obstacle);
}

const CGHeroInstance *BattleInterface::currentHero() const
{
	if (attackingHeroInstance && attackingHeroInstance->tempOwner == curInt->playerID)
		return attackingHeroInstance;

	if (defendingHeroInstance && defendingHeroInstance->tempOwner == curInt->playerID)
		return defendingHeroInstance;

	return nullptr;
}

InfoAboutHero BattleInterface::enemyHero() const
{
	InfoAboutHero ret;
	if (attackingHeroInstance->tempOwner == curInt->playerID)
		curInt->cb->getHeroInfo(defendingHeroInstance, ret);
	else
		curInt->cb->getHeroInfo(attackingHeroInstance, ret);

	return ret;
}

void BattleInterface::requestAutofightingAIToTakeAction()
{
	assert(curInt->isAutoFightOn);

	if(getBattle()->battleIsFinished())
	{
		return; // battle finished with spellcast
	}

	auto tacticsDist = curInt->cb->getBattle(battleID)->battleGetTacticDist();

	if (tacticsDist > 0)
	{
		stacksController->setActiveStack(nullptr);
		std::thread aiThread([localBattleID = battleID, localCurInt = curInt, tacticsDist]()
		{
			setThreadName("autofightingAI");
			localCurInt->autofightingAI->yourTacticPhase(localBattleID, tacticsDist);
		});
		aiThread.detach();
	}
	else
	{
		const CStack* activeStack = stacksController->getActiveStack();

		// If enemy is moving, activeStack can be null
		if (activeStack)
		{
			stacksController->setActiveStack(nullptr);

			// FIXME: unsafe
			// Run task in separate thread to avoid UI lock while AI is making turn (which might take some time)
			// HOWEVER this thread won't atttempt to lock game state, potentially leading to races
			std::thread aiThread([localBattleID = battleID, localCurInt = curInt, activeStack]()
			{
				setThreadName("autofightingAI");
				localCurInt->autofightingAI->activeStack(localBattleID, activeStack);
			});
			aiThread.detach();
		}
	}
}

void BattleInterface::castThisSpell(SpellID spellID)
{
	actionsController->castThisSpell(spellID);
}

void BattleInterface::endNetwork()
{
	ongoingAnimationsState.requestTermination();
}

void BattleInterface::executeStagedAnimations()
{
	EAnimationEvents earliestStage = EAnimationEvents::COUNT;

	for(const auto & event : awaitingEvents)
		earliestStage = std::min(earliestStage, event.event);

	if(earliestStage != EAnimationEvents::COUNT)
		executeAnimationStage(earliestStage);
}

void BattleInterface::executeAnimationStage(EAnimationEvents event)
{
	decltype(awaitingEvents) executingEvents;

	for(auto it = awaitingEvents.begin(); it != awaitingEvents.end();)
	{
		if(it->event == event)
		{
			executingEvents.push_back(*it);
			it = awaitingEvents.erase(it);
		}
		else
			++it;
	}
	for(const auto & event : executingEvents)
		event.action();
}

void BattleInterface::onAnimationsStarted()
{
	ongoingAnimationsState.setBusy();
}

void BattleInterface::onAnimationsFinished()
{
	ongoingAnimationsState.setFree();
}

void BattleInterface::waitForAnimations()
{
	{
		auto unlockInterface = vstd::makeUnlockGuard(ENGINE->interfaceMutex);
		ongoingAnimationsState.waitWhileBusy();
	}

	assert(!hasAnimations());
	assert(awaitingEvents.empty());

	if (!awaitingEvents.empty())
	{
		logGlobal->error("Wait for animations finished but we still have awaiting events!");
		awaitingEvents.clear();
	}
}

bool BattleInterface::hasAnimations()
{
	return ongoingAnimationsState.isBusy();
}

void BattleInterface::checkForAnimations()
{
	assert(!hasAnimations());
	if(hasAnimations())
		logGlobal->error("Unexpected animations state: expected all animations to be over, but some are still ongoing!");

	waitForAnimations();
}

void BattleInterface::addToAnimationStage(EAnimationEvents event, const AwaitingAnimationAction & action)
{
	awaitingEvents.push_back({action, event});
}

bool BattleInterface::hasQueuedStage(EAnimationEvents event) const
{
	for(const auto & e : awaitingEvents)
		if(e.event == event)
			return true;
	return false;
}

void BattleInterface::setBattleQueueVisibility(bool visible)
{
	windowObject->hideQueue();
	if(visible)
		windowObject->showQueue();
}

void BattleInterface::setStickyHeroWindowsVisibility(bool visible)
{
	windowObject->hideStickyHeroWindows();
	if(visible)
		windowObject->showStickyHeroWindows();
}

void BattleInterface::setStickyQuickSpellWindowVisibility(bool visible)
{
	windowObject->hideStickyQuickSpellWindow();
	if(visible)
		windowObject->showStickyQuickSpellWindow();
}
