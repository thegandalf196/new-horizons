/*
 * HeroQueueSelectorFixture.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueSelectorFixture.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/spells/CSpell.h"

#include <algorithm>
#include <sstream>

namespace heroQueueTest
{
const Services * QueueEnvironment::services() const { return LIBRARY; }
const Environment::BattleCb * QueueEnvironment::battle(const BattleID & id) const { return state->getBattle(id); }
const Environment::GameCb * QueueEnvironment::game() const { return state.get(); }

void HeroQueueSelectorTest::mapLoaded(CMap * map)
{
	HeroCommandFixture::mapLoaded(map);
	for(const auto & [key, path] : std::vector<std::pair<EGameSettings, std::string>>{
		{EGameSettings::MAGIC_NEW_HORIZONS, "config/newHorizonsMagic"},
		{EGameSettings::HEROES_NEW_HORIZONS, "config/newHorizonsHeroes"},
		{EGameSettings::HEROES_NEW_HORIZONS_PERKS, "config/newHorizonsPerks"},
		{EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, "config/newHorizonsCapabilities"}})
		map->overrideGameSetting(key, JsonNode(JsonPath::builtin(path)));
}

void HeroQueueSelectorTest::world()
{
	ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(seed);
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(book(attackerSideHero, SpellID::CURE, "new-horizons:lightMagic"));
}

void HeroQueueSelectorTest::selectPerk(CGHeroInstance * hero, const std::string & skill,
	const std::string & perk, MasteryLevel::Type rank)
{
	const auto id = SecondarySkill(SecondarySkill::decode(skill));
	ASSERT_TRUE(id.hasValue());
	hero->setSecSkillLevel(id, rank, ChangeValueMode::ABSOLUTE);
	const auto lookup = [hero](const std::string & name) { return hero->getPerkSkillRank(name); };
	for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
	{
		const auto offers = hero->getPerkState().prepareOffer(lookup, offerSeed, hero->getPerkOfferWeightContext());
		for(size_t index = 0; index < offers.size(); ++index)
			if(offers[index].selection.skillId == skill && offers[index].selection.perkId == perk)
			{
				gameHandler->levelUpHero(hero, offers, index, offerSeed, false);
				ASSERT_TRUE(hero->hasActivePerk(skill, perk));
				return;
			}
	}
	FAIL() << "No legal canonical offer for " << perk;
}

void HeroQueueSelectorTest::book(CGHeroInstance * hero, SpellID spell, const std::string & skill)
{
	giveArtifact(hero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	hero->removeAllSpells();
	hero->addSpellToSpellbook(spell);
	const auto id = SecondarySkill(SecondarySkill::decode(skill));
	ASSERT_TRUE(id.hasValue());
	hero->setSecSkillLevel(id, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->setPrimarySkill(PrimarySkill::SPELL_POWER, 100, ChangeValueMode::ABSOLUTE);
	setTestSpellPointTotal(hero, 1000);
}

void HeroQueueSelectorTest::arena()
{
	ASSERT_NO_FATAL_FAILURE(startBattle());
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(1, 5), 1);
	actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(7, 5), 100);
	waiter = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 100);
	enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 100);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(waiter, nullptr);
	ASSERT_NE(enemy, nullptr);
	int initiative = 1000;
	for(const auto * unit : {active, actor, waiter, enemy})
	{
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_INITIATIVE_BASE,
			BonusSource::OTHER, initiative, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER));
		effect(unit, Bonus(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE, BonusSource::OTHER, 1, BonusSourceID()));
		initiative -= 200;
	}
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	ASSERT_FALSE(active->isShooter());
	ASSERT_LT(active->getMovementRange() + 1, BattleHex::getDistance(active->getPosition(), enemy->getPosition()));
}

void HeroQueueSelectorTest::activate(const CStack * unit)
{
	BattleSetActiveStack packet;
	packet.battleID = BattleID(0);
	packet.stack = unit->unitId();
	packet.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(packet);
	ASSERT_EQ(battle()->battleActiveUnit(), unit);
}

void HeroQueueSelectorTest::effect(const CStack * unit, const Bonus & bonus)
{
	SetStackEffect packet;
	packet.battleID = BattleID(0);
	packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{bonus});
	gameHandler->sendAndApply(packet);
}

void HeroQueueSelectorTest::freeze(const CStack * unit)
{
	effect(unit, newHorizonsFrozen::makeFrozenMarker(
		BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound()));
	ASSERT_TRUE(newHorizonsFrozen::forfeitsNormalActivation(*unit, BattleUnitTurnReason::TURN_QUEUE));
}

void HeroQueueSelectorTest::injure(CStack * unit, int64_t remainingHealth)
{
	auto value = unit->acquireState();
	ASSERT_GT(remainingHealth, 0);
	ASSERT_LT(remainingHealth, value->getAvailableHealth());
	int64_t damage = value->getAvailableHealth() - remainingHealth;
	value->damage(damage);
	UnitChanges change(unit->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = value->save();
	change.healthDelta = -damage;
	BattleUnitsChanged packet;
	packet.battleID = BattleID(0);
	packet.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(packet);
}

void HeroQueueSelectorTest::spendEnemyAllowance()
{
	ASSERT_NO_FATAL_FAILURE(activate(enemy));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeHeroCommand(BattleSide::DEFENDER, HeroCommand::CHARGE)));
	ASSERT_FALSE(battle()->getHeroActionAllowances(BattleSide::DEFENDER).eligibleAllowance(
		HeroActionAllowanceState::ActionKind::SPELL, battle()->getRound()));
	ASSERT_NO_FATAL_FAILURE(activate(active));
}

bool HeroQueueSelectorTest::cast(CGHeroInstance * hero, SpellID spell, const CStack * target)
{
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = battle()->playerToSide(hero->getOwner());
	action.spell = spell;
	action.aimToUnit(target);
	return gameHandler->battles->makePlayerBattleAction(BattleID(0), hero->getOwner(), action);
}

std::vector<std::vector<uint32_t>> HeroQueueSelectorTest::queue() const
{
	std::vector<battle::Units> turns;
	battle()->battleGetTurnOrder(turns, battle()->battleGetAllStacks(false).size(), 2);
	std::vector<std::vector<uint32_t>> result;
	for(const auto & round : turns)
	{
		result.emplace_back();
		for(const auto * unit : round)
			result.back().push_back(unit->unitId());
	}
	return result;
}

void HeroQueueSelectorTest::expectBeforeEnemy(const CStack * unit)
{
	const auto turns = queue();
	ASSERT_FALSE(turns.empty());
	const auto found = std::ranges::find(turns.front(), unit->unitId());
	const auto stop = std::ranges::find(turns.front(), enemy->unitId());
	ASSERT_NE(found, turns.front().end());
	ASSERT_NE(stop, turns.front().end());
	ASSERT_LT(found, stop);
}

std::string HeroQueueSelectorTest::state(const CBattleInfoCallback & view) const
{
	std::ostringstream out;
	auto units = view.battleGetAllUnits(false);
	std::ranges::sort(units, {}, &battle::Unit::unitId);
	for(const auto * unit : units)
	{
		out << unit->unitId() << ':' << unit->acquireState()->save().toString();
		const auto bonuses = bonusSnapshots(*unit->getAllBonuses(CSelector([](const Bonus *) { return true; })));
		out << wire(bonuses);
	}
	const auto * value = view.getBattle();
	out << value->getRound() << ':' << value->getActiveStackID() << ':' << wire(value->getSeizeInitiativeState());
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		out << wire(value->getHeroActionAllowances(side)) << wire(value->getHeroOrderStates(side))
			<< wire(value->getRapidResponseState(side)) << value->getCastSpells(side);
	return out.str();
}

std::string HeroQueueSelectorTest::observation()
{
	JsonNode result;
	result["live"].String() = state(*live);
	result["parent"].String() = state(*parent);
	result["sibling"].String() = state(*sibling);
	result["points"].String() = wire(std::vector<int64_t>{attackerSideHero->getManaAvailable(),
		attackerSideHero->getNormalSpellPoints(), attackerSideHero->getBufferSpellPoints(),
		defenderSideHero->getManaAvailable(), defenderSideHero->getNormalSpellPoints(), defenderSideHero->getBufferSpellPoints()});
	result["authorityRng"].String() = wire(*gameHandler->randomizer);
	result["fixtureRng"].String() = wire(dynamic_cast<CRandomGenerator &>(*getRNG()));
	result["traffic"].String() = wire(std::vector<size_t>{server.startedActions.size(), server.stackActivations.size(),
			server.orderStateUpdates.size(), server.attacks.size(), server.injuries.size(), server.battleLogLines.size(),
			server.casts.size(), server.battleAnimations.size(), server.unitAdditionRounds.size(), server.progressionPackets.size()});
	return result.toString();
}

void HeroQueueSelectorTest::characterize(bool requireSubmittedRequest)
{
	ASSERT_EQ(battle()->battleActiveUnit(), active);
	callback = std::make_shared<QueueCallback>();
	callback->onBattleStarted(battle());
	live = callback->getBattle(BattleID(0));
	ASSERT_FALSE(live->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	ASSERT_FALSE(live->battleHasPendingPreCombatOrder(BattleSide::ATTACKER));
	ASSERT_FALSE(live->battleHasPendingPreCombatOrder(BattleSide::DEFENDER));
	ASSERT_FALSE(battle()->getCrisisCommandState().choice());
	RecordProperty("coldLive", state(*live));
	for(const auto * unit : live->battleGetAllUnits(false))
		(void)unit->ableToRetaliate();
	RecordProperty("warmLive", state(*live));
	environment = std::make_shared<QueueEnvironment>(gameState());
	parent = std::make_shared<HypotheticBattle>(environment.get(), live);
	child = std::make_shared<HypotheticBattle>(environment.get(), parent);
	sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
	RecordProperty("publicQueue", wire(queue()));
	const auto before = observation();
	DamageCache cache;
	cache.buildDamageCache(child, BattleSide::ATTACKER);
	BattleEvaluator evaluator(environment, callback, child, cache, active, PlayerColor(0), BattleID(0), BattleSide::ATTACKER, 1.0f, 2);
	const auto ordinary = evaluator.selectStackAction(active);
	ASSERT_TRUE(evaluator.canCastSpell());
	RecordProperty("canCastSpell", "1");
	// These are the exact queue-call guards, immediately before the real selector.
	JsonNode guards;
	guards["mandatoryDoubleCommand"].Bool() = live->battleHasPendingDoubleCommand(BattleSide::ATTACKER);
	guards["mandatoryPreCombatOrder"].Bool() = live->battleHasPendingPreCombatOrder(BattleSide::ATTACKER);
	guards["crisisChoice"].Bool() = live->getBattle()->getCrisisCommandState().choice();
	guards["attackerUnresolvedPreCombat"].Bool() = live->getBattle()->getPreCombatOrderState(BattleSide::ATTACKER).isUnresolved();
	guards["defenderUnresolvedPreCombat"].Bool() = live->getBattle()->getPreCombatOrderState(BattleSide::DEFENDER).isUnresolved();
	bool queueGuardsClear = true;
	for(const auto & [name, flag] : guards.Struct())
	{
		(void)name;
		queueGuardsClear = queueGuardsClear && !flag.Bool();
	}
	EXPECT_TRUE(queueGuardsClear);
	ASSERT_TRUE(callback->heroRequests.empty());
	ASSERT_TRUE(callback->unitRequests.empty());
	const bool submitted = evaluator.attemptCastingSpell(active, true);
	const auto after = observation();
	EXPECT_EQ(after, before);
	EXPECT_TRUE(callback->unitRequests.empty());
	EXPECT_EQ(callback->heroRequests.size(), submitted ? 1u : 0u);
	RecordProperty("before", before);
	RecordProperty("after", after);
	RecordProperty("ordinaryWire", wire(ordinary));
	RecordProperty("submitted", submitted ? "1" : "0");
	RecordProperty("requestWires", wire(callback->heroRequests));
	RecordProperty("unitRequestWires", wire(callback->unitRequests));
	RecordProperty("suppliedChildAfter", state(*child)); // Not the private no-cast queue state.
	const bool requestWitness = submitted && callback->heroRequests.size() == 1
		&& (callback->heroRequests.front().actionType == EActionType::HERO_COMMAND
			|| callback->heroRequests.front().actionType == EActionType::HERO_SPELL);
	RecordProperty("queueEntryGuards", guards.toString());
	RecordProperty("queueEntryWitnessRequired", requireSubmittedRequest ? "1" : "0");
	RecordProperty("queueEntryWitness", queueGuardsClear && requestWitness ? "submitted-request" : "none");
	if(requireSubmittedRequest)
	{
		EXPECT_TRUE(queueGuardsClear && requestWitness)
			<< "No queue-entry coverage: canCastSpell and publicQueue alone are insufficient";
	}
}
}
