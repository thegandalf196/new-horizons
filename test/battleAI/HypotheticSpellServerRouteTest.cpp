/*
 * HypotheticSpellServerRouteTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/texts/MetaString.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <typeinfo>

namespace
{
class RouteEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RouteEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

struct RouteMessages
{
	std::mutex mutex;
	std::vector<std::string> rows;
	bool overflow = false;
};

class RouteLogTarget final : public ILogTarget
{
	std::weak_ptr<RouteMessages> messages;
public:
	explicit RouteLogTarget(std::weak_ptr<RouteMessages> value) : messages(std::move(value)) {}
	void write(const LogRecord & record) override
	{
		if(record.domain.getName() != "ai" || record.level != ELogLevel::ERROR
			|| !record.message.starts_with("Package of type "))
			return;
		if(const auto target = messages.lock())
		{
			std::lock_guard lock(target->mutex);
			if(target->rows.size() < 16)
				target->rows.push_back(record.message);
			else
				target->overflow = true;
		}
	}
};

class ScopedRouteLogs final
{
	CLogger * global = CLogger::getGlobalLogger();
	CLogger * ai = CLogger::getLogger(CLoggerDomain("ai"));
	ELogLevel::ELogLevel oldGlobal = global->getLevel();
	ELogLevel::ELogLevel oldAi = ai->getLevel();
	std::shared_ptr<RouteMessages> messages = std::make_shared<RouteMessages>();
public:
	ScopedRouteLogs()
	{
		global->setLevel(ELogLevel::ERROR);
		ai->setLevel(ELogLevel::ERROR);
		global->addTarget(std::make_unique<RouteLogTarget>(messages));
	}
	~ScopedRouteLogs()
	{
		ai->setLevel(oldAi);
		global->setLevel(oldGlobal);
		// No per-target removal API: expire only this bounded weak capture.
		messages.reset();
	}
	ScopedRouteLogs(const ScopedRouteLogs &) = delete;
	ScopedRouteLogs & operator=(const ScopedRouteLogs &) = delete;
	std::vector<std::string> rows() const
	{
		std::lock_guard lock(messages->mutex);
		EXPECT_FALSE(messages->overflow);
		return messages->rows;
	}
};

void expectError(const std::function<void()> & operation, const std::string & expected)
{
	try
	{
		operation();
		FAIL() << "Expected runtime_error: " << expected;
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_EQ(error.what(), expected);
	}
}

void requireSnapshotUpdater(const TUpdaterPtr & updater, unsigned depth = 0)
{
	if(!updater)
		return;
	if(depth >= 16)
		throw std::runtime_error("Unsupported snapshot updater graph");
	const auto & kind = typeid(*updater);
	if(kind == typeid(CompositeUpdater))
	{
		for(const auto & nested : static_cast<const CompositeUpdater &>(*updater).updaters)
			requireSnapshotUpdater(nested, depth + 1);
		return;
	}
	if(kind == typeid(IUpdater) || kind == typeid(GrowsWithLevelUpdater)
		|| kind == typeid(TimesHeroLevelUpdater) || kind == typeid(TimesArmySizeUpdater)
		|| kind == typeid(TimesStackLevelUpdater) || kind == typeid(DivideStackLevelUpdater)
		|| kind == typeid(TimesHeroLevelDivideStackLevelUpdater) || kind == typeid(OwnerUpdater))
		return;
	// In particular, TimesStackSizeUpdater omits stepValue from binary serialization.
	throw std::runtime_error("Unsupported snapshot updater graph");
}

void requireSnapshotParameters(const TBonusParametersPtr & parameters)
{
	if(!parameters)
		return;
	bool supported = false;
	try
	{
		(void)parameters->toCustom<BonusParametersOnCombatEvent>();
	}
	catch(const std::runtime_error & error)
	{
		for(const int index : {0, 1, 2, 3, 5})
			supported |= error.what() == std::string("Invalid addInfo type access! Stored type: ") + std::to_string(index);
	}
	// Refuse the whole event variant: nested Bonus runtime owners are not serialized.
	if(!supported)
		throw std::runtime_error("Unsupported snapshot combat-event parameters");
}

std::string bonusSnapshot(const Bonus & bonus)
{
	requireSnapshotUpdater(bonus.updater);
	requireSnapshotUpdater(bonus.propagationUpdater);
	requireSnapshotParameters(bonus.parameters);
	CMemorySerializer memory;
	memory.oser & bonus;
	memory.oser & bonus.bonusOwner; // Runtime aura owner is not part of Bonus::serialize.
	const auto bytes = memory.extractBuffer();
	constexpr char digits[] = "0123456789abcdef";
	std::string encoded;
	encoded.reserve(bytes.size() * 2);
	for(const auto byte : bytes)
	{
		const auto value = std::to_integer<unsigned int>(byte);
		encoded.push_back(digits[value >> 4]);
		encoded.push_back(digits[value & 0x0f]);
	}
	return encoded;
}

void expectSnapshotRefusals()
{
	for(const int step : {1, 2})
	{
		Bonus bonus;
		bonus.updater = std::make_shared<TimesStackSizeUpdater>(0, 100, 1, step);
		expectError([&] { bonusSnapshot(bonus); }, "Unsupported snapshot updater graph");
		auto composite = std::make_shared<CompositeUpdater>();
		composite->updaters.push_back(bonus.updater);
		bonus.updater = composite;
		expectError([&] { bonusSnapshot(bonus); }, "Unsupported snapshot updater graph");
		bonus.propagationUpdater = bonus.updater;
		bonus.updater.reset();
		expectError([&] { bonusSnapshot(bonus); }, "Unsupported snapshot updater graph");
	}
	for(const auto owner : {PlayerColor(0), PlayerColor(1)})
	{
		auto nested = std::make_shared<Bonus>();
		nested->bonusOwner = owner;
		BonusParametersOnCombatEvent event;
		event.effects.emplace_back(BonusParametersOnCombatEvent::CombatEffectBonus{nested, false});
		Bonus bonus;
		bonus.parameters = std::make_shared<BonusParameters>(event);
		expectError([&] { bonusSnapshot(bonus); }, "Unsupported snapshot combat-event parameters");
	}
	auto cyclic = std::make_shared<CompositeUpdater>();
	cyclic->updaters.push_back(cyclic);
	Bonus cycle;
	cycle.updater = cyclic;
	expectError([&] { bonusSnapshot(cycle); }, "Unsupported snapshot updater graph");
	cyclic->updaters.clear();
}

std::string viewState(const CBattleInfoCallback & callback)
{
	std::ostringstream out;
	auto units = callback.battleGetAllUnits(false);
	std::sort(units.begin(), units.end(), [](const auto * a, const auto * b) { return a->unitId() < b->unitId(); });
	for(const auto * unit : units)
	{
		out << "unit=" << unit->unitId() << ':' << unit->acquireState()->save().toString();
		const auto owner = unit->getAllBonuses(CSelector([](const Bonus *) { return true; }));
		std::vector<std::string> bonuses;
		for(const auto & bonus : *owner)
			bonuses.push_back(bonusSnapshot(*bonus));
		std::sort(bonuses.begin(), bonuses.end());
		for(const auto & bonus : bonuses)
			out << "bonus=" << bonus;
	}
	const auto * state = callback.getBattle();
	out << "round=" << state->getRound() << ";active=" << state->getActiveStackID();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & weave = state->getWarcastingState(side);
		out << ";side=" << static_cast<int>(side) << ',' << static_cast<int>(weave.nextEligibleAction)
			<< ',' << weave.empowermentPercent << ',' << weave.expiryRound << ',' << weave.reactiveEmpowermentPercent
			<< ',' << weave.reactiveExpiryRound << ',' << weave.lastManaRecoveryRound << ',' << weave.hasConsumedBonus;
		for(const auto action : weave.recentActions)
			out << ',' << static_cast<int>(action);
		const auto & formula = state->getOverwhelmingFormulaState(side);
		out << ";response=" << state->getSpellResponseState(side).armedInRound
			<< ";formula=" << formula.lastCandidateCastToken << ',' << formula.winningCastToken
			<< ";completed=" << state->hasCompletedHeroSpellCast(side);
	}
	for(int i = 0; i < static_cast<int>(EWallPart::PARTS_COUNT); ++i)
		out << ";wall=" << i << ',' << static_cast<int>(state->getWallState(static_cast<EWallPart>(i)))
			<< ',' << state->getWallStructuralHP(static_cast<EWallPart>(i));
	for(const auto & obstacle : state->getAllObstacles())
	{
		out << ";obstacle=" << obstacle->uniqueID << ',' << obstacle->pos.toInt();
		if(const auto spell = std::dynamic_pointer_cast<const SpellCreatedObstacle>(obstacle))
			out << ',' << spell->turnsRemaining << ',' << spell->revealed;
	}
	return out.str();
}
}

class HypotheticSpellServerRouteTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<RouteEnvironment> environment;
	std::shared_ptr<CPlayerBattleCallback> live;
	std::shared_ptr<HypotheticBattle> parent, child, sibling;
	uint32_t actor = 0, victim = 0;
	std::array<std::string, 3> protectedViews;
	std::vector<std::byte> randomizerBefore, fixtureRngBefore;
	std::vector<size_t> trafficBefore;
	std::array<int64_t, 2> manaBefore{};
	JsonNode observed;
	bool ready = false;

	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void choose(const std::string & skill, const std::string & perk)
	{
		const auto lookup = [this](const std::string & id) { return defenderSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = defenderSideHero->getPerkState().prepareOffer(lookup, seed, defenderSideHero->getPerkOfferWeightContext());
			const auto found = std::find_if(offers.begin(), offers.end(), [&](const auto & entry)
				{ return entry.selection.skillId == skill && entry.selection.perkId == perk; });
			if(found == offers.end())
				continue;
			gameHandler->levelUpHero(defenderSideHero, offers, std::distance(offers.begin(), found), seed, false);
			ASSERT_TRUE(defenderSideHero->hasActivePerk(skill, perk));
			return;
		}
		FAIL() << "No legal offer for " << perk;
	}

	std::vector<std::byte> randomizerState()
	{
		CMemorySerializer memory;
		gameHandler->randomizer->serialize(memory.oser);
		return memory.extractBuffer();
	}
	std::vector<std::byte> fixtureRngState()
	{
		CMemorySerializer memory;
		dynamic_cast<CRandomGenerator &>(*getRNG()).serialize(memory.oser);
		return memory.extractBuffer();
	}
	std::vector<size_t> traffic() const
	{
		return {server.startedActions.size(), server.stackActivations.size(), server.orderStateUpdates.size(),
			server.attacks.size(), server.injuries.size(), server.battleLogLines.size(), server.casts.size(),
			server.battleAnimations.size(), server.unitAdditionRounds.size(), server.progressionPackets.size()};
	}

	void prepare(bool reactions = false, bool siege = false, PlayerColor viewer = PlayerColor(0))
	{
		ASSERT_NO_FATAL_FAILURE(expectSnapshotRefusals());
		RecordProperty("snapshotRefusalControls", "direct=2;composite=2;propagation=2;nestedOwner=2;cycle=1");
		ASSERT_NO_FATAL_FAILURE(startGame(siege));
		if(reactions)
			for(const auto & keys : {std::array<std::string, 3>{"new-horizons:warcasting", "martialChanneling", "reactiveWeave"},
				std::array<std::string, 3>{"new-horizons:spellcraft", "spellPenetration", "counterpressure"}})
			{
				const auto skill = SecondarySkill(SecondarySkill::decode(keys[0]));
				ASSERT_TRUE(skill.hasValue());
				defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
				ASSERT_NO_FATAL_FAILURE(choose(keys[0], keys[0] + '.' + keys[1]));
				gameHandler->levelUpHero(defenderSideHero, skill, false);
				ASSERT_EQ(defenderSideHero->getSecSkillLevel(skill), MasteryLevel::ADVANCED);
				ASSERT_NO_FATAL_FAILURE(choose(keys[0], keys[0] + '.' + keys[2]));
			}
		if(siege)
		{
			const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
			ASSERT_EQ(towns.size(), 1u);
			ASSERT_NO_FATAL_FAILURE(startBattle(towns.front()));
		}
		else
			ASSERT_NO_FATAL_FAILURE(startBattle());
		auto * first = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
		auto * second = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
		ASSERT_NE(first, nullptr);
		ASSERT_NE(second, nullptr);
		actor = first->unitId();
		victim = second->unitId();
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		environment = std::make_shared<RouteEnvironment>(gameState());
		live = std::make_shared<CPlayerBattleCallback>(battle(), viewer);
		parent = std::make_shared<HypotheticBattle>(environment.get(), live);
		child = std::make_shared<HypotheticBattle>(environment.get(), parent);
		sibling = std::make_shared<HypotheticBattle>(environment.get(), parent);
		protectedViews = {viewState(*live), viewState(*parent), viewState(*sibling)};
		randomizerBefore = randomizerState();
		fixtureRngBefore = fixtureRngState();
		trafficBefore = traffic();
		manaBefore = {attackerSideHero->getManaAvailable(), defenderSideHero->getManaAvailable()};
		ready = true;
	}

	void record(const std::string & route)
	{
		EXPECT_EQ(viewState(*live), protectedViews[0]);
		EXPECT_EQ(viewState(*parent), protectedViews[1]);
		EXPECT_EQ(viewState(*sibling), protectedViews[2]);
		EXPECT_EQ(randomizerState(), randomizerBefore);
		EXPECT_EQ(fixtureRngState(), fixtureRngBefore);
		EXPECT_EQ(traffic(), trafficBefore);
		EXPECT_EQ(attackerSideHero->getManaAvailable(), manaBefore[0]);
		EXPECT_EQ(defenderSideHero->getManaAvailable(), manaBefore[1]);
		EXPECT_NE(child->getServerCallback()->getRNG(), getRNG());
		JsonNode row;
		row["route"].String() = route;
		row["child"].String() = viewState(*child);
		observed.Vector().push_back(std::move(row));
	}

	StacksInjured injury(int64_t damage = 7)
	{
		auto replacement = child->battleGetUnitByID(victim)->acquireState();
		replacement->damage(damage);
		StacksInjured packet;
		packet.battleID = BattleID(0);
		auto & hit = packet.stacks.emplace_back();
		hit.stackAttacked = victim;
		hit.attackerID = actor;
		hit.damageAmount = damage;
		hit.newState = UnitChanges(victim, UnitChanges::EOperation::UPDATE);
		hit.newState.data = replacement->save();
		hit.newState.healthDelta = -damage;
		return packet;
	}

	BattleObstaclesChanged obstacle(BattleChanges::EOperation operation = BattleChanges::EOperation::ADD)
	{
		SpellCreatedObstacle wall;
		wall.uniqueID = 700;
		wall.ID = SpellID::FORCE_FIELD;
		wall.trigger = SpellID::NONE;
		wall.pos = BattleHex(8, 7);
		wall.customSize.insert(wall.pos);
		wall.turnsRemaining = 3;
		wall.casterSide = BattleSide::ATTACKER;
		wall.revealed = operation == BattleChanges::EOperation::UPDATE;
		BattleObstaclesChanged packet;
		packet.battleID = BattleID(0);
		wall.toInfo(packet.change, operation);
		return packet;
	}

	void TearDown() override
	{
		if(ready)
		{
			record("final");
			RecordProperty("routeRows", observed.toString());
		}
		sibling.reset();
		child.reset();
		parent.reset();
		live.reset();
		environment.reset();
		HeroCommandFixture::TearDown();
	}
};

TEST_F(HypotheticSpellServerRouteTest, TypedLogMoveAndEffectUseTheirOverloads)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ScopedRouteLogs logs;
	auto * callback = child->getServerCallback();
	const auto initial = viewState(*child);
	BattleLogMessage log;
	log.battleID = BattleID(0);
	log.lines.push_back(MetaString::createFromRawString("detached route witness"));
	callback->apply(log);
	EXPECT_EQ(viewState(*child), initial); // BattleStatePackVisitor has no log mutation.
	EXPECT_TRUE(logs.rows().empty());
	record("typed-log-no-state");
	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = actor;
	move.tilesToMove.insert(BattleHex(6, 5));
	callback->apply(move);
	EXPECT_EQ(child->battleGetUnitByID(actor)->getPosition(), BattleHex(6, 5));
	record("typed-move-endpoint");
	move.tilesToMove.clear();
	const auto moved = viewState(*child);
	expectError([&] { callback->apply(move); }, "Movement has no endpoint");
	EXPECT_EQ(viewState(*child), moved);
	record("typed-empty-move-throws-without-state-change");
	const auto speed = child->battleGetUnitByID(actor)->valOfBonuses(BonusType::STACKS_SPEED);
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 3,
		BonusSourceID(SpellID(SpellID::HASTE)));
	effect.toAdd.emplace_back(actor, std::vector<Bonus>{bonus});
	callback->apply(effect);
	EXPECT_EQ(child->battleGetUnitByID(actor)->valOfBonuses(BonusType::STACKS_SPEED), speed + 3);
	record("typed-effect-add3");
}

TEST_F(HypotheticSpellServerRouteTest, TypedUnitsInjuryAndOrderedFailureRetainAppliedPrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * callback = child->getServerCallback();
	const auto health = child->battleGetUnitByID(victim)->getAvailableHealth();
	auto hit = injury();
	callback->apply(hit);
	EXPECT_EQ(child->battleGetUnitByID(victim)->getAvailableHealth(), health - 7);
	EXPECT_TRUE(child->getProjectedPlagueHits().empty());
	record("typed-injury-seven");
	BattleUnitsChanged updates;
	updates.battleID = BattleID(0);
	auto replacement = child->battleGetUnitByID(actor)->acquireState();
	replacement->defending = true;
	updates.changedStacks.emplace_back(actor, UnitChanges::EOperation::UPDATE);
	updates.changedStacks.back().data = replacement->save();
	EXPECT_NO_THROW(callback->apply(updates));
	EXPECT_TRUE(child->battleGetUnitByID(actor)->defended());
	record("typed-units-valid-update");
	replacement->defending = false;
	updates.changedStacks.back().data = replacement->save();
	updates.changedStacks.emplace_back(victim, static_cast<UnitChanges::EOperation>(-1));
	expectError([&] { callback->apply(updates); }, "Unknown unit operation");
	EXPECT_FALSE(child->battleGetUnitByID(actor)->defended()); // No invented rollback of first UPDATE.
	EXPECT_EQ(child->battleGetUnitByID(victim)->getAvailableHealth(), health - 7);
	record("typed-units-prefix-retained-after-throw");
}

TEST_F(HypotheticSpellServerRouteTest, TypedObstacleAddUpdateRemoveAndFieldCatapultRejection)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * callback = child->getServerCallback();
	const auto count = child->getAllObstacles().size();
	auto added = obstacle();
	callback->apply(added);
	ASSERT_EQ(child->getAllObstacles().size(), count + 1);
	const auto retained = std::dynamic_pointer_cast<const SpellCreatedObstacle>(child->getAllObstacles().back());
	ASSERT_NE(retained, nullptr);
	EXPECT_FALSE(retained->revealed);
	record("typed-obstacle-add");
	auto updated = obstacle(BattleChanges::EOperation::UPDATE);
	callback->apply(updated);
	EXPECT_TRUE(std::dynamic_pointer_cast<const SpellCreatedObstacle>(child->getAllObstacles().back())->revealed);
	EXPECT_FALSE(retained->revealed);
	record("typed-obstacle-update-retains-old-view");
	auto removed = obstacle(BattleChanges::EOperation::REMOVE);
	callback->apply(removed);
	EXPECT_EQ(child->getAllObstacles().size(), count);
	EXPECT_TRUE(child->hasObstacleChanges());
	record("typed-obstacle-remove");
	CatapultAttack catapult;
	catapult.battleID = BattleID(0);
	catapult.attackedPart = EWallPart::BOTTOM_WALL;
	catapult.damageDealt = 1;
	const auto before = viewState(*child);
	expectError([&] { callback->apply(catapult); }, "CatapultAttack without town!");
	EXPECT_EQ(viewState(*child), before);
	record("typed-field-catapult-throws");
}

TEST_F(HypotheticSpellServerRouteTest, TypedCatapultUsesAbsoluteDamageOnlyInChildSiege)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, true));
	const auto health = child->getWallStructuralHP(EWallPart::BOTTOM_WALL);
	ASSERT_GT(health, 50);
	CatapultAttack catapult;
	catapult.battleID = BattleID(0);
	catapult.attackedPart = EWallPart::BOTTOM_WALL;
	catapult.structuralDamage = 50;
	catapult.damageDealt = 1;
	child->getServerCallback()->apply(catapult);
	EXPECT_EQ(child->getWallStructuralHP(EWallPart::BOTTOM_WALL), health - 50);
	EXPECT_TRUE(child->hasWallChanges());
	record("typed-catapult-absolute50-not-legacy1");
}

TEST_F(HypotheticSpellServerRouteTest, GenericAllowlistAppliesThreeStatesAndPropagatesInvalidReceiptThrows)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, PlayerColor(1)));
	ScopedRouteLogs logs;
	auto * callback = child->getServerCallback();
	const int round = child->getRound();
	callback->recordCompletedHeroSpellCast(BattleSide::ATTACKER, 1);
	SetReactiveWeaveState weave;
	weave.battleID = BattleID(0);
	weave.side = BattleSide::DEFENDER;
	weave.casterSide = BattleSide::ATTACKER;
	weave.round = round;
	weave.empowerment = 15;
	weave.recipients = {victim};
	callback->apply(static_cast<CPackForClient &>(weave));
	EXPECT_EQ(child->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 15);
	EXPECT_EQ(child->getWarcastingState(BattleSide::DEFENDER).reactiveExpiryRound, round + 1);
	record("generic-reactive15");
	SetSpellResponseState response;
	response.battleID = BattleID(0);
	response.side = BattleSide::DEFENDER;
	response.state.armedInRound = round;
	const auto * defender = battle()->getSideHero(BattleSide::DEFENDER);
	ASSERT_EQ(defender, defenderSideHero);
	ASSERT_TRUE(defender->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
		std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)));
	ASSERT_EQ(child->battleGetFightingHero(BattleSide::DEFENDER), defender);
	auto hidden = std::make_shared<HypotheticBattle>(environment.get(),
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	EXPECT_EQ(hidden->getSideHero(BattleSide::DEFENDER), defender);
	EXPECT_EQ(hidden->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	auto * hiddenCallback = hidden->getServerCallback();
	hiddenCallback->recordCompletedHeroSpellCast(BattleSide::ATTACKER, 1);
	hiddenCallback->apply(static_cast<CPackForClient &>(weave));
	EXPECT_EQ(hidden->getWarcastingState(BattleSide::DEFENDER).reactiveEmpowermentPercent, 15);
	const auto hiddenBefore = viewState(*hidden);
	expectError([&] { hiddenCallback->apply(static_cast<CPackForClient &>(response)); },
		"Hypothetical Spell Response state requires Counterpressure");
	EXPECT_EQ(viewState(*hidden), hiddenBefore);
	record("generic-hidden-opponent-response-rejects-with-world-perk-and-no-mutation");
	callback->apply(static_cast<CPackForClient &>(response));
	EXPECT_EQ(child->getSpellResponseState(BattleSide::DEFENDER).armedInRound, round);
	record("generic-response-current-round");
	SetOverwhelmingFormulaState formula;
	formula.battleID = BattleID(0);
	formula.side = BattleSide::ATTACKER;
	formula.state.lastCandidateCastToken = 1;
	callback->apply(static_cast<CPackForClient &>(formula));
	EXPECT_EQ(child->getOverwhelmingFormulaState(BattleSide::ATTACKER).lastCandidateCastToken, 1u);
	EXPECT_EQ(child->getOverwhelmingFormulaState(BattleSide::ATTACKER).winningCastToken, 0u);
	record("generic-formula-first-token1");
	const auto accepted = viewState(*child);
	weave.battleID = BattleID(1);
	expectError([&] { callback->apply(static_cast<CPackForClient &>(weave)); },
		"Reactive Weave receipt has invalid cast provenance");
	EXPECT_EQ(viewState(*child), accepted);
	record("generic-invalid-reactive-provenance");
	response.state.armedInRound = round + 2;
	expectError([&] { callback->apply(static_cast<CPackForClient &>(response)); },
		"Spell Response state can only be armed in the current round");
	EXPECT_EQ(viewState(*child), accepted);
	record("generic-invalid-response-round");
	formula.state.lastCandidateCastToken = 3;
	expectError([&] { callback->apply(static_cast<CPackForClient &>(formula)); },
		"Invalid Overwhelming Formula state transition");
	EXPECT_EQ(viewState(*child), accepted);
	EXPECT_TRUE(logs.rows().empty());
	record("generic-three-invalid-updates-throw-without-rollback-of-earlier-valid-updates");
}

TEST_F(HypotheticSpellServerRouteTest, ErasedTypedAndUnsupportedGenericLogOnceAndReturnWithoutDispatch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ScopedRouteLogs logs;
	BattleLogMessage log;
	log.battleID = BattleID(0);
	BattleStackMoved move;
	move.battleID = BattleID(0);
	move.stack = actor;
	move.tilesToMove.insert(BattleHex(6, 5));
	BattleUnitsChanged units;
	units.battleID = BattleID(0);
	units.changedStacks.emplace_back(victim, UnitChanges::EOperation::REMOVE);
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(actor, std::vector<Bonus>{Bonus(BonusDuration::ONE_BATTLE,
		BonusType::STACKS_SPEED, BonusSource::SPELL_EFFECT, 9, BonusSourceID(SpellID(SpellID::HASTE)))});
	auto hit = injury();
	auto wall = obstacle();
	CatapultAttack catapult;
	catapult.battleID = BattleID(0);
	catapult.attackedPart = EWallPart::BOTTOM_WALL;
	CPackForClient unsupported;
	std::array<CPackForClient *, 8> packets{&log, &move, &units, &effect, &hit, &wall, &catapult, &unsupported};
	const auto before = viewState(*child);
	for(auto * packet : packets)
	{
		const auto count = logs.rows().size();
		EXPECT_NO_THROW(child->getServerCallback()->apply(*packet));
		const auto rows = logs.rows();
		ASSERT_EQ(rows.size(), count + 1);
		EXPECT_EQ(rows.back(), std::string("Package of type ") + typeid(*packet).name() + " is not allowed in battle evaluation");
		EXPECT_EQ(viewState(*child), before);
		record(std::string("generic-erased-reject:") + typeid(*packet).name());
	}
}

TEST_F(HypotheticSpellServerRouteTest, InjuryFailureKeepsAppliedHealthButPublishesNoPlagueReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto health = child->battleGetUnitByID(victim)->getAvailableHealth();
	const auto actorBefore = child->battleGetUnitByID(actor)->acquireState()->save();
	auto packet = injury();
	packet.stacks.front().flags = BattleStackAttacked::SPELL_EFFECT;
	packet.stacks.front().spellID = SpellID(SpellID::decode(std::string(newHorizonsPlague::SPELL_ID)));
	ASSERT_TRUE(packet.stacks.front().spellID.hasValue());
	ASSERT_TRUE(child->getProjectedPlagueHits().empty());
	auto & malformed = packet.stacks.emplace_back();
	malformed.stackAttacked = actor;
	malformed.newState = UnitChanges(actor, UnitChanges::EOperation::UPDATE);
	malformed.newState.data = actorBefore;
	malformed.newState.data["state"]["luckyOwnAttackSequence"].String() = "not-a-boolean";
	expectError([&] { child->getServerCallback()->apply(packet); },
		"Own lucky attack sequence must be a boolean");
	EXPECT_EQ(child->battleGetUnitByID(victim)->getAvailableHealth(), health - 7);
	EXPECT_EQ(child->battleGetUnitByID(actor)->acquireState()->save(), actorBefore);
	EXPECT_TRUE(child->getProjectedPlagueHits().empty()); // Publication follows successful visitor/rebirth handling.
	record("injury-prefix-mutated-plague-receipt-unpublished");
}
