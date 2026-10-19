/* License: GNU General Public License v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CureFixture.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/StartInfo.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <iostream>

namespace cureFixture
{
namespace
{
std::mutex registryMutex;
std::weak_ptr<Context> registeredContext;
bool registered = false; // Rendezvous only; never owns gameplay or rearms a context.

std::shared_ptr<Context> context()
{
	std::lock_guard lock(registryMutex);
	return registeredContext.lock();
}

void validateWorld(const CGameState & game, const CBattleInfoCallback & battle)
{
	const auto * start = game.getInitialStartInfo();
	require(start && start->mode == EStartMode::NEW_GAME && start->difficulty == 1
		&& ResourcePath(start->mapname, EResType::MAP) == ResourcePath(
			"Maps/ordinary48-normal-garrison-ice200-three-human-neutral-owner.vmap", EResType::MAP),
		"Fixture requires fixed ordinary Normal new game");
	require(start->playerInfos.size() == 2 && start->playerInfos.contains(PlayerColor(0))
		&& start->playerInfos.contains(PlayerColor(1))
		&& start->playerInfos.at(PlayerColor(0)).isControlledByHuman()
		&& start->playerInfos.at(PlayerColor(1)).isControlledByAI(), "Fixture requires red-human blue-AI");
	const auto & map = game.getMap();
	require(map.width == 36 && map.height == 36 && map.levels() == 1 && !map.battleOnly
		&& map.difficulty == EMapDifficulty::NORMAL, "Fixture map/header changed");
	const auto * hero = battle.battleGetFightingHero(BattleSide::ATTACKER);
	const auto * enemy = battle.battleGetArmyObject(BattleSide::DEFENDER);
	require(hero && enemy && battle.battleGetArmyObject(BattleSide::ATTACKER) == hero
		&& !battle.battleGetFightingHero(BattleSide::DEFENDER)
		&& hero->instanceName == "hero_0" && hero->getOwner() == PlayerColor(0)
		&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:tyris"))
		&& enemy->instanceName == "ordinary48_normal_garrison_ice200"
		&& enemy->getOwner() == PlayerColor::NEUTRAL, "Fixture hero/garrison identity changed");
	const SpellID cure(SpellID::CURE);
	const auto & rules = battle.getBattle()->getMagicRules();
	require(hero->hasSpellbook() && hero->isSpellInscribedForCasting(cure)
		&& hero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic"))) > 0
		&& newHorizonsMagic::hasSchoolProficiency(hero, cure)
		&& newHorizonsMagic::spellAllowedByBattleRoster(battle, cure)
		&& battle.battleGetSpellCost(cure.toSpell(), hero) == 4 && hero->getManaAvailable() >= 4
		&& battle.battleGetSpellActionAllowance(BattleSide::ATTACKER, cure).has_value()
		&& battle.battleCanCastSpell(hero, spells::Mode::HERO) == ESpellCastProblem::OK
		&& battle.getBattle()->getCastSpells(BattleSide::ATTACKER) == 0
		&& !battle.getBattle()->getHeroCommandUsed(BattleSide::ATTACKER)
		&& newHorizonsFrozen::chancePercent(rules) == 20, "Fixture requires ordinary unused real Cure access");
}

bool untouched(const CStack & stack, int count)
{
	return stack.alive() && !stack.isGhost() && !stack.isClone() && !stack.summoned
		&& !stack.isTimeStopped() && !stack.moved() && !stack.defended()
		&& stack.getCount() == count && stack.getKilled() == 0
		&& stack.getFirstHPleft() == stack.getMaxHealth()
		&& stack.getAvailableHealth() == static_cast<int64_t>(count) * stack.getMaxHealth()
		&& stack.frozenLastAppliedRound() == -1
		&& !stack.hasBonusOfType(BonusType::PHYSICAL_AFFLICTION)
		&& !stack.hasBonus(Selector::sourceType()(BonusSource::SPELL_EFFECT));
}

uint32_t validateArmies(const CBattleInfoCallback & battle, const CStack & next)
{
	const auto stacks = battle.battleGetAllStacks();
	require(stacks.size() == 4 && battle.battleGetRound() == 1
		&& next.unitSide() == BattleSide::ATTACKER && next.slot == SlotID(1)
		&& next.creatureId() == CreatureID(CreatureID::decode("core:waterElemental"))
		&& next.getInitiative() == 9, "Fixture requires first normal slot1 Water activation in round1");
	std::array<bool, 4> found{};
	uint32_t target = 0;
	for(const auto * stack : stacks)
	{
		require(stack != nullptr, "Missing fixture unit");
		if(stack->unitSide() == BattleSide::DEFENDER)
		{
			require(!found[3] && stack->creatureId() == CreatureID(CreatureID::decode("core:iceElemental"))
				&& untouched(*stack, 200) && stack->waited() && stack->getInitiative() == 10,
				"Fixture requires actual undamaged Ice200 WAIT before human control");
			found[3] = true;
			continue;
		}
		const int slot = stack->slot.getNum();
		require(slot >= 0 && slot < 3 && !found[slot] && !stack->waited()
			&& untouched(*stack, slot == 0 ? 300 : 200), "Friendly fixture activation/health changed");
		const auto expected = slot == 0 ? "core:crusader" : "core:waterElemental";
		require(stack->creatureId() == CreatureID(CreatureID::decode(expected))
			&& stack->getInitiative() == (slot == 0 ? 6 : 9), "Friendly fixture creature/initiative changed");
		found[slot] = true;
		if(slot == 0)
			target = stack->unitId();
	}
	require(std::all_of(found.begin(), found.end(), [](bool value) { return value; }), "Incomplete fixture army");
	return target;
}
}

void registerContext(const std::shared_ptr<Context> & value)
{
	std::lock_guard lock(registryMutex);
	require(value && !registered, "Fixture context may be attached only once per process");
	registered = true;
	registeredContext = value;
}

const CStack * beforeHumanActivation(CGameHandler & handler, const CBattleInfoCallback & battle, const CStack & next)
{
	const auto owner = context();
	const auto battleID = battle.getBattle()->getBattleID();
	if(!owner || !owner->pendingFor(battleID.getNum(), next.unitSide() == BattleSide::ATTACKER))
		return &next;
	// Context is consumed now. Every failure below is terminal for this setup.
	const auto * initialBattle = handler.gameState().getBattle(battleID);
	require(initialBattle && initialBattle->getBattle() == battle.getBattle(), "Fixture owner not current");
	validateWorld(handler.gameState(), battle);
	const auto targetID = validateArmies(battle, next);
	const auto activeID = next.unitId();
	const auto * target = battle.battleGetStackByID(targetID);
	require(target && newHorizonsMagic::cureAfflictions(battle.getBattle()->getMagicRules(), target).empty(),
		"Fixture target has another Cure affliction");
	SetStackEffect effect;
	effect.battleID = battleID;
	effect.toAdd.emplace_back(targetID, std::vector<Bonus>{newHorizonsFrozen::marker(
		BonusSourceID(CreatureID(CreatureID::decode("core:iceElemental"))), 1)});
	handler.sendAndApply(effect); // Existing authoritative validation, stamp and replica publication.
	const auto * current = handler.gameState().getBattle(battleID);
	require(current == initialBattle, "Fixture battle retired during publication");
	target = current->battleGetStackByID(targetID);
	const auto * active = current->battleGetStackByID(activeID);
	require(target && active && target->alive() && target->getCount() == 300
		&& target->getAvailableHealth() == static_cast<int64_t>(300) * target->getMaxHealth()
		&& newHorizonsFrozen::isFrozen(*target) && target->frozenLastAppliedRound() == 1
		&& newHorizonsMagic::cureAfflictions(current->getMagicRules(), target).empty()
		&& active->slot == SlotID(1) && active->unitSide() == BattleSide::ATTACKER && untouched(*active, 200),
		"Fixture authoritative Frozen/fullHP/receipt or selected Water changed");
	owner->published(battleID.getNum(), targetID, 1);
	std::cout << "NH_CURE_FROZEN_FIXTURE_TEST_ONLY_V1 authoritative PASS: battle=" << battleID.getNum()
		<< " round=1 target=" << targetID << " active=" << activeID
		<< " Frozen-only Crusader300 fullHP; no natural proc claim" << std::endl;
	return active; // Reacquired after publication, never the pre-publication pointer.
}
}
