/*
 * NewHorizonsArcheryAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleEvaluator.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/SelectedAttackReceipt.h"
#include "../../AI/BattleAI/VisibleCandidateWorld.h"
#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../lib/CStack.h"
#include "../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <bit>

namespace
{
class ArcheryAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ArcheryAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ArcheryAICallback final : public CBattleCallback
{
public:
	ArcheryAICallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
};

class NewHorizonsArcheryAITest : public BattleTestFixture
{
protected:
	std::shared_ptr<ArcheryAIEnvironment> environment;
	std::shared_ptr<ArcheryAICallback> callback;
	CStack * shooter = nullptr;
	CStack * target = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool withSkirmisher, bool blockCurrentShot = true, bool useMarksman = false)
	{
		startGame();
		if(withSkirmisher)
		{
			ASSERT_NO_FATAL_FAILURE(selectFixtureSkirmisher());
		}
		startBattle();
		shooter = addStack(BattleSide::ATTACKER,
			creatureByName(useMarksman ? "core:marksman" : "core:titan"), BattleHex(3, 5), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
		CStack * blocker = nullptr;
		if(withSkirmisher && blockCurrentShot)
			blocker = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(6, 5), 100);

		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != shooter && unit != target && unit != blocker)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		beginCombat();
		battle()->activeStack = shooter->unitId();
		callback = std::make_shared<ArcheryAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	}

	void replaceFixtureBasicPerk(CGHeroInstance * hero, std::string_view authoredId,
		std::string_view desiredId)
	{
		const auto skill = std::string(newHorizonsArchery::SKILL);
		const auto desired = std::string(desiredId);
		const int decoded = SecondarySkill::decode(skill);
		ASSERT_GE(decoded, 0);
		hero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		auto & perks = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(perks.rules));
		const auto capturedRules = perks.rules;
		const auto capturedWeights = hero->getPerkOfferWeightContext().referenceWeights;
		const auto rank = hero->getPerkSkillRank(skill);
		ASSERT_EQ(rank, MasteryLevel::BASIC);
		const auto definition = newHorizonsHeroes::perkDefinition(perks.rules, skill, desired);
		ASSERT_TRUE(definition);
		ASSERT_EQ(definition->requiredRank, "basic");
		const newHorizonsHeroes::PerkSelection authored{skill, std::string(authoredId)};
		const auto authoredDefinition = newHorizonsHeroes::perkDefinition(perks.rules, skill, authored.perkId);
		ASSERT_TRUE(authoredDefinition);
		ASSERT_EQ(authoredDefinition->requiredRank, "basic");
		ASSERT_EQ(std::ranges::count_if(perks.selected, [&skill](const auto & entry) { return entry.skillId == skill; }), 1);
		const auto starting = std::ranges::find(perks.selected, authored);
		ASSERT_NE(starting, perks.selected.end());
		ASSERT_FALSE(perks.canAcquirePerk(skill, desired, rank)); // Occupied Basic slot stays rejected.
		// Test construction, not a gameplay respec: neutral skill ranks do not
		// erase authored choices. Replace that one fixture selection only.
		auto expected = perks.selected;
		expected.erase(expected.begin() + std::distance(perks.selected.begin(), starting));
		perks.selected.erase(starting);
		perks.validate();
		ASSERT_EQ(perks.selected, expected);
		ASSERT_TRUE(std::ranges::none_of(perks.selected, [&skill](const auto & entry) { return entry.skillId == skill; }));
		ASSERT_TRUE(perks.canAcquirePerk(skill, desired, rank));
		hero->applyPerkSelection({skill, desired});
		ASSERT_TRUE(hero->hasActivePerk(skill, desired));
		expected.push_back({skill, desired});
		ASSERT_EQ(perks.selected, expected);
		ASSERT_EQ(perks.rules, capturedRules);
		ASSERT_EQ(hero->getPerkOfferWeightContext().referenceWeights, capturedWeights);
		ASSERT_EQ(hero->getPerkSkillRank(skill), rank);
		ASSERT_FALSE(perks.canAcquirePerk(skill, authored.perkId, rank));
	}

	void selectFixtureSkirmisher()
	{
		ASSERT_NO_FATAL_FAILURE(replaceFixtureBasicPerk(attackerSideHero,
			newHorizonsArchery::TARGET_CALLER, newHorizonsArchery::SKIRMISHER));
		ASSERT_TRUE(newHorizonsArchery::hasSkirmisher(attackerSideHero));
	}

	void selectFixtureExpertPerk(std::string_view desired)
	{
		const auto skill = std::string(newHorizonsArchery::SKILL);
		const int decoded = SecondarySkill::decode(skill);
		ASSERT_GE(decoded, 0);
		attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::EXPERT,
			ChangeValueMode::ABSOLUTE);
		const auto & perks = attackerSideHero->getPerkState();
		ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(perks.rules));
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(newHorizonsArchery::TARGET_CALLER)));
		// Preserve Orrin's Basic choice and legally fill the missing Advanced tier.
		// Crossfire is inert here: these fixtures have only one friendly shooter.
		ASSERT_TRUE(perks.canAcquirePerk(skill, std::string(newHorizonsArchery::CROSSFIRE), MasteryLevel::EXPERT));
		attackerSideHero->applyPerkSelection({skill, std::string(newHorizonsArchery::CROSSFIRE)});
		ASSERT_TRUE(perks.canAcquirePerk(skill, std::string(desired), MasteryLevel::EXPERT));
		attackerSideHero->applyPerkSelection({skill, std::string(desired)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, std::string(desired)));
	}

	void prepareOwnedSkirmisherOverwatch(bool lethal)
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(selectFixtureSkirmisher());
		defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(std::string(newHorizonsArchery::SKILL))),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(newHorizonsArchery::hasPointBlankShot(defenderSideHero));
		const std::string skill = "new-horizons:battlecraft";
		const std::string perk = "new-horizons:battlecraft.overwatch";
		const int decoded = SecondarySkill::decode(skill);
		ASSERT_GE(decoded, 0);
		defenderSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC,
			ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(defenderSideHero->getPerkState().canAcquirePerk(skill, perk, MasteryLevel::BASIC));
		defenderSideHero->applyPerkSelection({skill, perk});
		ASSERT_TRUE(newHorizonsBattlecraft::hasOverwatch(defenderSideHero));
		startBattle();
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(2, 5), lethal ? 1 : 1000);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(8, 5), 10000);
		auto * reactor = addStack(BattleSide::DEFENDER, creatureByName("core:archer"), BattleHex(14, 5), lethal ? 1000 : 100);
		auto * survivor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(2, 8), 10);
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit != shooter && unit != target && unit != reactor && unit != survivor)
				remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		forceMaximumDamage(shooter);
		forceMaximumDamage(reactor);
		beginCombat();
		BattleSetActiveStack activate;
		activate.battleID = BattleID(0);
		activate.reason = BattleUnitTurnReason::TURN_QUEUE;
		activate.stack = reactor->unitId();
		gameHandler->sendAndApply(activate);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), BattleAction::makeWait(reactor)));
		ASSERT_TRUE(battle()->battleOverwatchReady(reactor));
		activate.stack = shooter->unitId();
		gameHandler->sendAndApply(activate);
		callback = std::make_shared<ArcheryAICallback>();
		callback->onBattleStarted(battle());
		environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	}

	void checkOwnedSkirmisherOverwatch(bool lethal)
	{
		ASSERT_NO_FATAL_FAILURE(prepareOwnedSkirmisherOverwatch(lethal));
		const BattleHex destination(4, 5);
		ASSERT_TRUE(battle()->battleCanSkirmisherAttackFromHex(shooter, target->getPosition(), destination));
		const auto reactors = battle()->battleGetUnitsIf([this](const battle::Unit * unit)
			{ return battle()->battleOverwatchReady(unit); });
		ASSERT_EQ(reactors.size(), 1u);
		ASSERT_FALSE(battle()->battleCanOverwatch(reactors.front(), shooter, BattleHex(2, 5), BattleHex(3, 5)));
		ASSERT_TRUE(battle()->battleCanOverwatch(reactors.front(), shooter, BattleHex(3, 5), destination));
		const auto reactorId = reactors.front()->unitId();
		const auto shooterId = shooter->unitId();
		const auto targetId = target->unitId();
		const auto snapshot = [](const auto & world)
		{
			std::map<uint32_t, JsonNode> result;
			for(const auto * unit : world.battleAliveUnits())
				result.emplace(unit->unitId(), unit->acquireState()->save());
			return result;
		};
		const auto rngBytes = [](auto & rng)
		{
			CMemorySerializer wire;
			wire.oser & rng;
			return wire.extractBuffer();
		};
		auto source = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
		auto sibling = std::make_shared<HypotheticBattle>(environment.get(), source);
		const auto liveBefore = gameState()->saveToMemory();
		const auto sourceBefore = snapshot(*source);
		const auto siblingBefore = snapshot(*sibling);
		const auto serverRng = rngBytes(*gameHandler->randomizer);
		const auto aiRng = rngBytes(CRandomGenerator::getDefault());
		DamageCache cache;
		PotentialTargets candidates(source->battleGetUnitByID(shooterId), cache, source);
		const auto stationary = std::ranges::find_if(candidates.possibleAttacks, [&](const auto & attack)
		{
			return attack.attack.shooting && !attack.from.isValid() && attack.attack.defender->unitId() == targetId;
		});
		ASSERT_NE(stationary, candidates.possibleAttacks.end());
		EXPECT_TRUE(stationary->overwatchHits.empty()) << "A direct shot does not enter Overwatch movement admission";
		EXPECT_EQ(stationary->attackerState->getPosition(), shooter->getPosition());
		const auto owned = std::ranges::find_if(candidates.possibleAttacks, [&](const auto & attack)
		{
			return attack.attack.shooting && attack.from == destination && attack.attack.defender->unitId() == targetId;
		});
		ASSERT_NE(owned, candidates.possibleAttacks.end());
		ASSERT_TRUE(owned->queryWorld);
		BattleAttackInfo directInput(source->battleGetUnitByID(shooterId), source->battleGetUnitByID(targetId), 2, true);
		directInput.attackerPos = destination;
		directInput.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
		const auto direct = AttackPossibility::evaluate(directInput, destination, cache, source);
		ASSERT_EQ(owned->overwatchHits.size(), 1u);
		ASSERT_EQ(direct.overwatchHits.size(), 1u);
		EXPECT_EQ(owned->overwatchHits.front().shooterId, reactorId);
		EXPECT_EQ(owned->overwatchHits.front().position, destination);
		EXPECT_EQ(owned->overwatchHits.front().healthLoss, direct.overwatchHits.front().healthLoss);
		EXPECT_EQ(std::bit_cast<uint32_t>(owned->attackerDamageReduce), std::bit_cast<uint32_t>(direct.attackerDamageReduce));
		EXPECT_EQ(std::bit_cast<uint32_t>(owned->defenderDamageReduce), std::bit_cast<uint32_t>(direct.defenderDamageReduce));
		ASSERT_EQ(owned->fortuneStrikes.size(), direct.fortuneStrikes.size());
		for(size_t index = 0; index < owned->fortuneStrikes.size(); ++index)
		{
			EXPECT_EQ(owned->fortuneStrikes[index].hits, direct.fortuneStrikes[index].hits);
			EXPECT_EQ(owned->fortuneStrikes[index].resolvedHits, direct.fortuneStrikes[index].resolvedHits);
		}
		ASSERT_NE(owned->effectPreview, nullptr);
		ASSERT_NE(direct.effectPreview, nullptr);
		EXPECT_EQ(snapshot(*owned->effectPreview), snapshot(*direct.effectPreview));
		EXPECT_EQ(owned->attackerState->alive(), !lethal);
		EXPECT_EQ(owned->fortuneStrikes.empty(), lethal);
		const auto predictedReactor = owned->effectPreview->getForUpdate(reactorId);
		EXPECT_EQ(predictedReactor->shots.available(), reactors.front()->acquireState()->shots.available() - 1);
		EXPECT_FALSE(owned->effectPreview->battleOverwatchReady(predictedReactor.get()));
		EXPECT_EQ(gameState()->saveToMemory(), liveBefore);
		EXPECT_EQ(snapshot(*source), sourceBefore);
		EXPECT_EQ(snapshot(*sibling), siblingBefore);
		EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
		EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
		const auto attacksBefore = server.attacks.size();
		auto action = BattleAction::makeMeleeAttack(shooter, target->getPosition(), destination, false);
		action.archerySkirmisherAttack = true;
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		EXPECT_EQ(shooter->alive(), !lethal);
		EXPECT_EQ(shooter->getPosition(), destination);
		EXPECT_EQ(shooter->getAvailableHealth(), owned->attackerState->getAvailableHealth());
		EXPECT_EQ(shooter->shots.available(), owned->attackerState->shots.available());
		EXPECT_EQ(target->getAvailableHealth(), owned->effectPreview->battleGetUnitByID(targetId)->getAvailableHealth());
		const auto * nativeReactor = battle()->getStack(reactorId, false);
		EXPECT_EQ(nativeReactor->shots.available(), predictedReactor->shots.available());
		EXPECT_EQ(nativeReactor->acquireState()->battlecraftOverwatchReadyRound,
			predictedReactor->acquireState()->battlecraftOverwatchReadyRound);
		EXPECT_EQ(nativeReactor->acquireState()->battlecraftOverwatchUsedRound,
			predictedReactor->acquireState()->battlecraftOverwatchUsedRound);
		ASSERT_GT(server.attacks.size(), attacksBefore);
		EXPECT_EQ(server.attacks[attacksBefore].stackAttacking, reactorId);
		EXPECT_EQ(std::ranges::count_if(server.attacks.begin() + attacksBefore, server.attacks.end(),
			[shooterId](const auto & attack) { return attack.stackAttacking == shooterId && attack.shot(); }), lethal ? 0 : 1);
		EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
	}

	BattleAction choose()
	{
		BattleEvaluator evaluator(environment, callback, shooter, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 1.0f, 2);
		return evaluator.selectStackAction(shooter);
	}
};
}

TEST_F(NewHorizonsArcheryAITest, OrdinaryShooterKeepsUsingDirectShotAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	const BattleAction action = choose();
	EXPECT_EQ(action.actionType, EActionType::SHOOT);
	ASSERT_FALSE(action.target.empty());
	EXPECT_EQ(action.target.front().unitValue, target->unitId());
}

TEST_F(NewHorizonsArcheryAITest, OwnedSkirmisherOverwatchMatchesDirectPreviewAndNativeMoveThenShot)
{
	ASSERT_NO_FATAL_FAILURE(checkOwnedSkirmisherOverwatch(false));
}

TEST_F(NewHorizonsArcheryAITest, LethalOwnedSkirmisherOverwatchPreventsOutgoingShotInPreviewAndNative)
{
	ASSERT_NO_FATAL_FAILURE(checkOwnedSkirmisherOverwatch(true));
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherUsesCanonicalWalkAndAttackOnlyForLegalMoveThenShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	// A friendly midpoint stack does not block shots; moving must resolve range.
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 6, BonusSourceID()));
	ASSERT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()));
	ASSERT_FALSE(battle()->battleCanShootAction(shooter, target->getPosition()));
	const auto candidates = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	ASSERT_FALSE(candidates.empty());

	// The unchanged exchange scorer prefers waiting over an immediate shot here.
	const BattleAction initial = choose();
	ASSERT_EQ(initial.actionType, EActionType::WAIT);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetActionController(shooter), initial));
	ASSERT_TRUE(shooter->acquireState()->waitedThisTurn);
	// Advance only through validated actions; do not fabricate a waited activation.
	for(int actions = 0; actions < 8 && battle()->battleActiveUnit()
		&& battle()->battleActiveUnit()->unitId() != shooter->unitId(); ++actions)
	{
		const auto * active = battle()->battleActiveUnit();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
	}
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), shooter->unitId());
	ASSERT_TRUE(shooter->acquireState()->waitedThisTurn);
	const BattleAction action = choose();
	EXPECT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	EXPECT_TRUE(action.archerySkirmisherAttack);
	ASSERT_EQ(action.target.size(), 2u);
	EXPECT_EQ(action.target[1].hexValue, target->getPosition());
	EXPECT_TRUE(vstd::contains(candidates, action.target[0].hexValue))
		<< "AI should submit its scored legal destination; it need not match a nearest-hex tie-breaker";
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetActionController(shooter), action));
	EXPECT_EQ(shooter->getPosition(), action.target[0].hexValue);
}

TEST_F(NewHorizonsArcheryAITest, PotentialTargetsScoreSkirmisherPositionsAlongsideDirectShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	ASSERT_TRUE(battle()->battleCanShoot(shooter, target->getPosition()));
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto candidates = simulation->battleGetSkirmisherAttackFromHexes(
		simulation->battleGetUnitByID(shooter->unitId()), target->getPosition());
	ASSERT_FALSE(candidates.empty());
	PotentialTargets evaluated(shooter, cache, simulation);
	const uint32_t targetUnitId = target->unitId();
	EXPECT_TRUE(std::ranges::any_of(evaluated.possibleAttacks, [targetUnitId](const AttackPossibility & attack)
	{
		return attack.attack.shooting && attack.attack.defender->unitId() == targetUnitId
			&& !attack.from.isValid();
	})) << "Direct fire remains a candidate when legal";
	for(const BattleHex & candidate : candidates)
		EXPECT_TRUE(std::ranges::any_of(evaluated.possibleAttacks, [targetUnitId, candidate](const AttackPossibility & attack)
		{
			return attack.attack.shooting && attack.attack.defender->unitId() == targetUnitId
				&& attack.from == candidate;
		})) << "Each legal move-and-fire destination is evaluated by the AI";
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherForecastsEveryMarksmanRangedAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false, true));
	const int expectedAttackCount = shooter->getTotalAttacks(true);
	ASSERT_GT(expectedAttackCount, 1);
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto candidates = simulation->battleGetSkirmisherAttackFromHexes(
		simulation->battleGetUnitByID(shooter->unitId()), target->getPosition());
	ASSERT_FALSE(candidates.empty());
	PotentialTargets evaluated(shooter, cache, simulation);
	const uint32_t targetUnitId = target->unitId();
	for(const BattleHex & candidate : candidates)
	{
		const auto scored = std::ranges::find_if(evaluated.possibleAttacks,
			[targetUnitId, candidate](const AttackPossibility & attack)
			{
				return attack.attack.shooting && attack.attack.defender->unitId() == targetUnitId
					&& attack.from == candidate;
			});
		ASSERT_NE(scored, evaluated.possibleAttacks.end());
		EXPECT_EQ(scored->fortuneStrikes.size(), static_cast<size_t>(expectedAttackCount))
			<< "AI forecasts the same sequence of ranged strikes that the server executes";
	}
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherForecastProjectsMovePositionForRangeAndCounterfire)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectFixtureSkirmisher());
	ASSERT_NO_FATAL_FAILURE(replaceFixtureBasicPerk(defenderSideHero,
		newHorizonsArchery::POINT_BLANK_SHOT, newHorizonsArchery::COUNTERFIRE));
	ASSERT_TRUE(newHorizonsArchery::hasCounterfire(defenderSideHero));
	startBattle();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(2, 5), 10);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:titan"), BattleHex(11, 5), 10);
	const Bonus range(BonusDuration::PERMANENT, BonusType::LIMITED_SHOOTING_RANGE,
		BonusSource::OTHER, 6, BonusSourceID());
	shooter->addNewBonus(std::make_shared<Bonus>(range));
	target->addNewBonus(std::make_shared<Bonus>(range));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	ASSERT_FALSE(battle()->battleCanShoot(shooter, target->getPosition()))
		<< "The original position is outside the shooter's range";
	ASSERT_FALSE(battle()->battleCanShoot(target, shooter->getPosition()))
		<< "Counterfire cannot legally reach the original position";

	const auto candidates = battle()->battleGetSkirmisherAttackFromHexes(shooter, target->getPosition());
	const auto candidate = std::ranges::find_if(candidates, [this](const BattleHex & hex)
		{ return BattleHex::getDistance(hex, target->getPosition()) <= 6
			&& BattleHex::getDistance(hex, shooter->getPosition()) <= shooter->getMovementRange(0) / 2; });
	ASSERT_NE(candidate, candidates.end());
	const int moveDistance = static_cast<int>(BattleHex::getDistance(*candidate, shooter->getPosition()));

	environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	callback = std::make_shared<ArcheryAICallback>();
	callback->onBattleStarted(battle());
	const auto snapshot = [](const auto & world)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : world.battleGetAllUnits(false))
			result.emplace(unit->unitId(), unit->acquireState()->save());
		return result;
	};
	const auto rngBytes = [](auto & rng)
	{
		CMemorySerializer wire;
		wire.oser & rng;
		return wire.extractBuffer();
	};
	const auto forecast = [&](const std::shared_ptr<HypotheticBattle> & simulation)
	{
		DamageCache cache;
		cache.buildDamageCache(simulation, BattleSide::ATTACKER);
		BattleAttackInfo projectedShot(simulation->battleGetUnitByID(shooter->unitId()),
			simulation->battleGetUnitByID(target->unitId()), moveDistance, true);
		projectedShot.attackerPos = *candidate;
		projectedShot.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
		return AttackPossibility::evaluate(projectedShot, *candidate, cache, simulation);
	};
	const auto counterReceipts = [this](const AttackPossibility & preview)
	{
		return std::ranges::count_if(preview.fortuneStrikes, [this](const auto & strike)
		{
			return strike.retaliation && strike.shooting && strike.attackerId == target->unitId()
				&& strike.defenderId == shooter->unitId();
		});
	};
	const auto liveBefore = snapshot(*battle());
	const auto serverRng = rngBytes(*gameHandler->randomizer);
	const auto aiRng = rngBytes(CRandomGenerator::getDefault());
	auto player = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	ASSERT_EQ(player->battleGetMySide(), BattleSide::ATTACKER);
	ASSERT_EQ(player->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(),
		VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_EQ(player->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	const auto playerBefore = snapshot(*player);
	const auto hidden = forecast(player);
	ASSERT_NE(hidden.attackerState, nullptr);
	EXPECT_EQ(hidden.attackerState->getPosition(), *candidate);
	EXPECT_EQ(hidden.attackerState->getAvailableHealth(), shooter->getAvailableHealth());
	EXPECT_EQ(counterReceipts(hidden), 0) << "Hidden perks are unknown, not known absent";
	EXPECT_EQ(snapshot(*player), playerBefore);
	// Explicit authoritative mechanics fixture, never an ordinary AI visibility bypass.
	auto allKnowing = std::shared_ptr<CBattleInfoCallback>(gameState(), battle());
	ASSERT_EQ(allKnowing->battleGetMySide(), BattleSide::ALL_KNOWING);
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), allKnowing);
	ASSERT_TRUE(newHorizonsArchery::hasCounterfire(simulation->battleGetFightingHero(BattleSide::DEFENDER)));
	const auto knownBefore = snapshot(*simulation);
	const auto possibility = forecast(simulation);
	ASSERT_NE(possibility.attackerState, nullptr);
	EXPECT_EQ(possibility.attackerState->getPosition(), *candidate)
		<< "AI damage and reaction calculations use the firing destination, not the original hex";
	EXPECT_GT(possibility.defenderDamageReduce, 0.0f);
	EXPECT_LT(possibility.attackerState->getAvailableHealth(), shooter->getAvailableHealth());
	EXPECT_EQ(counterReceipts(possibility), 1);
	ASSERT_NE(possibility.effectPreview, nullptr);
	EXPECT_EQ(possibility.effectPreview->getForUpdate(target->unitId())->archeryCounterfireRound,
		simulation->battleGetRound());
	EXPECT_GT(possibility.attackerDamageReduce, 0.0f)
		<< "The target's legal Counterfire at the moved destination is included in the forecast";
	EXPECT_EQ(snapshot(*simulation), knownBefore);
	EXPECT_EQ(snapshot(*battle()), liveBefore);
	EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
}

TEST_F(NewHorizonsArcheryAITest, NullkillerProjectsDeadeyeForOnlyTheFirstMarksmanStrike)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectFixtureExpertPerk(newHorizonsArchery::DEADEYE));
	startBattle();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(3, 5), 10);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	callback = std::make_shared<ArcheryAICallback>();
	callback->onBattleStarted(battle());
	environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto * simShooter = simulation->battleGetUnitByID(shooter->unitId());
	const auto * simTarget = simulation->battleGetUnitByID(target->unitId());
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(simShooter, simTarget, 0, true),
		BattleHex::INVALID, cache, simulation);
	ASSERT_EQ(possibility.fortuneStrikes.size(), static_cast<size_t>(shooter->getTotalAttacks(true)));
	ASSERT_GT(possibility.fortuneStrikes.size(), 1u);
	ASSERT_EQ(possibility.fortuneStrikes[0].hits.front().first, target->unitId());
	ASSERT_EQ(possibility.fortuneStrikes[1].hits.front().first, target->unitId());
	EXPECT_GT(possibility.fortuneStrikes[0].hits.front().second,
		possibility.fortuneStrikes[1].hits.front().second)
		<< "The forecast uses maximum creature damage on the first shot, then ordinary ranged damage";
	EXPECT_EQ(possibility.attackerState->archeryDeadeyeRound, battle()->battleGetRound());
}

TEST_F(NewHorizonsArcheryAITest, NullkillerProjectsSuppressionAndRainDamageIntoTheTargetStates)
{
	startGame();
	const int archery = SecondarySkill::decode(std::string(newHorizonsArchery::SKILL));
	ASSERT_GE(archery, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(archery), MasteryLevel::EXPERT,
		ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::SUPPRESSION)});
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::RAIN_OF_ARROWS)});
	startBattle();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(3, 5), 10);
	const BattleHex targetHex(12, 5);
	BattleHex collateralHex = BattleHex::INVALID;
	for(int rawHex = 0; rawHex < GameConstants::BFIELD_SIZE; ++rawHex)
	{
		const BattleHex candidate(rawHex);
		if(candidate.isValid() && BattleHex::getDistance(candidate, targetHex) == 1
			&& !battle()->battleGetStackByPos(candidate))
		{
			collateralHex = candidate;
			break;
		}
	}
	ASSERT_TRUE(collateralHex.isValid());
	auto * secondary = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), collateralHex, 50);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), targetHex, 100);
	ASSERT_LT(secondary->unitId(), target->unitId()) << "The collateral is deliberately enumerated before the primary";
	const Bonus splash(BonusDuration::PERMANENT, BonusType::SHOOTS_ALL_ADJACENT,
		BonusSource::OTHER, 1, BonusSourceID());
	shooter->addNewBonus(std::make_shared<Bonus>(splash));
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target && unit != secondary)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	callback = std::make_shared<ArcheryAICallback>();
	callback->onBattleStarted(battle());
	environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto * simShooter = simulation->battleGetUnitByID(shooter->unitId());
	const auto * simTarget = simulation->battleGetUnitByID(target->unitId());
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(simShooter, simTarget, 0, true),
		BattleHex::INVALID, cache, simulation);
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto projectedTarget = possibility.effectPreview->getForUpdate(target->unitId());
	const auto speedPenalties = projectedTarget->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_TRUE(speedPenalties);
	EXPECT_TRUE(std::ranges::any_of(*speedPenalties, [](const auto & bonus)
	{
		return bonus->source == BonusSource::OTHER && bonus->duration == BonusDuration::STACK_GETS_TURN
			&& bonus->val == -1;
	}));
	const auto collateralSpeedPenalties = possibility.effectPreview->getForUpdate(secondary->unitId())
		->getAllBonuses(Selector::type()(BonusType::STACKS_SPEED));
	ASSERT_TRUE(collateralSpeedPenalties);
	EXPECT_FALSE(std::ranges::any_of(*collateralSpeedPenalties, [](const auto & bonus)
	{
		return bonus->source == BonusSource::OTHER && bonus->duration == BonusDuration::STACK_GETS_TURN
			&& bonus->val == -1;
	})) << "The AI projects Suppression onto the server's primary hit, not lower-ID collateral";
	int64_t projectedPrimaryDamage = 0;
	int64_t projectedCollateralDamage = 0;
	for(const auto & strike : possibility.fortuneStrikes)
		for(const auto & hit : strike.hits)
		{
			if(hit.first == target->unitId())
				projectedPrimaryDamage += hit.second;
			if(hit.first == secondary->unitId())
				projectedCollateralDamage += hit.second;
		}
	ASSERT_GT(projectedPrimaryDamage, 0);
	const auto projectedSecondary = possibility.effectPreview->getForUpdate(secondary->unitId());
	ASSERT_GT(secondary->getAvailableHealth() - projectedCollateralDamage,
		projectedPrimaryDamage * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100)
		<< "Keep enough health after ordinary splash to observe the complete Rain damage";
	EXPECT_EQ(secondary->getAvailableHealth() - projectedSecondary->getAvailableHealth() - projectedCollateralDamage,
		projectedPrimaryDamage * newHorizonsArchery::RAIN_OF_ARROWS_DAMAGE_PERCENT / 100)
		<< "Rain is forecast from primary damage and applied to the adjacent stack in the projected state";
	EXPECT_EQ(possibility.attackerState->archeryRainOfArrowsActivationSerial,
		static_cast<int32_t>(battle()->getBattle()->getActivationSerial()));
}

TEST_F(NewHorizonsArcheryAITest, LethalRainSecondaryKeepsNullkillerDamageScoreFinite)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(selectFixtureExpertPerk(newHorizonsArchery::RAIN_OF_ARROWS));
	startBattle();
	shooter = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(3, 5), 100);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	BattleHex secondaryHex = BattleHex::INVALID;
	for(const auto & hex : target->getSurroundingHexes())
		if(hex.isValid() && !battle()->battleGetStackByPos(hex))
		{
			secondaryHex = hex;
			break;
		}
	ASSERT_TRUE(secondaryHex.isValid());
	ASSERT_EQ(BattleHex::getDistance(secondaryHex, target->getPosition()), 1);
	auto * secondary = addStack(BattleSide::DEFENDER, creatureByName("core:marksman"), secondaryHex, 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != shooter && unit != target && unit != secondary)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	beginCombat();
	callback = std::make_shared<ArcheryAICallback>();
	callback->onBattleStarted(battle());
	environment = std::make_shared<ArcheryAIEnvironment>(gameState());
	auto simulation = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(simulation, BattleSide::ATTACKER);
	const auto * simShooter = simulation->battleGetUnitByID(shooter->unitId());
	const auto * simTarget = simulation->battleGetUnitByID(target->unitId());
	const auto possibility = AttackPossibility::evaluate(BattleAttackInfo(simShooter, simTarget, 0, true),
		BattleHex::INVALID, cache, simulation);
	ASSERT_NE(possibility.effectPreview, nullptr);
	const auto projectedSecondary = possibility.effectPreview->getForUpdate(secondary->unitId());
	EXPECT_FALSE(projectedSecondary->alive()) << "The chosen adjacent target is lethally hit by Rain of Arrows";
	EXPECT_TRUE(std::isfinite(static_cast<double>(possibility.defenderDamageReduce)))
		<< "A lethal Rain secondary must not evaluate casualty value after its unit count reaches zero";
}

TEST_F(NewHorizonsArcheryAITest, SelectedSkirmisherReplayKeepsTheFiringHexForOwnedAndDirectPreviews)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 6, BonusSourceID()));
	auto source = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(source, BattleSide::ATTACKER);
	PotentialTargets targets(source->battleGetUnitByID(shooter->unitId()), cache, source);
	const auto moved = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
	{
		return ap.attack.shooting && ap.attack.defender->unitId() == target->unitId()
			&& ap.from.isAvailable() && ap.from != shooter->getPosition();
	});
	ASSERT_NE(moved, targets.possibleAttacks.end());
	ASSERT_TRUE(moved->queryWorld);
	ASSERT_EQ(moved->attackerState->getPosition(), moved->from);
	ASSERT_TRUE(source->battleCanSkirmisherAttackFromHex(source->battleGetUnitByID(shooter->unitId()),
		target->getPosition(), moved->from));
	const auto direct = AttackPossibility::evaluate(moved->attack, moved->from, cache, source);
	ASSERT_FALSE(direct.queryWorld);
	ASSERT_EQ(direct.attackerState->getPosition(), moved->from);
	const auto snapshot = [](const auto & world)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : world.battleAliveUnits())
			result.emplace(unit->unitId(), unit->acquireState()->save());
		return result;
	};
	const auto rngBytes = [](auto & rng)
	{
		CMemorySerializer wire;
		wire.oser & rng;
		return wire.extractBuffer();
	};
	auto first = std::make_shared<HypotheticBattle>(environment.get(), source);
	auto second = std::make_shared<HypotheticBattle>(environment.get(), source);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), source);
	const auto liveBefore = snapshot(*battle());
	const auto sourceBefore = snapshot(*source);
	const auto siblingBefore = snapshot(*sibling);
	const auto previewBefore = moved->attackerState->save();
	const auto directBefore = direct.attackerState->save();
	const auto serverRng = rngBytes(*gameHandler->randomizer);
	const auto aiRng = rngBytes(CRandomGenerator::getDefault());
	DamageCache firstCache, secondCache;
	BattleExchangeVariant firstExchange, secondExchange;
	const auto firstScore = firstExchange.trackAttack(*moved, first, firstCache);
	const auto secondScore = secondExchange.trackAttack(direct, second, secondCache);
	EXPECT_EQ(first->battleGetUnitByID(shooter->unitId())->getPosition(), moved->from);
	EXPECT_EQ(second->battleGetUnitByID(shooter->unitId())->getPosition(), moved->from);
	EXPECT_EQ(std::bit_cast<uint32_t>(firstScore), std::bit_cast<uint32_t>(secondScore));
	EXPECT_EQ(snapshot(*first), snapshot(*second));
	EXPECT_EQ(snapshot(*battle()), liveBefore);
	EXPECT_EQ(snapshot(*source), sourceBefore);
	EXPECT_EQ(snapshot(*sibling), siblingBefore);
	EXPECT_EQ(moved->attackerState->save(), previewBefore);
	EXPECT_EQ(direct.attackerState->save(), directBefore);
	EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
}

TEST_F(NewHorizonsArcheryAITest, ShootingPositionRestoreRejectsStrayFieldsAndForeignQueryOwners)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, false));
	auto source = std::make_shared<HypotheticBattle>(environment.get(), callback->getBattle(BattleID(0)));
	DamageCache cache;
	cache.buildDamageCache(source, BattleSide::ATTACKER);
	PotentialTargets targets(source->battleGetUnitByID(shooter->unitId()), cache, source);
	const auto direct = std::ranges::find_if(targets.possibleAttacks, [](const auto & ap)
		{ return ap.attack.shooting && !ap.from.isValid(); });
	const auto moved = std::ranges::find_if(targets.possibleAttacks, [this](const auto & ap)
		{ return ap.attack.shooting && ap.from.isAvailable() && ap.from != shooter->getPosition(); });
	ASSERT_NE(direct, targets.possibleAttacks.end());
	ASSERT_NE(moved, targets.possibleAttacks.end());
	auto branch = std::make_shared<HypotheticBattle>(environment.get(), source);
	auto sibling = std::make_shared<HypotheticBattle>(environment.get(), source);
	auto actor = branch->getForUpdate(shooter->unitId());
	const auto before = actor->save();
	selectedAttackReceipt::restorePosition(*direct, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "An ordinary direct shot never moves";
	auto stray = *direct;
	stray.from = moved->from;
	selectedAttackReceipt::restorePosition(stray, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "A stray shooting from-field is not a move receipt";
	stray.attackerState = moved->attackerState;
	selectedAttackReceipt::restorePosition(stray, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "A moved snapshot without Skirmisher classification cannot move";
	auto foreign = *moved;
	ASSERT_NE(sibling->battleGetUnitByID(shooter->unitId()), moved->attack.attacker);
	foreign.queryWorld = std::make_shared<const VisibleCandidateWorld>(sibling);
	selectedAttackReceipt::restorePosition(foreign, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "The owner must supply the actual preview actors";
	auto wrongActor = *moved;
	wrongActor.attackerState = source->battleGetUnitByID(target->unitId())->acquireState();
	wrongActor.attackerState->setPosition(moved->from);
	selectedAttackReceipt::restorePosition(wrongActor, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "A different unit's snapshot grants no actor movement";
	auto invalid = *moved;
	invalid.from = target->getPosition(); // Occupied by the hostile recipient.
	invalid.attackerState = moved->attackerState->acquireState();
	invalid.attackerState->setPosition(invalid.from);
	selectedAttackReceipt::restorePosition(invalid, *actor, *branch);
	EXPECT_EQ(actor->save(), before) << "Current canonical legality still rejects an occupied endpoint";
	EXPECT_EQ(source->battleGetUnitByID(shooter->unitId())->getPosition(), shooter->getPosition());
	EXPECT_EQ(sibling->battleGetUnitByID(shooter->unitId())->getPosition(), shooter->getPosition());
}

TEST_F(NewHorizonsArcheryAITest, SkirmisherWaitWinsTheCurrentExchangeComparisonWithoutMutatingState)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	shooter->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::LIMITED_SHOOTING_RANGE, BonusSource::OTHER, 6, BonusSourceID()));
	const auto snapshot = [](const auto & world)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : world.battleGetAllUnits(false))
			result.emplace(unit->unitId(), unit->acquireState()->save());
		return result;
	};
	const auto rngBytes = [](auto & rng)
	{
		CMemorySerializer wire;
		wire.oser & rng;
		return wire.extractBuffer();
	};
	const auto liveBefore = snapshot(*battle());
	const auto serverRng = rngBytes(*gameHandler->randomizer);
	const auto aiRng = rngBytes(CRandomGenerator::getDefault());
	BattleEvaluator actual(environment, callback, shooter, PlayerColor(0), BattleID(0),
		BattleSide::ATTACKER, 1.0f, 2);
	const auto action = actual.selectStackAction(shooter);
	auto view = callback->getBattle(BattleID(0));
	auto source = std::make_shared<HypotheticBattle>(environment.get(), view);
	const auto sourceBefore = snapshot(*source);
	DamageCache cache;
	cache.buildDamageCache(source, BattleSide::ATTACKER);
	PotentialTargets targets(source->battleGetUnitByID(shooter->unitId()), cache, source);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	const auto ranged = std::ranges::count_if(targets.possibleAttacks,
		[](const auto & ap) { return ap.attack.shooting; });
	ASSERT_GT(ranged, 0);
	BattleExchangeEvaluator scorer(view, environment, 1.0f, 2);
	const auto movement = scorer.findMoveTowardsUnreachable(shooter, targets, cache, source);
	BattleBestTargetTiming targetTiming;
	const auto best = scorer.findBestTarget(shooter, targets, cache, source, false, &targetTiming);
	const auto maximum = [&](const std::shared_ptr<HypotheticBattle> & phase)
	{
		scorer.updateReachabilityMap(phase);
		float score = EvaluationResult::INEFFECTIVE_SCORE;
		for(const auto & ap : targets.possibleAttacks)
		{
			const float value = scorer.evaluateExchange(ap, 0, targets, cache, phase);
			if(value > score)
				score = value;
		}
		return score;
	};
	const auto normalMaximum = maximum(source);
	auto waited = std::make_shared<HypotheticBattle>(environment.get(), source);
	waited->makeWait(shooter);
	const auto waitedMaximum = maximum(waited);
	EXPECT_EQ(action.actionType, EActionType::WAIT);
	EXPECT_EQ(targetTiming.normalExchange.calls, targets.possibleAttacks.size());
	EXPECT_EQ(targetTiming.waitedExchange.calls, targets.possibleAttacks.size());
	EXPECT_TRUE(best.wait);
	EXPECT_TRUE(best.bestAttack.attack.shooting);
	EXPECT_LE(movement.score, best.score);
	EXPECT_GT(waitedMaximum, normalMaximum);
	EXPECT_EQ(std::bit_cast<uint32_t>(best.score), std::bit_cast<uint32_t>(waitedMaximum));
	EXPECT_EQ(snapshot(*source), sourceBefore);
	EXPECT_EQ(snapshot(*battle()), liveBefore);
	EXPECT_EQ(rngBytes(*gameHandler->randomizer), serverRng);
	EXPECT_EQ(rngBytes(CRandomGenerator::getDefault()), aiRng);
}
