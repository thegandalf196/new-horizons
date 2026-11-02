/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "BattlecraftPreemptiveAIFixture.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/EnemyBypassReachability.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/GameSettings.h"
#include "../../lib/mapping/CMap.h"
#include "../../AI/BattleAI/CreatureCandidateScreening.h"
#include "../../AI/BattleAI/HeroActionAdmission.h"
#include "../../AI/BattleAI/PossibleSpellcast.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/HeroCommandRevision.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include <algorithm>
#include <limits>
#include <array>
#include <chrono>
#include "../../lib/filesystem/NhArtSha256.h"
#include <cstddef>
#include <bit>
#include <cmath>
#include <map>
#include <sstream>

namespace
{
class CreatureCandidateBaselineTest : public battlecraftTest::PreemptiveFixture
{
protected:
	struct ForecastRow { size_t ordinal; bool waited; float score; };
	std::vector<ForecastRow> forecastRows;
	bool historicalOrders = false;
	bool historicalHeroCapture = false;
	bool revisedCoefficientCapture = false;
	static constexpr float strengthRatio = 0.5f; // Explicit public evaluator input, not a recalculated army ratio.
	CStack * actor = nullptr;
	CStack * reserve = nullptr;
	std::array<CStack *, 3> enemies{};

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		if(historicalHeroCapture)
		{
			// Valid predecessor absence, captured by native initialization rather
			// than injected into a live Hero or substituted after loading.
			auto perks = LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
			perks.Struct().erase("quickStudyPolicy");
			perks.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
			auto growth = LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS);
			growth.Struct().erase("additionalSpecialties");
			growth.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, growth);
		}
		if(revisedCoefficientCapture)
		{
			auto commands = LIBRARY->engineSettings()->getValue(EGameSettings::COMBAT_HERO_COMMANDS);
			// Same revised representation, different valid native formula.
			auto & feint = commands["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"];
			feint["base"].Integer() = 4;
			feint["leadership"].Integer() = 0;
			feint["cap"].Integer() = 4;
			commands.setOverrideFlag(true);
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, commands);
		}
		if(historicalOrders)
		{
			const JsonNode file(JsonPath::builtin("config/newHorizonsCombatV3"));
			map->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS, file["combat"]["heroCommands"]);
		}
	}

	void prepare(bool distant, bool ranged = false)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		ASSERT_NO_FATAL_FAILURE(startBattle());
		actor = addStack(BattleSide::ATTACKER, creatureByName(ranged ? "core:archer" : "core:pikeman"),
			distant ? BattleHex(2, 5) : BattleHex(5, 5), 80);
		reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"),
			distant ? BattleHex(3, 2) : BattleHex(3, 5), 40);
		enemies[0] = addStack(BattleSide::DEFENDER, creatureByName(historicalOrders && ranged ? "core:walkingDead" : "core:pikeman"),
			distant ? (ranged ? BattleHex(11, 4) : BattleHex(12, 4)) : BattleHex(7, 4), distant && !ranged ? 80 : 60);
		enemies[1] = addStack(BattleSide::DEFENDER, creatureByName(historicalOrders && ranged ? "core:walkingDead" : "core:pikeman"),
			distant ? BattleHex(14, 6) : BattleHex(7, 6), distant ? 120 : 80);
		if(!distant)
			enemies[2] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"),
				BattleHex(8, 5), 120);
		removeOtherStacks({actor, reserve, enemies[0], enemies[1], enemies[2]});
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(const auto & unit : battle()->stacks)
			(void)unit->counterAttacks.available();
		initializeAI(PlayerColor(0));
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
		ASSERT_EQ(battle()->battleGetActionController(actor), PlayerColor(0));
		EXPECT_EQ(heroCommands::isRevisedRules(battle()->getHeroCommandRules()), !historicalOrders);
		ASSERT_NO_FATAL_FAILURE(recordEffectiveDomain());
	}

	void prepareNativeEdric()
	{
		// Build real authored level-one heroes directly. BattleTestFixture::startGame
		// neutralizes heroes, so it is deliberately not used in this scenario.
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:edric")), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode("core:edric")));
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:armorer", "new-horizons:armorer.countercharge"));
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:armorer"))), MasteryLevel::BASIC);
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"))), MasteryLevel::BASIC);
		ASSERT_GT(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 0);
		ASSERT_GT(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), 0);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(5, 5), 80);
		reserve = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 40);
		enemies[0] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 4), 60);
		enemies[1] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 6), 80);
		enemies[2] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 120);
		removeOtherStacks({actor, reserve, enemies[0], enemies[1], enemies[2]});
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(const auto & unit : battle()->stacks) (void)unit->counterAttacks.available();
		initializeAI(PlayerColor(0));
	}

	void roundTripCapturedProfiles()
	{
		const auto ids = gameState()->getMap().getHeroesOnMap();
		std::map<ObjectInstanceID, std::pair<JsonNode, JsonNode>> captures;
		for(const auto id : ids)
		{
			const auto * hero = gameState()->getHero(id);
			captures.emplace(id, std::make_pair(hero->getPrimaryGrowthRules(), hero->getPerkState().rules));
		}
		const auto commands = gameState()->getHeroCommandRules();
		// Use the established full-world loader, then resave/load. Battle creation
		// and all quality checks below consume the restored native objects.
		for(int pass = 0; pass < 2; ++pass)
		{
			ASSERT_NO_THROW(gameState()->loadFromMemory(gameState()->saveToMemory()));
			EXPECT_EQ(gameState()->getHeroCommandRules(), commands);
			for(const auto & [id, capture] : captures)
			{
				const auto * hero = gameState()->getHero(id);
				ASSERT_NE(hero, nullptr);
				EXPECT_EQ(hero->getPrimaryGrowthRules(), capture.first);
				EXPECT_EQ(hero->getPerkState().rules, capture.second);
			}
		}
	}

	void prepareNativeProfile(const std::string & heroKey, const std::string & troopKey, bool roundTrip = false)
	{
		// Build real authored level-one heroes directly. BattleTestFixture::startGame
		// neutralizes heroes, so it is deliberately not used in this scenario.
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(heroKey)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(troopKey), 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1}});
		ASSERT_NO_FATAL_FAILURE(startWithMap(std::move(builder)));
		const auto * originalAttacker = findHeroByOwner(PlayerColor(0));
		const auto * originalDefender = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(originalAttacker, nullptr);
		ASSERT_NE(originalDefender, nullptr);
		const auto attackerId = originalAttacker->id;
		const auto defenderId = originalDefender->id;
		if(roundTrip)
			ASSERT_NO_FATAL_FAILURE(roundTripCapturedProfiles());
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = gameState()->getHero(attackerId);
		defenderSideHero = gameState()->getHero(defenderId);
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroTypeID(), HeroTypeID(HeroTypeID::decode(heroKey)));
		ASSERT_GT(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 0);
		ASSERT_GT(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), 0);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		actor = addStack(BattleSide::ATTACKER, creatureByName(troopKey), BattleHex(5, 5), 80);
		reserve = addStack(BattleSide::ATTACKER, creatureByName(troopKey), BattleHex(3, 5), 40);
		enemies[0] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 4), 60);
		enemies[1] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(7, 6), 80);
		enemies[2] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(8, 5), 120);
		removeOtherStacks({actor, reserve, enemies[0], enemies[1], enemies[2]});
		ASSERT_NO_FATAL_FAILURE(beginCombat());
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = actor->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(const auto & unit : battle()->stacks) (void)unit->counterAttacks.available();
		initializeAI(PlayerColor(0));
	}

	void recordEffectiveDomain()
	{
		std::ostringstream bonuses;
		for(const auto * hero : {attackerSideHero, defenderSideHero})
		{
			for(int i = 0; i < LIBRARY->skillh->size(); ++i)
				ASSERT_EQ(hero->getSecSkillLevel(SecondarySkill(i)), 0);
			for(const auto & selection : hero->getPerkState().selected)
				ASSERT_FALSE(hero->hasActivePerk(selection.skillId, selection.perkId));
			EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 0);
			EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::DEFENSE), 0);
		}
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			ASSERT_TRUE(battle()->getHeroOrderStates(side).empty());
		for(const auto * unit : battle()->battleGetAllUnits(false))
		{
			const bool pikeman = unit->unitType()->getJsonKey() == "core:pikeman";
			const bool archer = unit->unitType()->getJsonKey() == "core:archer";
			const bool historicalWalkingDead = historicalOrders
				&& unit->unitType()->getJsonKey() == "core:walkingDead";
			ASSERT_TRUE(pikeman || archer || historicalWalkingDead);
			EXPECT_EQ(unit->hasBonusOfType(BonusType::SHOOTER), archer);
			if(historicalWalkingDead)
			{
				EXPECT_TRUE(unit->hasBonusOfType(BonusType::UNDEAD));
			}
			if(pikeman)
			{
				EXPECT_TRUE(unit->hasBonusOfType(BonusType::CHARGE_IMMUNITY));
			}
			for(const auto type : {BonusType::FLYING, BonusType::ATTACKS_NEAREST_CREATURE,
				BonusType::SPELL_LIKE_ATTACK, BonusType::ADDITIONAL_ATTACK,
				BonusType::COMBAT_EVENT_TRIGGER, BonusType::ON_COMBAT_EVENT,
				BonusType::SPELL_BEFORE_ATTACK, BonusType::SPELL_AFTER_ATTACK})
				ASSERT_FALSE(unit->hasBonusOfType(type));
			bonuses << unit->unitId() << ':' << unit->unitType()->getJsonKey() << '[';
			const auto observedBonuses = unit->getAllBonuses(Selector::all);
			for(const auto & bonus : *observedBonuses)
				bonuses << bonus->toJsonNode().toCompactString() << ';';
			bonuses << "];";
		}
		RecordProperty("EffectiveCreatureBonusesAndAbilities", bonuses.str());
		RecordProperty("HeroSkillsAndActivePerks", "native-neutralization-asserted-none");
		RecordProperty("DomainQualification", "baseline-only-native-abilities-recorded-not-closed-policy-admission");
		RecordProperty("ExplicitEvaluatorStrengthRatio", "0.5");
	}

	auto worldAndRng()
	{
		return std::array<std::vector<std::byte>, 4>{gameState()->saveToMemory(),
			projectedHitTest::serialized(*gameHandler->randomizer),
			projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())),
			projectedHitTest::serialized(CRandomGenerator::getDefault())};
	}

	static auto units(const HypotheticBattle & model)
	{
		std::map<uint32_t, JsonNode> result;
		for(const auto * unit : model.battleGetAllUnits(false))
			result.emplace(unit->unitId(), unit->acquireState()->save());
		return result;
	}

	static std::string identity(const AttackPossibility & ap)
	{
		std::ostringstream out;
		out << ap.attack.attacker->unitId() << ':' << ap.attack.defender->unitId()
			<< ':' << ap.attack.shooting << ':' << ap.from.toInt() << ':' << ap.dest.toInt()
			<< ':' << ap.attack.chargeDistance;
		return out.str();
	}

	void recordPool(const PotentialTargets & targets, const BattleExchangeEvaluator & utility)
	{
		std::ostringstream out;
		std::map<uint32_t, size_t> cheapValues;
		for(size_t i = 0; i < targets.possibleAttacks.size(); ++i)
		{
			const auto & ap = targets.possibleAttacks[i];
			const auto immediate = ap.attackValue();
			const auto weighted = ap.damageDiff(utility.getPositiveEffectMultiplier(), utility.getNegativeEffectMultiplier());
			EXPECT_TRUE(std::isfinite(immediate));
			EXPECT_TRUE(std::isfinite(weighted));
			++cheapValues[std::bit_cast<uint32_t>(weighted)];
			out << i << ':' << identity(ap) << ':' << std::bit_cast<uint32_t>(immediate)
				<< ':' << std::bit_cast<uint32_t>(weighted) << ';';
		}
		RecordProperty("CandidatePoolImmediateWeightedBits", out.str());
		RecordProperty("PositiveUtilityMultiplierBits", std::to_string(std::bit_cast<uint32_t>(utility.getPositiveEffectMultiplier())));
		RecordProperty("NegativeUtilityMultiplierBits", std::to_string(std::bit_cast<uint32_t>(utility.getNegativeEffectMultiplier())));
		RecordProperty("CandidateCount", std::to_string(targets.possibleAttacks.size()));
		RecordProperty("DuplicateWeightedValueGroups", std::to_string(std::count_if(cheapValues.begin(), cheapValues.end(),
			[](const auto & entry) { return entry.second > 1; })));
	}

	EvaluationResult fullMeleeReference(const std::shared_ptr<HypotheticBattle> & model,
		PotentialTargets & targets, DamageCache & original, std::string & ledger)
	{
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
		EvaluationResult result(targets.bestAction());
		forecastRows.clear();
		std::ostringstream out;
		const auto phase = [&](const std::shared_ptr<HypotheticBattle> & state, bool waited)
		{
			evaluator.updateReachabilityMap(state);
			for(size_t i = 0; i < targets.possibleAttacks.size(); ++i)
			{
				const auto & ap = targets.possibleAttacks[i];
				const float score = evaluator.evaluateExchange(ap, 0, targets, original, state);
				forecastRows.push_back({i, waited, score});
				const auto members = evaluator.getExchangeUnits(ap, 0, targets, state);
				EXPECT_TRUE(std::isfinite(score));
				out << waited << ':' << i << ':' << identity(ap) << ':' << std::bit_cast<uint32_t>(score) << '[';
				for(const auto & [turn, queue] : members.units)
				{
					out << turn << '=';
					for(const auto * unit : queue) out << unit->unitId() << ',';
					out << '/';
				}
				out << "];";
				const bool normalNearWait = !waited && result.wait && vstd::isAlmostEqual(score, result.score);
				if(score > result.score || normalNearWait)
				{
					result.bestAttack = ap;
					result.score = score;
					result.wait = waited;
				}
			}
		};
		if(!actor->acquireState()->waitedThisTurn && !actor->acquireState()->hadMorale)
		{
			auto waited = std::make_shared<HypotheticBattle>(environment.get(), model);
			waited->makeWait(actor);
			const auto before = units(*waited);
			phase(waited, true);
			EXPECT_EQ(units(*waited), before);
		}
		phase(model, false);
		ledger = out.str();
		return result;
	}


	std::vector<bool> checkReplay(const std::shared_ptr<HypotheticBattle> & model,
		PotentialTargets & targets, const EvaluationResult & full)
	{
		BattleExchangeEvaluator utility(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
		EvaluationResult replay(targets.bestAction());
		std::optional<size_t> waitedIncumbent;
		for(const auto & row : forecastRows)
			if(row.waited && row.score > replay.score)
			{
				replay.score = row.score;
				replay.bestAttack = targets.possibleAttacks[row.ordinal];
				replay.wait = true;
				waitedIncumbent = row.ordinal;
			}
		const auto mask = creatureCandidateScreening::normalMeleeForecasts(actor, targets, *model,
			false, utility.getPositiveEffectMultiplier(), utility.getNegativeEffectMultiplier(), waitedIncumbent);
		for(const auto & row : forecastRows)
			if(!row.waited && mask[row.ordinal]
				&& (row.score > replay.score || (vstd::isAlmostEqual(row.score, replay.score) && replay.wait)))
			{
				replay.score = row.score;
				replay.bestAttack = targets.possibleAttacks[row.ordinal];
				replay.wait = false;
			}
		std::ostringstream retained;
		bool winnerRetained = false;
		for(size_t i = 0; i < mask.size(); ++i)
		{
			if(mask[i]) retained << i << ',';
			if(identity(targets.possibleAttacks[i]) == identity(full.bestAttack))
				winnerRetained = full.wait || mask[i];
		}
		RecordProperty("NormalRetainedOriginalOrdinals", retained.str());
		RecordProperty("WaitedIncumbentOrdinal", waitedIncumbent ? std::to_string(*waitedIncumbent) : "none");
		RecordProperty("NormalRetainedCount", std::to_string(std::ranges::count(mask, true)));
		RecordProperty("FullWinnerRetained", winnerRetained ? "true" : "false");
		RecordProperty("CounterfactualReferenceMinusSelected", std::to_string(full.score - replay.score));
		RecordProperty("CounterfactualPositiveRegret", std::to_string(std::max(0.0f, full.score - replay.score)));
		RecordProperty("CounterfactualAttack", identity(replay.bestAttack));
		RecordProperty("CounterfactualWaited", replay.wait ? "true" : "false");
		RecordProperty("CounterfactualScoreBits", std::to_string(std::bit_cast<uint32_t>(replay.score)));
		EXPECT_TRUE(winnerRetained);
		EXPECT_EQ(identity(replay.bestAttack), identity(full.bestAttack));
		EXPECT_EQ(replay.wait, full.wait);
		EXPECT_EQ(std::bit_cast<uint32_t>(replay.score), std::bit_cast<uint32_t>(full.score));
		return mask;
	}

	BattleAction selectAction(const std::string & prefix)
	{
		const auto before = worldAndRng();
		BattleEvaluator evaluator(environment, callback, actor, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, strengthRatio, 2);
		BattleEvaluator::SelectionTiming timing;
		const auto action = evaluator.selectStackAction(actor, &timing);
		EXPECT_EQ(worldAndRng(), before);
		std::ostringstream bytes;
		for(const auto byte : projectedHitTest::serialized(action)) bytes << std::hex << std::to_integer<unsigned>(byte) << '.';
		RecordProperty(prefix + "SelectedActionBytes", bytes.str());
		RecordProperty(prefix + "SelectionMovementExchangeCalls", std::to_string(timing.movementDetails.findMoveExchange.calls));
		RecordProperty(prefix + "SelectionNormalExchangeCalls", std::to_string(timing.bestTargetDetails.normalExchange.calls));
		RecordProperty(prefix + "SelectionWaitedExchangeCalls", std::to_string(timing.bestTargetDetails.waitedExchange.calls));
		return action;
	}

	void selectAndSubmit()
	{
		const auto action = selectAction("Final");
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}
	void characterizeMovement(bool blocked)
	{
		const std::string prefix = blocked ? "Blocked" : "Open";
		auto model = simulation();
		DamageCache original;
		original.buildDamageCache(model, BattleSide::ATTACKER);
		const auto before = worldAndRng();
		const auto modelBefore = units(*model);
		PotentialTargets targets(actor, original, model);
		const auto ordinaryReach = model->getReachability(actor);
		std::ostringstream geometry;
		for(const auto * enemy : targets.unreachableEnemies)
		{
			uint32_t nearest = ReachabilityInfo::INFINITE_DIST;
			for(const auto hex : enemy->getAttackableHexes(actor))
				nearest = std::min(nearest, ordinaryReach.distances[hex.toInt()]);
			geometry << enemy->unitId() << ':' << enemy->getPosition().toInt()
				<< ":center=" << static_cast<int>(BattleHex::getDistance(actor->getPosition(), enemy->getPosition()))
				<< ":approach=" << nearest << ';';
			EXPECT_GT(nearest, actor->getMovementRange());
		}
		RecordProperty(prefix + "ActorNativeMovementRange", std::to_string(actor->getMovementRange()));
		RecordProperty(prefix + "NativeTargetDistances", geometry.str());
		ASSERT_TRUE(targets.possibleAttacks.empty());
		ASSERT_EQ(targets.unreachableEnemies.size(), blocked ? 3 : 2);
		const auto bypass = getReachabilityWithEnemyBypass(actor, original, model);
		ASSERT_TRUE(bypass.params.bypassEnemyStacks);
		if(blocked)
		{
			for(const auto hex : enemies[2]->getHexes())
				ASSERT_GT(bypass.params.destructibleEnemyTurns[hex.toInt()], 0);
		}
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
		BattleMovementTiming timing;
		const auto move = evaluator.findMoveTowardsUnreachable(actor, targets, original, model, &timing);
		ASSERT_FALSE(move.positions.empty());
		ASSERT_TRUE(move.cachedAttack);
		EXPECT_GT(timing.findMoveExchange.calls, 0);
		std::ostringstream positions;
		for(const auto hex : move.positions) positions << hex.toInt() << ',';
		RecordProperty(prefix + "MovementPositions", positions.str());
		RecordProperty(prefix + "MovementTarget", identity(*move.cachedAttack));
		RecordProperty(prefix + "MovementTurns", std::to_string(move.turnsToReach));
		RecordProperty(prefix + "MovementScoreBits", std::to_string(std::bit_cast<uint32_t>(move.score)));
		RecordProperty(prefix + "MovementExchangeCalls", std::to_string(timing.findMoveExchange.calls));
		RecordProperty(prefix + "MovementTargetProbeCalls", std::to_string(timing.findMoveExchangeDetails.targetProbes.calls));
		RecordProperty(prefix + "PerHexFullScoreTraceAvailable", "false");
		std::ostringstream predecessors;
		auto cursor = move.cachedAttack->from;
		for(int steps = 0; cursor.isValid() && steps < GameConstants::BFIELD_SIZE; ++steps)
		{
			predecessors << cursor.toInt() << ',';
			const auto next = bypass.predecessors.at(cursor.toInt());
			if(next == cursor) break;
			cursor = next;
		}
		RecordProperty(prefix + "SelectedFutureAttackNativeBypassPredecessors", predecessors.str());
		RecordProperty(prefix + "ExtraBypassExchangeBranchTraceAvailable", "false");
		EXPECT_EQ(units(*model), modelBefore);
		EXPECT_EQ(worldAndRng(), before);
	}

	void comparePreparedMelee()
	{
		const auto before = worldAndRng();
		auto fullModel = simulation();
		ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *fullModel));
		DamageCache fullDamage;
		fullDamage.buildDamageCache(fullModel, BattleSide::ATTACKER);
		PotentialTargets fullTargets(actor, fullDamage, fullModel);
		ASSERT_FALSE(fullTargets.possibleAttacks.empty());
		ASSERT_TRUE(fullTargets.unreachableEnemies.empty());
		ASSERT_TRUE(std::ranges::all_of(fullTargets.possibleAttacks, [](const auto & ap)
			{ return !ap.attack.shooting && ap.collateralDamageReduce == 0; }));
		BattleExchangeEvaluator poolUtility(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
		recordPool(fullTargets, poolUtility);
		std::string ledger;
		const auto full = fullMeleeReference(fullModel, fullTargets, fullDamage, ledger);
		RecordProperty("IssuedOrderFullLedger", ledger);
		auto partialModel = simulation();
		DamageCache partialDamage;
		partialDamage.buildDamageCache(partialModel, BattleSide::ATTACKER);
		PotentialTargets partialTargets(actor, partialDamage, partialModel);
		const auto modelBefore = units(*partialModel);
		const auto mask = checkReplay(partialModel, partialTargets, full);
		ASSERT_LT(std::ranges::count(mask, true), partialTargets.possibleAttacks.size());
		BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
		BattleBestTargetTiming timing;
		const auto selected = evaluator.findBestTarget(actor, partialTargets, partialDamage, partialModel, false, &timing);
		EXPECT_EQ(identity(selected.bestAttack), identity(full.bestAttack));
		EXPECT_EQ(selected.wait, full.wait);
		EXPECT_EQ(std::bit_cast<uint32_t>(selected.score), std::bit_cast<uint32_t>(full.score));
		EXPECT_EQ(timing.normalExchange.calls, std::ranges::count(mask, true));
		EXPECT_EQ(timing.waitedExchange.calls, partialTargets.possibleAttacks.size());
		EXPECT_EQ(units(*partialModel), modelBefore);
		EXPECT_EQ(worldAndRng(), before);
		// This fixture has only ordinary noncollateral melee and no unreachable
		// alternative: use the existing factory lowering for the full reference.
		ASSERT_GT(full.score, EvaluationResult::INEFFECTIVE_SCORE);
		auto expected = full.wait ? BattleAction::makeWait(actor)
			: BattleAction::makeMeleeAttack(actor, full.bestAttack.attack.defenderPos, full.bestAttack.from);
		expected.perfectMoment = full.bestAttack.perfectMoment;
		const auto action = selectAction("IssuedOrder");
		EXPECT_TRUE(action == expected);
		EXPECT_EQ(projectedHitTest::serialized(action), projectedHitTest::serialized(expected));
		EXPECT_EQ(worldAndRng(), before);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	}

	void characterizeIssuedOrder(HeroCommand command)
	{
		ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command)));
		const auto issued = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, command);
		ASSERT_TRUE(issued);
		ASSERT_TRUE(issued->hasScheduledRecipients(battle()->battleGetRound()));
		if(command == HeroCommand::VANGUARD)
		{
			const auto observed = actor->getAllBonuses(Selector::all);
			ASSERT_TRUE(std::ranges::any_of(*observed, [](const auto & bonus)
				{ return heroCommands::isVanguardBonus(bonus.get()) && bonus->turnsRemain > 0; }));
			const auto active = heroCommands::activeVanguardBonuses(actor);
			ASSERT_TRUE(active);
			RecordProperty("VanguardNativeGrantRoundsRemaining", std::to_string(active->roundsRemaining));
		}
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			ASSERT_FALSE(battle()->battleHasPendingDoubleCommand(side));
			ASSERT_FALSE(battle()->battleHasPendingPreCombatOrder(side));
			ASSERT_FALSE(battle()->getDoubleCommandState(side).secondWindReady());
		}
		RecordProperty("IssuedOrderCommand", heroCommands::key(command));
		ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
	}

};
}

TEST_F(CreatureCandidateBaselineTest, ReachableMeleeRecordsActualPoolAndOrderedFullReference)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto model = simulation();
	DamageCache original;
	original.buildDamageCache(model, BattleSide::ATTACKER);
	const auto before = worldAndRng();
	const auto modelBefore = units(*model);
	PotentialTargets targets(actor, original, model);
	ASSERT_FALSE(targets.berserk);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	ASSERT_TRUE(targets.unreachableEnemies.empty());
	ASSERT_TRUE(std::ranges::all_of(targets.possibleAttacks, [](const auto & ap)
	{
		return !ap.attack.shooting && !ap.completedReturnDestination;
	}));
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	recordPool(targets, evaluator);
	std::string ledger;
	const auto expected = fullMeleeReference(model, targets, original, ledger);
	const auto mask = checkReplay(model, targets, expected);
	BattleBestTargetTiming timing;
	const auto selected = evaluator.findBestTarget(actor, targets, original, model, false, &timing);
	EXPECT_EQ(identity(selected.bestAttack), identity(expected.bestAttack));
	EXPECT_EQ(selected.wait, expected.wait);
	EXPECT_EQ(std::bit_cast<uint32_t>(selected.score), std::bit_cast<uint32_t>(expected.score));
	EXPECT_EQ(timing.normalExchange.calls, std::ranges::count(mask, true));
	EXPECT_EQ(timing.waitedExchange.calls, actor->acquireState()->waitedThisTurn || actor->acquireState()->hadMorale
		? 0 : targets.possibleAttacks.size());
	RecordProperty("OrderedNormalWaitedFullScoreAndMembership", ledger);
	RecordProperty("SelectedFullAttack", identity(selected.bestAttack));
	RecordProperty("SelectedFullScoreBits", std::to_string(std::bit_cast<uint32_t>(selected.score)));
	RecordProperty("SelectedWaited", selected.wait ? "true" : "false");
	EXPECT_EQ(units(*model), modelBefore);
	EXPECT_EQ(worldAndRng(), before);
	ASSERT_NO_FATAL_FAILURE(selectAndSubmit());
}

TEST_F(CreatureCandidateBaselineTest, UnreachableMeleeRecordsMovementAndBlockerWithoutAPTraceClaims)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	ASSERT_NO_FATAL_FAILURE(characterizeMovement(false));
	(void)selectAction("Open");
	// New phase: authoritative addition, then a FRESH model/original cache.
	enemies[2] = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(10, 5), 80);
	for(const auto & unit : battle()->stacks)
		(void)unit->counterAttacks.available();
	ASSERT_NO_FATAL_FAILURE(recordEffectiveDomain());
	ASSERT_NO_FATAL_FAILURE(characterizeMovement(true));
	const auto action = selectAction("Blocked");
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
}

TEST_F(CreatureCandidateBaselineTest, HistoricalRangedWaitRetainsExistingFullSelector)
{
	historicalOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, true));
	ASSERT_FALSE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	ASSERT_FALSE(actor->acquireState()->waitedThisTurn);
	ASSERT_FALSE(actor->acquireState()->hadMorale);
	auto model = simulation();
	DamageCache original;
	original.buildDamageCache(model, BattleSide::ATTACKER);
	const auto before = worldAndRng();
	const auto modelBefore = units(*model);
	PotentialTargets targets(actor, original, model);
	ASSERT_FALSE(targets.possibleAttacks.empty());
	ASSERT_TRUE(std::ranges::all_of(targets.possibleAttacks, [](const auto & ap)
	{
		return ap.attack.shooting && !ap.defenderDead;
	}));
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	recordPool(targets, evaluator);
	evaluator.updateReachabilityMap(model);
	std::ostringstream threatGeometry;
	for(unsigned round = 0; round < 2; ++round)
		for(const auto * enemy : {enemies[0], enemies[1]})
		{
			const auto reach = model->getReachability(enemy);
			uint32_t closest = ReachabilityInfo::INFINITE_DIST;
			for(const auto hex : actor->getSurroundingHexes())
				if(reach.isReachable(hex)) closest = std::min(closest, reach.distances[hex.toInt()]);
			const auto nativeSpeed = enemy->getMovementRange(round);
			const auto radius = nativeSpeed * (round + 1);
			threatGeometry << enemy->unitId() << ':' << round << ":speed=" << nativeSpeed
				<< ":radius=" << radius << ":nearest=" << closest << ';';
			EXPECT_GT(closest, radius);
		}
	RecordProperty("NativeTwoRoundThreatGeometry", threatGeometry.str());
	RecordProperty("NearTargetNativeShotDistance", std::to_string(BattleHex::getDistance(actor->getPosition(), enemies[0]->getPosition())));
	RecordProperty("FarTargetNativeShotDistance", std::to_string(BattleHex::getDistance(actor->getPosition(), enemies[1]->getPosition())));
	for(const auto hex : actor->getSurroundingHexes())
		for(const auto * unit : evaluator.getOneTurnReachableUnits(0, hex))
			ASSERT_EQ(unit->unitSide(), actor->unitSide());
	ASSERT_TRUE(std::ranges::any_of(targets.possibleAttacks, [&](const auto & ap)
	{
		return model->battleHasShootingPenalty(actor, ap.dest);
	}));
	ASSERT_TRUE(std::ranges::any_of(targets.possibleAttacks, [&](const auto & ap)
	{
		return !model->battleHasShootingPenalty(actor, ap.dest);
	}));
	BattleBestTargetTiming timing;
	const auto selected = evaluator.findBestTarget(actor, targets, original, model, false, &timing);
	EXPECT_EQ(timing.waitedExchange.calls, targets.possibleAttacks.size());
	if(timing.normalExchange.calls == 0)
	{
		EXPECT_TRUE(selected.wait);
		EXPECT_TRUE(model->battleHasShootingPenalty(actor, selected.bestAttack.dest));
	}
	else
	{
		EXPECT_EQ(timing.normalExchange.calls, targets.possibleAttacks.size());
	}
	RecordProperty("SelectedFullAttack", identity(selected.bestAttack));
	RecordProperty("SelectedFullScoreBits", std::to_string(std::bit_cast<uint32_t>(selected.score)));
	RecordProperty("OutsideProposedCurrentMeleeDomain", "historical-orders/ranged");
	RecordProperty("ExistingShootingWaitEarlyReturn", timing.normalExchange.calls == 0 ? "true" : "false");
	RecordProperty("PerCandidateFullScoreTraceAvailable", "false-mixed-ranged-path-not-mirrored");
	EXPECT_EQ(units(*model), modelBefore);
	EXPECT_EQ(worldAndRng(), before);
	ASSERT_NO_FATAL_FAILURE(selectAndSubmit());
}

TEST_F(CreatureCandidateBaselineTest, NonzeroScalarSkillsAndStartingPerksCompareFreshPartialForecastToFull)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	for(auto * hero : {attackerSideHero, defenderSideHero})
	{
		hero->setPrimarySkill(PrimarySkill::ATTACK, 35, ChangeValueMode::ABSOLUTE);
		hero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:archery")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		hero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate")),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:offense")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	defenderSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:armorer")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:archery", "new-horizons:archery.targetCaller"));
	ASSERT_TRUE(defenderSideHero->hasActivePerk("new-horizons:archery", "new-horizons:archery.pointBlankShot"));
	const auto before = worldAndRng();
	auto referenceModel = simulation();
	DamageCache referenceDamage;
	referenceDamage.buildDamageCache(referenceModel, BattleSide::ATTACKER);
	PotentialTargets referenceTargets(actor, referenceDamage, referenceModel);
	ASSERT_FALSE(referenceTargets.possibleAttacks.empty());
	std::string ledger;
	const auto full = fullMeleeReference(referenceModel, referenceTargets, referenceDamage, ledger);
	RecordProperty("NonzeroScalarFullLedger", ledger);
	auto partialModel = simulation();
	ASSERT_EQ(partialModel->battleGetVisibleHeroPolicy(BattleSide::ATTACKER).kind(),
		VisibleHeroPolicy::Knowledge::VISIBLE_HERO);
	ASSERT_EQ(partialModel->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(),
		VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	DamageCache partialDamage;
	partialDamage.buildDamageCache(partialModel, BattleSide::ATTACKER);
	PotentialTargets partialTargets(actor, partialDamage, partialModel);
	ASSERT_EQ(partialTargets.possibleAttacks.size(), referenceTargets.possibleAttacks.size());
	for(size_t i = 0; i < partialTargets.possibleAttacks.size(); ++i)
		ASSERT_EQ(identity(partialTargets.possibleAttacks[i]), identity(referenceTargets.possibleAttacks[i]));
	const auto modelBefore = units(*partialModel);
	const auto mask = checkReplay(partialModel, partialTargets, full);
	// This is an opportunity oracle, not a timing claim. It must fail if the
	// chosen source domain quietly falls back or retains this entire pool.
	ASSERT_LT(std::ranges::count(mask, true), partialTargets.possibleAttacks.size());
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	BattleBestTargetTiming timing;
	const auto selected = evaluator.findBestTarget(actor, partialTargets, partialDamage, partialModel, false, &timing);
	EXPECT_EQ(identity(selected.bestAttack), identity(full.bestAttack));
	EXPECT_EQ(selected.wait, full.wait);
	EXPECT_EQ(std::bit_cast<uint32_t>(selected.score), std::bit_cast<uint32_t>(full.score));
	EXPECT_EQ(timing.normalExchange.calls, std::ranges::count(mask, true));
	EXPECT_EQ(timing.waitedExchange.calls, partialTargets.possibleAttacks.size());
	EXPECT_EQ(units(*partialModel), modelBefore);
	EXPECT_EQ(worldAndRng(), before);
	ASSERT_NO_FATAL_FAILURE(selectAndSubmit());
}

TEST(CreatureCandidateScreening, NegativePlateauAnchorsAndNonfiniteFallback)
{
	using creatureCandidateScreening::promisingNormalMelee;
	const std::array<float, 5> negative{-8.0f, -3.0f, -1.0f, -1.0f, -7.0f};
	EXPECT_EQ(promisingNormalMelee(negative, 4), (std::vector<bool>{true, false, true, true, true}));
	const std::array<float, 3> equal{0.0f, 0.0f, 0.0f};
	EXPECT_EQ(promisingNormalMelee(equal, std::nullopt), (std::vector<bool>{true, true, true}));
	const std::array<float, 3> unknown{5.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f};
	EXPECT_EQ(promisingNormalMelee(unknown, std::nullopt), (std::vector<bool>{true, true, true}));
	EXPECT_EQ(promisingNormalMelee(negative, negative.size()), std::vector<bool>(negative.size(), true));
}

TEST(CreatureCandidateScreening, ImmediateValueCannotBoundFutureExchange)
{
	const std::array<float, 3> immediate{8.0f, 6.0f, 3.0f};
	const auto mask = creatureCandidateScreening::promisingNormalMelee(immediate, std::nullopt);
	EXPECT_EQ(mask, (std::vector<bool>{true, false, false}));
	// A lower immediate attack can remove a blocker or avoid exposure. This
	// deterministic counterexample documents the deliberate approximation;
	// no choice-equivalence claim is inferred from the mask itself.
	const std::array<float, 3> futureExchange{1.0f, 9.0f, 2.0f};
	EXPECT_FALSE(mask[1]);
	EXPECT_EQ(*std::ranges::max_element(futureExchange) - futureExchange[0], 8.0f);
}

TEST_F(CreatureCandidateBaselineTest, TemporaryLifecycleAndLostCandidateProvenanceKeepEveryForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	auto model = simulation();
	DamageCache original;
	original.buildDamageCache(model, BattleSide::ATTACKER);
	PotentialTargets targets(actor, original, model);
	BattleExchangeEvaluator utility(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	const auto getMask = [&](bool siege)
	{
		return creatureCandidateScreening::normalMeleeForecasts(actor, targets, *model, siege,
			utility.getPositiveEffectMultiplier(), utility.getNegativeEffectMultiplier(), std::nullopt);
	};
	ASSERT_LT(std::ranges::count(getMask(false), true), targets.possibleAttacks.size());
	EXPECT_EQ(getMask(true), std::vector<bool>(targets.possibleAttacks.size(), true));
	const auto owner = targets.possibleAttacks.back().queryWorld;
	targets.possibleAttacks.back().queryWorld.reset();
	EXPECT_EQ(getMask(false), std::vector<bool>(targets.possibleAttacks.size(), true));
	targets.possibleAttacks.back().queryWorld = owner;
	Bonus temporary(BonusDuration::N_TURNS, BonusType::MORALE, BonusSource::OTHER, -1, BonusSourceID());
	temporary.turnsRemain = 1;
	model->addUnitBonus(actor->unitId(), {temporary});
	PotentialTargets timedTargets(actor, original, model);
	const auto fallback = creatureCandidateScreening::normalMeleeForecasts(actor, timedTargets, *model, false,
		utility.getPositiveEffectMultiplier(), utility.getNegativeEffectMultiplier(), std::nullopt);
	EXPECT_EQ(fallback, std::vector<bool>(timedTargets.possibleAttacks.size(), true));
}

TEST(CreatureCandidateScreening, NearTiePlateauIsAnchoredToMathematicalMaximum)
{
	const std::array<float, 4> immediate{0.0f, 100000.0f, 99999.5f, 99998.9f};
	EXPECT_TRUE(vstd::isAlmostEqual(immediate[1], immediate[2]));
	EXPECT_TRUE(vstd::isAlmostEqual(immediate[2], immediate[3]));
	EXPECT_FALSE(vstd::isAlmostEqual(immediate[1], immediate[3]));
	EXPECT_EQ(creatureCandidateScreening::promisingNormalMelee(immediate, std::nullopt),
		(std::vector<bool>{true, true, true, false}));
}

TEST_F(CreatureCandidateBaselineTest, NativeEdricCounterchargeAdmitsWithoutGriffinsAndStoredPoisonFallsBack)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
	const auto before = worldAndRng();
	auto fullModel = simulation();
	ASSERT_EQ(fullModel->battleGetVisibleHeroPolicy(BattleSide::DEFENDER).kind(), VisibleHeroPolicy::Knowledge::HIDDEN_UNKNOWN);
	ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *fullModel));
	DamageCache fullDamage;
	fullDamage.buildDamageCache(fullModel, BattleSide::ATTACKER);
	PotentialTargets fullTargets(actor, fullDamage, fullModel);
	std::string ledger;
	const auto full = fullMeleeReference(fullModel, fullTargets, fullDamage, ledger);
	RecordProperty("NativeEdricFullLedger", ledger);
	auto partialModel = simulation();
	DamageCache partialDamage;
	partialDamage.buildDamageCache(partialModel, BattleSide::ATTACKER);
	PotentialTargets partialTargets(actor, partialDamage, partialModel);
	const auto modelBefore = units(*partialModel);
	const auto mask = checkReplay(partialModel, partialTargets, full);
	ASSERT_LT(std::ranges::count(mask, true), partialTargets.possibleAttacks.size());
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	BattleBestTargetTiming timing;
	const auto selected = evaluator.findBestTarget(actor, partialTargets, partialDamage, partialModel, false, &timing);
	EXPECT_EQ(identity(selected.bestAttack), identity(full.bestAttack));
	EXPECT_EQ(selected.wait, full.wait);
	EXPECT_EQ(std::bit_cast<uint32_t>(selected.score), std::bit_cast<uint32_t>(full.score));
	EXPECT_EQ(timing.normalExchange.calls, std::ranges::count(mask, true));
	EXPECT_EQ(timing.waitedExchange.calls, partialTargets.possibleAttacks.size());
	EXPECT_EQ(units(*partialModel), modelBefore);
	EXPECT_EQ(worldAndRng(), before);
	// Physical poison is stored directly, with no supporting Bonus marker and
	// no requirement that its original poisoner remains alive.
	auto poisoned = partialModel->getForUpdate(enemies[0]->unitId());
	const auto bonusesBefore = poisoned->getAllBonuses(Selector::all)->size();
	ASSERT_TRUE(newHorizonsBulwark::applyPhysicalPoison(poisoned.get(), 8, -1));
	EXPECT_EQ(poisoned->getAllBonuses(Selector::all)->size(), bonusesBefore);
	EXPECT_FALSE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *partialModel));
	const auto poisonedMask = creatureCandidateScreening::normalMeleeForecasts(actor, partialTargets, *partialModel, false,
		evaluator.getPositiveEffectMultiplier(), evaluator.getNegativeEffectMultiplier(), std::nullopt);
	EXPECT_EQ(poisonedMask, std::vector<bool>(partialTargets.possibleAttacks.size(), true));
	for(const auto * creature : {"core:griffin", "core:royalGriffin"})
	{
		auto * aerial = addStack(BattleSide::ATTACKER, creatureByName(creature), BattleHex(3, 2), 10);
		auto aerialModel = simulation();
		EXPECT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *aerialModel));
		EXPECT_FALSE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(aerial, *aerialModel));
		removeOtherStacks({actor, reserve, enemies[0], enemies[1], enemies[2]});
	}
	ASSERT_NO_FATAL_FAILURE(selectAndSubmit());
}

namespace
{
class NativeEdricCreatureDecisionProfileTest : public CreatureCandidateBaselineTest
{
protected:
	void configureWorldInitialization(StartInfo & info, GameRandomizer & randomizer) const override
	{
		info.startTime = 1'700'000'000;
		randomizer.setSeed(1337);
	}
};
}

// Explicit isolated creature-decision observations; no Hero Action/callback/turn timing.
TEST_F(NativeEdricCreatureDecisionProfileTest, DISABLED_NullWholeCreatureDecisionTimingProfile)
{
	dynamic_cast<CRandomGenerator &>(*getRNG()).setSeed(1337);
	CRandomGenerator::getDefault().setSeed(1337);
	ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
	CRandomGenerator::getDefault().setSeed(1337);
	const auto before = worldAndRng();
	const auto digest = [](const std::vector<std::byte> & bytes)
	{
		return nhart::hex(nhart::sha256(std::span<const ui8>(
			reinterpret_cast<const ui8 *>(bytes.data()), bytes.size())));
	};
	const std::array<const char *, 4> captureNames{"World", "ServerRng", "GameRng", "DefaultRng"};
	for(size_t index = 0; index < before.size(); ++index)
	{
		RecordProperty(std::string("CreatureDecision") + captureNames[index] + "BeforeSHA256", digest(before[index]));
		RecordProperty(std::string("CreatureDecision") + captureNames[index] + "BeforeBytes", std::to_string(before[index].size()));
	}
	const auto run = [&](BattleEvaluator::SelectionTiming * timing)
	{
		// Fresh HB, damage cache and candidates each time; evaluator destruction
		// finishes inside the lambda, before the measured caller reads its clock.
		BattleEvaluator evaluator(environment, callback, actor, PlayerColor(0), BattleID(0),
			BattleSide::ATTACKER, 0.5f, 2);
		return evaluator.selectStackAction(actor, timing);
	};
	const auto warmup = run(nullptr);
	const auto expectedBytes = projectedHitTest::serialized(warmup);
	const auto targets = [&](const BattleAction & action)
	{
		std::vector<std::pair<int, int64_t>> result;
		for(const auto & target : action.getTarget(battle()))
			result.emplace_back(target.hexValue.toInt(), target.unitValue
				? static_cast<int64_t>(target.unitValue->unitId()) : -1);
		return result;
	};
	const auto expectedTargets = targets(warmup);
	ASSERT_EQ(worldAndRng(), before);
	std::ostringstream actionBytes;
	for(const auto byte : expectedBytes) actionBytes << std::hex << std::to_integer<unsigned>(byte) << '.';
	RecordProperty("CreatureDecisionExpectedActionBytes", actionBytes.str());
	RecordProperty("CreatureDecisionExpectedActionSHA256", digest(expectedBytes));
	RecordProperty("CreatureDecisionExpectedAction", warmup.toString());
	// Separate observer/count diagnostic. Its elapsed values are not samples.
	BattleEvaluator::SelectionTiming diagnostic;
	const auto diagnosticAction = run(&diagnostic);
	ASSERT_EQ(projectedHitTest::serialized(diagnosticAction), expectedBytes);
	ASSERT_TRUE(diagnosticAction == warmup);
	ASSERT_EQ(targets(diagnosticAction), expectedTargets);
	ASSERT_EQ(worldAndRng(), before);
	RecordProperty("CreatureDecisionDiagnosticNormalExchangeCalls", std::to_string(diagnostic.bestTargetDetails.normalExchange.calls));
	RecordProperty("CreatureDecisionDiagnosticWaitedExchangeCalls", std::to_string(diagnostic.bestTargetDetails.waitedExchange.calls));
	RecordProperty("CreatureDecisionDiagnosticMovementExchangeCalls", std::to_string(diagnostic.movementDetails.findMoveExchange.calls));
	EXPECT_GT(diagnostic.bestTargetDetails.normalExchange.calls + diagnostic.bestTargetDetails.waitedExchange.calls
		+ diagnostic.movementDetails.findMoveExchange.calls, 0u);
	for(unsigned index = 0; index < 4; ++index)
	{
		SCOPED_TRACE("NULLsample" + std::to_string(index));
		const auto started = std::chrono::steady_clock::now();
		const auto action = run(nullptr);
		const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - started).count();
		// Exact serialized state/RNG/action/target comparisons stay outside clocks.
		EXPECT_EQ(worldAndRng(), before);
		EXPECT_EQ(projectedHitTest::serialized(action), expectedBytes);
		EXPECT_TRUE(action == warmup);
		EXPECT_EQ(action.toString(), warmup.toString());
		EXPECT_EQ(targets(action), expectedTargets);
		RecordProperty("CreatureDecisionNullSample" + std::to_string(index) + "WallNs", std::to_string(elapsed));
	}
	RecordProperty("CreatureDecisionClockScope", "fresh BattleEvaluator construction/selectStackAction(NULL)/destruction; four samples; no HeroAction/fullcallback/turn");
	RecordProperty("CreatureDecisionPreparation", "current native fixture; world startTime1700000000; all seeds1337; one NULL warmup then one separately instrumented diagnostic; native/global caches retained");
	RecordProperty("CreatureDecisionForecast", "current NH; strengthRatio0.5; two-round horizon; fixture default difficulty unchanged; no timing threshold");
}

TEST_F(CreatureCandidateBaselineTest, IssuedCurrentChargeStillOmitsForecastsAndMatchesFullReference)
{
	ASSERT_NO_FATAL_FAILURE(characterizeIssuedOrder(HeroCommand::CHARGE));
}

TEST_F(CreatureCandidateBaselineTest, IssuedCurrentFeintStillOmitsForecastsAndMatchesFullReference)
{
	ASSERT_NO_FATAL_FAILURE(characterizeIssuedOrder(HeroCommand::FEINT));
}

TEST_F(CreatureCandidateBaselineTest, NativeActiveSecondWindRemainsOutsideOrdinaryScreening)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
	ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *simulation()));
	const auto activate = [&](CStack * unit)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = unit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	};
	activate(reserve);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(reserve)));
	ASSERT_TRUE(reserve->defended());
	activate(actor);
	ASSERT_EQ(battle()->battleGetHeroOrderTargetRejection(BattleSide::ATTACKER,
		HeroCommand::SECOND_WIND, {reserve->unitId()}), heroCommands::TargetRejection::NONE);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, reserve->unitId())));
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), reserve->unitId());
	const auto order = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(order);
	ASSERT_TRUE(order->secondWindActive);
	const auto before = worldAndRng();
	auto model = simulation();
	EXPECT_FALSE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(reserve, *model));
	RecordProperty("SecondWindNativeActivationOutputPercent", std::to_string(model->battleGetActivationOutputPercent(model->battleActiveUnit())));
	EXPECT_EQ(worldAndRng(), before);
}

TEST_F(CreatureCandidateBaselineTest, IssuedCurrentVanguardOmitsForecastsAndMatchesFullReference)
{
	ASSERT_NO_FATAL_FAILURE(characterizeIssuedOrder(HeroCommand::VANGUARD));
}

TEST_F(CreatureCandidateBaselineTest, ProjectedCurrentChargeContinuationMatchesFullReferenceWithOmissions)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
	const auto before = worldAndRng();
	auto fullModel = simulation();
	PossibleSpellcast candidate;
	candidate.command = HeroCommand::CHARGE;
	const auto admission = heroActionProjection::admitCandidate(*fullModel, BattleSide::ATTACKER, candidate);
	ASSERT_TRUE(admission.admitted);
	const auto completion = heroActionProjection::completeOrder(*fullModel,
		*callback->getBattle(BattleID(0)), BattleSide::ATTACKER, candidate, admission);
	ASSERT_TRUE(completion.accepted);
	ASSERT_TRUE(completion.continuation);
	ASSERT_EQ(completion.continuation->kind, HypotheticBattle::ProjectedOrderContinuation::Kind::CURRENT_ACTOR);
	ASSERT_TRUE(fullModel->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *fullModel));
	// Fork the already accepted projection before reference forecasts. Neither
	// child accepts another Hero Action or mutates its native source.
	auto partialModel = std::make_shared<HypotheticBattle>(environment.get(), fullModel);
	const auto fullBefore = units(*fullModel);
	DamageCache fullDamage;
	fullDamage.buildDamageCache(fullModel, BattleSide::ATTACKER);
	PotentialTargets fullTargets(actor, fullDamage, fullModel);
	BattleExchangeEvaluator poolUtility(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	recordPool(fullTargets, poolUtility);
	std::string ledger;
	const auto full = fullMeleeReference(fullModel, fullTargets, fullDamage, ledger);
	EXPECT_EQ(units(*fullModel), fullBefore);
	RecordProperty("ProjectedChargeFullLedger", ledger);
	DamageCache partialDamage;
	partialDamage.buildDamageCache(partialModel, BattleSide::ATTACKER);
	PotentialTargets partialTargets(actor, partialDamage, partialModel);
	const auto partialBefore = units(*partialModel);
	const auto mask = checkReplay(partialModel, partialTargets, full);
	ASSERT_LT(std::ranges::count(mask, true), partialTargets.possibleAttacks.size());
	BattleExchangeEvaluator evaluator(callback->getBattle(BattleID(0)), environment, strengthRatio, 2);
	BattleBestTargetTiming timing;
	const auto selected = evaluator.findBestTarget(actor, partialTargets, partialDamage, partialModel, false, &timing);
	EXPECT_EQ(identity(selected.bestAttack), identity(full.bestAttack));
	EXPECT_EQ(selected.wait, full.wait);
	EXPECT_EQ(std::bit_cast<uint32_t>(selected.score), std::bit_cast<uint32_t>(full.score));
	EXPECT_EQ(timing.normalExchange.calls, std::ranges::count(mask, true));
	EXPECT_EQ(timing.waitedExchange.calls, partialTargets.possibleAttacks.size());
	EXPECT_EQ(units(*partialModel), partialBefore);
	EXPECT_EQ(units(*fullModel), fullBefore);
	EXPECT_EQ(worldAndRng(), before);
}

TEST(CreatureCandidateScreening, LocalSpatialRefinementUsesSameDefenderAndOriginalAnchorsOnly)
{
	using Position = creatureCandidateScreening::MeleeCandidatePosition;
	const std::array<Position, 6> positions{{
		{4, BattleHex(5, 5)}, {4, BattleHex(6, 5)}, {4, BattleHex(7, 5)},
		{5, BattleHex(6, 5)}, {4, BattleHex(5, 5)}, {4, BattleHex(9, 5)}}};
	const std::vector<bool> original{true, false, false, false, false, false};
	const auto retained = creatureCandidateScreening::retainLocalMeleeAlternatives(positions, original);
	EXPECT_EQ(retained, (std::vector<bool>{true, true, false, false, true, false}));
	EXPECT_EQ(original, (std::vector<bool>{true, false, false, false, false, false}));
	// The observed Charge counterexample is adjacent, without interpreting contact sides.
	const std::array<Position, 2> charge{{{4, BattleHex(76)}, {4, BattleHex(59)}}};
	EXPECT_EQ(creatureCandidateScreening::retainLocalMeleeAlternatives(charge, {true, false}),
		(std::vector<bool>{true, true}));
}

TEST(CreatureCandidateScreening, LocalSpatialRefinementMalformedGeometryKeepsEveryCandidate)
{
	using Position = creatureCandidateScreening::MeleeCandidatePosition;
	const std::array<Position, 2> invalid{{{4, BattleHex(76)}, {4, BattleHex()}}};
	EXPECT_EQ(creatureCandidateScreening::retainLocalMeleeAlternatives(invalid, {true, false}),
		(std::vector<bool>{true, true}));
	const std::array<Position, 2> valid{{{4, BattleHex(76)}, {4, BattleHex(59)}}};
	EXPECT_EQ(creatureCandidateScreening::retainLocalMeleeAlternatives(valid, {true}),
		(std::vector<bool>{true, true}));
	EXPECT_EQ(creatureCandidateScreening::retainLocalMeleeAlternatives(valid, {false, false}),
		(std::vector<bool>{true, true}));
}

TEST_F(CreatureCandidateBaselineTest, VanguardDurationExceptionStillRejectsForeignTimedBonusesAndStatusMetadata)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeEdric());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::VANGUARD)));
	const auto before = worldAndRng();
	const auto observed = actor->getAllBonuses(Selector::all);
	const auto native = std::ranges::find_if(*observed, [](const auto & bonus)
		{ return heroCommands::isVanguardBonus(bonus.get()) && bonus->type == BonusType::PRIMARY_SKILL; });
	ASSERT_NE(native, observed->end());
	for(int variant = 0; variant < 3; ++variant)
	{
		SCOPED_TRACE(variant);
		auto model = simulation();
		ASSERT_TRUE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *model));
		auto modified = **native;
		// Distinct stacking preserves an observable root alongside the native grants.
		if(variant == 0) { modified.source = BonusSource::OTHER; modified.stacking = "foreign-timed"; }
		if(variant == 1) modified.statusIdentity = "screening-fallback";
		if(variant == 2) modified.statusTags = {BonusStatusTag::DEBUFF};
		model->addUnitBonus(actor->unitId(), {modified});
		const auto payloads = model->battleGetUnitByID(actor->unitId())->getAllBonuses(Selector::all);
		ASSERT_TRUE(std::ranges::any_of(*payloads, [variant](const auto & bonus)
			{ return variant == 0 ? bonus->stacking == "foreign-timed"
				: variant == 1 ? bonus->statusIdentity == "screening-fallback" : !bonus->statusTags.empty(); }));
		EXPECT_FALSE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *model));
		EXPECT_EQ(worldAndRng(), before);
	}
}

TEST_F(CreatureCandidateBaselineTest, NativeClaviusDeathKnightGroundUndeadMatchesFullReferenceWithOmissions)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:clavius", "core:skeleton"));
	ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:deathknight");
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:necromancy"))), MasteryLevel::BASIC);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:estates"))), MasteryLevel::BASIC);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:estates", "new-horizons:estates.taxCollector"));
	ASSERT_TRUE(actor->hasBonusOfType(BonusType::UNDEAD));
	ASSERT_FALSE(actor->hasBonusOfType(BonusType::FLYING));
	RecordProperty("ActualNativeHeroProfile", "core:clavius; DeathKnight; Necromancy/Estates/TaxCollector; ground Skeletons");
	ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
}

TEST_F(CreatureCandidateBaselineTest, NativeSorshaActiveExecutionerMatchesFullReferenceWithOmissions)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:sorsha", "core:pikeman"));
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"))), MasteryLevel::BASIC);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:offense"))), MasteryLevel::BASIC);
	ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:offense", "new-horizons:offense.executioner"));
	const BattleAttackInfo probe(actor, enemies[0], 0, false);
	const auto fullHealthDamage = battle()->calculateDmgRange(probe).damage;
	// Reuse the authoritative unit-state pack pattern from Offense specialty tests.
	int64_t wound = enemies[0]->getTotalHealth() * 61 / 100;
	auto wounded = enemies[0]->acquireState();
	wounded->damage(wound);
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	UnitChanges change(enemies[0]->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = wounded->save();
	change.healthDelta = -wound;
	update.changedStacks.push_back(std::move(change));
	gameHandler->sendAndApply(update);
	ASSERT_TRUE(actor->alive());
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
	ASSERT_LT(enemies[0]->getAvailableHealth() * 100, enemies[0]->getTotalHealth() * 40);
	ASSERT_EQ(enemies[1]->getAvailableHealth(), enemies[1]->getTotalHealth());
	const auto woundedDamage = battle()->calculateDmgRange(probe).damage;
	ASSERT_GT(woundedDamage.min, fullHealthDamage.min);
	ASSERT_GT(woundedDamage.max, fullHealthDamage.max);
	RecordProperty("ActualNativeHeroProfile", "core:sorsha; DivineMandate/Offense/Executioner; one below40% target and full-health alternatives");
	RecordProperty("NativeExecutionerFullHealthMinDamage", std::to_string(fullHealthDamage.min));
	RecordProperty("NativeExecutionerWoundedMinDamage", std::to_string(woundedDamage.min));
	ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
}

TEST_F(CreatureCandidateBaselineTest, CurrentNativeCaptureRoundTripMatchesFullReferenceWithOmissions)
{
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:clavius", "core:skeleton", true));
	ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
}

TEST_F(CreatureCandidateBaselineTest, HistoricalNativeCaptureAbsenceRoundTripMatchesFullReferenceWithOmissions)
{
	historicalHeroCapture = true;
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:clavius", "core:skeleton", true));
	for(const auto * hero : {attackerSideHero, defenderSideHero})
	{
		EXPECT_FALSE(hero->getPerkState().rules.Struct().contains("quickStudyPolicy"));
		EXPECT_FALSE(hero->getPrimaryGrowthRules().Struct().contains("additionalSpecialties"));
		EXPECT_NE(hero->getPerkState().rules,
			LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS));
	}
	RecordProperty("HistoricalCaptureQualification", "authored predecessor absence; native init/full-world load/resave; not an earned Day115 fixture");
	ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
}

TEST_F(CreatureCandidateBaselineTest, RevisedCapturedCoefficientRoundTripMatchesFullReferenceWithOmissions)
{
	revisedCoefficientCapture = true;
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:clavius", "core:skeleton", true));
	ASSERT_TRUE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	const auto & defaults = LIBRARY->engineSettings()->getValue(EGameSettings::COMBAT_HERO_COMMANDS);
	EXPECT_NE(battle()->getHeroCommandRules(), defaults);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::FEINT)));
	const auto issued = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::FEINT);
	ASSERT_TRUE(issued);
	ASSERT_TRUE(issued->revision);
	ASSERT_TRUE(issued->revision->potencyFinalized);
	ASSERT_TRUE(issued->hasScheduledRecipients(battle()->battleGetRound()));
	const auto & captured = battle()->getHeroCommandRules()["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"];
	const int activeReduction = heroCommands::revisedFeintReductionFor(*issued, actor->unitId());
	// Native finalization must respect the captured cap. This active grant is
	// below even the installed formula's base; merely differing JSON cannot pass.
	ASSERT_GT(activeReduction, 0);
	EXPECT_EQ(issued->revision->feintCap, captured["cap"].Integer());
	EXPECT_LE(activeReduction, captured["cap"].Integer());
	EXPECT_LT(activeReduction, defaults["commands"]["feint"]["effects"]["retaliationDamageReductionPercent"]["base"].Integer());
	ASSERT_EQ(battle()->battleActiveUnit()->unitId(), actor->unitId());
	RecordProperty("CapturedCoefficientActiveFeintReduction", std::to_string(activeReduction));
	ASSERT_NO_FATAL_FAILURE(comparePreparedMelee());
}

TEST_F(CreatureCandidateBaselineTest, LegacyOrderRepresentationRoundTripRemainsOutsideOrdinaryScreening)
{
	historicalOrders = true;
	ASSERT_NO_FATAL_FAILURE(prepareNativeProfile("core:clavius", "core:skeleton", true));
	ASSERT_FALSE(heroCommands::isRevisedRules(battle()->getHeroCommandRules()));
	const auto before = worldAndRng();
	EXPECT_FALSE(creatureCandidateScreening::supportsVisibleOrdinaryMelee(actor, *simulation()));
	EXPECT_EQ(worldAndRng(), before);
}
