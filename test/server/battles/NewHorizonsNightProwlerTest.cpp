/*
 * NewHorizonsNightProwlerTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/BattleHexArray.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsShroud.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>
#include <vstd/ContainerUtils.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
const BattleHex corridorStart(8, 2);
const BattleHex hostileTransitHex(8, 5);
const BattleHex corridorAttackPosition(8, 7);
const BattleHex corridorTargetPosition(9, 7);
const BattleHex alternateAttackPosition(10, 7);

class NightProwlerEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit NightProwlerEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsNightProwlerTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		for(auto & perk : rules["skills"][std::string(newHorizonsShroud::SKILL_ID)]["perks"].Vector())
		{
			if(perk["id"].String() != newHorizonsShroud::NIGHT_PROWLER_PERK_ID)
				continue;
			const auto status = perk["effect"]["status"].String();
			if(status == "active")
				return;
			if(status != "planned")
				throw std::runtime_error("Night Prowler must be planned or active in canonical content");
			perk["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(rules));
			return;
		}
		throw std::runtime_error("Missing Night Prowler registry entry");
	}

	HeroTypeID heroTypeForFaction(FactionID faction) const
	{
		for(const auto & id : LIBRARY->heroh->getDefaultAllowed())
		{
			const auto * hero = dynamic_cast<const CHero *>(id.toHeroType());
			if(hero && hero->heroClass && hero->heroClass->faction == faction)
				return id;
		}
		throw std::runtime_error("No default-allowed hero found for the requested fixture faction");
	}

	void startGameWithDungeonHero()
	{
		const auto dungeonHero = heroTypeForFaction(FactionID::DUNGEON);
		const auto otherHero = heroTypeForFaction(FactionID::CASTLE);
		const CreatureID token(0);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, dungeonHero, PlayerColor(0)).heroGarrison({{token, 1}})
			.hero({7, 7, 0}, otherHero, PlayerColor(1)).heroGarrison({{token, 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroClass()->faction, FactionID::DUNGEON);
	}

	void acceptPerk(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, offerSeed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == newHorizonsShroud::SKILL_ID
					&& offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, offerSeed, false);
					ASSERT_TRUE(hero->hasActivePerk(std::string(newHorizonsShroud::SKILL_ID), std::string(perkId)));
					return;
				}
			}
		}
		FAIL() << "No legal Shroud of Malassa offer for " << perkId;
	}

	void selectNightProwler(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(std::string(newHorizonsShroud::SKILL_ID));
		ASSERT_GE(decoded, 0);
		const SecondarySkill shroud(decoded);
		hero->setSecSkillLevel(shroud, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_FALSE(newHorizonsShroud::hasNightProwler(hero));
		acceptPerk(hero, newHorizonsShroud::BACKSTAB_PERK_ID);
		ASSERT_FALSE(newHorizonsShroud::hasNightProwler(hero));
		hero->setSecSkillLevel(shroud, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerk(hero, newHorizonsShroud::NIGHT_PROWLER_PERK_ID);
		ASSERT_TRUE(newHorizonsShroud::hasNightProwler(hero));
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}

	void addCorridorWall(int32_t uniqueId, const BattleHexArray & openings)
	{
		BattleHexArray tiles;
		for(int x = 1; x < GameConstants::BFIELD_WIDTH - 1; ++x)
		{
			const BattleHex hex(x, hostileTransitHex.getY());
			if(!openings.contains(hex))
				tiles.insert(hex);
		}

		SpellCreatedObstacle obstacle;
		obstacle.uniqueID = uniqueId;
		obstacle.ID = SpellID::FORCE_FIELD;
		obstacle.trigger = SpellID::NONE;
		obstacle.pos = tiles.front();
		obstacle.casterSide = BattleSide::NONE;
		obstacle.turnsRemaining = 3;
		obstacle.passable = false;
		obstacle.trap = false;
		obstacle.hidden = false;
		obstacle.nativeVisible = false;
		obstacle.customSize.insert(tiles);

		BattleObstaclesChanged packet;
		packet.battleID = BattleID(0);
		packet.change = ObstacleChanges(uniqueId, BattleChanges::EOperation::ADD);
		obstacle.toInfo(packet.change);
		gameHandler->sendAndApply(packet);
	}

	int64_t baselineDamageAt(const std::shared_ptr<HypotheticBattle> & model,
		const CStack * attacker, const CStack * target, const BattleHex & position)
	{
		auto baseline = std::make_shared<HypotheticBattle>(model->env, model);
		auto projectedAttacker = baseline->getForUpdate(attacker->unitId());
		auto projectedTarget = baseline->getForUpdate(target->unitId());
		projectedAttacker->setPosition(position);
		BattleAttackInfo ordinary(projectedAttacker.get(), projectedTarget.get(), 0, false);
		ordinary.attackerPos = position;
		ordinary.defenderPos = projectedTarget->getPosition();
		return baseline->battleExpectedLuckDamage(ordinary);
	}

	int64_t firstStrikeDamage(const AttackPossibility & possibility, uint32_t targetId, size_t strikeIndex)
	{
		if(strikeIndex >= possibility.fortuneStrikes.size())
			return -1;
		const auto & hits = possibility.fortuneStrikes[strikeIndex].hits;
		const auto hit = std::ranges::find_if(hits, [targetId](const auto & candidate)
		{
			return candidate.first == targetId;
		});
		return hit == hits.end() ? -1 : hit->second;
	}

	bool act(const battle::Unit * stack, const BattleAction & action)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(stack->unitSide()), action);
	}

	void activate(const CStack * stack)
	{
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = stack->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), stack->unitId());
	}

	TConstBonusListPtr nightProwlerBonuses(const battle::Unit * unit) const
	{
		return unit->getAllBonuses(CSelector(newHorizonsShroud::isNightProwlerBonus));
	}

	std::vector<int64_t> acceptedPrimaryDamageFrom(
		std::size_t beginIndex, const CStack * attacker, const CStack * target) const
	{
		std::vector<int64_t> result;
		for(auto it = server.attacks.begin() + static_cast<std::ptrdiff_t>(beginIndex); it != server.attacks.end(); ++it)
		{
			if(it->counter() || it->stackAttacking != attacker->unitId())
				continue;
			const auto hit = std::ranges::find(it->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
			if(hit != it->bsa.end())
				result.push_back(hit->damageAmount);
		}
		return result;
	}
};
}

TEST_F(NewHorizonsNightProwlerTest, LegalAdvancedPerkRewardsACommittedEnemyTransitOnItsFirstAttack)
{
	startGameWithDungeonHero();
	selectNightProwler(attackerSideHero);
	const auto pikeman = creatureByName("core:pikeman");
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HERO_GRANTS_ATTACKS, BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(pikeman)));

	startBattle();
	removeStartingUnits();
	auto * mover = addStack(BattleSide::ATTACKER, pikeman, corridorStart, 10);
	auto * hostileBlocker = addStack(BattleSide::DEFENDER,
		creatureByName("core:peasant"), hostileTransitHex, 1);
	auto * target = addStack(BattleSide::DEFENDER,
		creatureByName("core:peasant"), corridorTargetPosition, 1000);
	auto * reserve = addStack(BattleSide::ATTACKER,
		creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(mover, nullptr);
	ASSERT_NE(hostileBlocker, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(reserve, nullptr);
	mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::STACKS_SPEED, BonusSource::OTHER, 20, BonusSourceID()));
	forceMaximumDamage(mover);
	blockRetaliation(mover);
	BattleHexArray openings;
	openings.insert(hostileTransitHex);
	openings.insert(BattleHex(10, hostileTransitHex.getY()));
	addCorridorWall(317, openings);
	beginCombat();

	const auto [path, movementCost] = battle()->getPath(corridorStart, corridorAttackPosition, mover);
	ASSERT_GT(movementCost, 0);
	ASSERT_NE(std::ranges::find(path, hostileTransitHex), path.end())
		<< "The wall leaves the hostile stack's footprint as the only traversable crossing";
	BattleHexArray committedPath;
	for(const auto & hex : path)
		committedPath.insert(hex);
	EXPECT_TRUE(battle()->battleNightProwlerCrossesEnemy(mover, committedPath));
	const auto [alternatePath, alternateCost] = battle()->getPath(corridorStart, alternateAttackPosition, mover);
	ASSERT_GT(alternateCost, 0);
	BattleHexArray alternateCommittedPath;
	for(const auto & hex : alternatePath)
		alternateCommittedPath.insert(hex);
	EXPECT_FALSE(battle()->battleNightProwlerCrossesEnemy(mover, alternateCommittedPath))
		<< "The second legal route opens beside the hostile stack and does not cross its footprint";

	BattleAttackInfo unbuffed(mover, target, movementCost, false);
	unbuffed.attackerPos = corridorAttackPosition;
	unbuffed.defenderPos = corridorTargetPosition;
	const auto unbuffedRange = battle()->calculateDmgRange(unbuffed).damage;
	ASSERT_GT(unbuffedRange.max, 0);

	NightProwlerEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	auto projectedMover = model->getForUpdate(mover->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	BattleAttackInfo crossingAttack(projectedMover.get(), projectedTarget.get(), 0, false);
	const auto crossingPreview = AttackPossibility::evaluate(
		crossingAttack, corridorAttackPosition, cache, model);
	ASSERT_GE(crossingPreview.fortuneStrikes.size(), 2u);
	EXPECT_TRUE(battle()->battleNightProwlerCrossesEnemy(mover, committedPath));
	EXPECT_GT(firstStrikeDamage(crossingPreview, target->unitId(), 0),
		baselineDamageAt(model, mover, target, corridorAttackPosition))
		<< "The detached candidate applies the one-strike bonus after its hostile crossing";
	EXPECT_EQ(firstStrikeDamage(crossingPreview, target->unitId(), 1),
		baselineDamageAt(model, mover, target, corridorAttackPosition))
		<< "The candidate's follow-up strike is already unboosted";
	auto alternateAttack = BattleAttackInfo(projectedMover.get(), projectedTarget.get(), 0, false);
	const auto alternatePreview = AttackPossibility::evaluate(
		alternateAttack, alternateAttackPosition, cache, model);
	ASSERT_NE(alternatePreview.attackerState, nullptr);
	const auto alternateBonusPair = nightProwlerBonuses(alternatePreview.attackerState.get());
	EXPECT_TRUE(!alternateBonusPair || alternateBonusPair->empty());
	alternateAttack.attackerPos = alternateAttackPosition;
	alternateAttack.defenderPos = corridorTargetPosition;
	EXPECT_EQ(model->battleExpectedLuckDamage(alternateAttack),
		baselineDamageAt(model, mover, target, alternateAttackPosition))
		<< "A route which ends adjacent without crossing an enemy receives no candidate bonus";
	const auto modelTargetHealth = model->getForUpdate(target->unitId())->getAvailableHealth();
	const auto modelMoverPosition = model->getForUpdate(mover->unitId())->getPosition();
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	auto selected = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant replay;
	replay.trackAttack(crossingPreview, selected, cache);
	EXPECT_EQ(model->getForUpdate(mover->unitId())->getPosition(), modelMoverPosition);
	EXPECT_EQ(model->getForUpdate(target->unitId())->getAvailableHealth(), modelTargetHealth);
	EXPECT_EQ(sibling->getForUpdate(mover->unitId())->getPosition(), modelMoverPosition);
	EXPECT_EQ(sibling->getForUpdate(target->unitId())->getAvailableHealth(), modelTargetHealth);
	EXPECT_EQ(selected->getForUpdate(mover->unitId())->getPosition(), corridorAttackPosition);
	EXPECT_LT(selected->getForUpdate(target->unitId())->getAvailableHealth(), modelTargetHealth);

	activate(mover);
	const auto firstAttackIndex = server.attacks.size();
	const auto action = BattleAction::makeMeleeAttack(mover, target->getPosition(), corridorAttackPosition, false);
	ASSERT_EQ(action.actionType, EActionType::WALK_AND_ATTACK);
	ASSERT_TRUE(act(mover, action)) << "This is the accepted WALK_AND_ATTACK path through the hostile footprint";
	EXPECT_EQ(mover->getPosition(), corridorAttackPosition);
	ASSERT_TRUE(target->alive());
	const auto directHits = acceptedPrimaryDamageFrom(firstAttackIndex, mover, target);
	ASSERT_EQ(directHits.size(), 2u) << "The legal extra-strike bonus distinguishes the first attack from the follow-up";
	EXPECT_GT(directHits.front(), unbuffedRange.max)
		<< "The first committed attack uses Night Prowler's +10% damage";
	EXPECT_GT(directHits.front(), directHits.back())
		<< "UNTIL_ATTACK consumes the crossing bonus before the second attack";
	const auto remainingBonuses = nightProwlerBonuses(mover);
	EXPECT_TRUE(!remainingBonuses || remainingBonuses->empty());
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Night Prowler:") != std::string::npos
			&& line.find("+10% damage on its next attack this activation") != std::string::npos;
	})) << "The accepted crossing is reported in combat feedback";
}

TEST_F(NewHorizonsNightProwlerTest, EnemyTransitWithoutAnAttackExpiresUnusedAndFriendlyTransitDoesNotGrant)
{
	startGameWithDungeonHero();
	selectNightProwler(attackerSideHero);
	startBattle();
	removeStartingUnits();
	auto * enemyTransitMover = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), corridorStart, 5);
	auto * enemyBlocker = addStack(BattleSide::DEFENDER,
		creatureByName("core:peasant"), hostileTransitHex, 1);
	auto * friendlyTransitMover = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 2), 5);
	auto * friendlyBlocker = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(3, 5), 1);
	auto * adjacentMover = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(10, 2), 5);
	auto * target = addStack(BattleSide::DEFENDER,
		creatureByName("core:peasant"), corridorTargetPosition, 100);
	auto * flyingMover = addStack(BattleSide::ATTACKER,
		creatureByName("core:pikeman"), BattleHex(13, 2), 5);
	auto * secondEnemyBlocker = addStack(BattleSide::DEFENDER,
		creatureByName("core:peasant"), BattleHex(13, 5), 1);
	auto * reserve = addStack(BattleSide::DEFENDER,
		creatureByName("core:zombie"), BattleHex(4, 9), 1);
	ASSERT_NE(enemyTransitMover, nullptr);
	ASSERT_NE(enemyBlocker, nullptr);
	ASSERT_NE(friendlyTransitMover, nullptr);
	ASSERT_NE(friendlyBlocker, nullptr);
	ASSERT_NE(adjacentMover, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(flyingMover, nullptr);
	ASSERT_NE(secondEnemyBlocker, nullptr);
	ASSERT_NE(reserve, nullptr);
	for(auto * mover : {enemyTransitMover, friendlyTransitMover, adjacentMover, flyingMover})
		mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 20, BonusSourceID()));
	flyingMover->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::FLYING, BonusSource::OTHER, 0, BonusSourceID()));
	BattleHexArray openings;
	openings.insert(hostileTransitHex);
	openings.insert(BattleHex(3, hostileTransitHex.getY()));
	openings.insert(BattleHex(10, hostileTransitHex.getY()));
	openings.insert(BattleHex(13, hostileTransitHex.getY()));
	addCorridorWall(318, openings);
	beginCombat();

	const BattleHex enemyDestination(8, 8);
	const auto [enemyPath, enemyCost] = battle()->getPath(enemyTransitMover->getPosition(), enemyDestination, enemyTransitMover);
	ASSERT_GT(enemyCost, 0);
	BattleHexArray enemyCommittedPath;
	for(const auto & hex : enemyPath)
		enemyCommittedPath.insert(hex);
	ASSERT_TRUE(battle()->battleNightProwlerCrossesEnemy(enemyTransitMover, enemyCommittedPath));
	const auto priorLogCount = server.battleLogLines.size();
	activate(enemyTransitMover);
	ASSERT_TRUE(act(enemyTransitMover, BattleAction::makeMove(enemyTransitMover, enemyDestination)));
	EXPECT_EQ(enemyTransitMover->getPosition(), enemyDestination);
	const auto expired = nightProwlerBonuses(enemyTransitMover);
	EXPECT_TRUE(!expired || expired->empty())
		<< "The accepted movement-only activation expires an unused next-attack reward";
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines.begin() + static_cast<std::ptrdiff_t>(priorLogCount),
		server.battleLogLines.end(), [](const auto & line)
	{
		return line.find("Night Prowler:") != std::string::npos
			&& line.find("gains +10% damage on its next attack this activation") != std::string::npos;
	})) << "The accepted crossing grants a reward that the unused activation then expires";

	const BattleHex friendlyStart(3, 2);
	const BattleHex friendlyDestination(3, 8);
	const auto [friendlyPath, friendlyCost] = battle()->getPath(friendlyStart, friendlyDestination, friendlyTransitMover);
	ASSERT_GT(friendlyCost, 0);
	BattleHexArray friendlyCommittedPath;
	for(const auto & hex : friendlyPath)
		friendlyCommittedPath.insert(hex);
	EXPECT_FALSE(battle()->battleNightProwlerCrossesEnemy(friendlyTransitMover, friendlyCommittedPath));
	activate(friendlyTransitMover);
	ASSERT_TRUE(act(friendlyTransitMover, BattleAction::makeMove(friendlyTransitMover, friendlyDestination)));
	const auto friendlyBonuses = nightProwlerBonuses(friendlyTransitMover);
	EXPECT_TRUE(!friendlyBonuses || friendlyBonuses->empty())
		<< "Passing through a friendly occupied footprint does not earn Night Prowler";

	const BattleHex adjacentStart(10, 2);
	const BattleHex adjacentDestination(10, 7);
	const auto [adjacentPath, adjacentCost] = battle()->getPath(adjacentStart, adjacentDestination, adjacentMover);
	ASSERT_GT(adjacentCost, 0);
	BattleHexArray adjacentCommittedPath;
	for(const auto & hex : adjacentPath)
		adjacentCommittedPath.insert(hex);
	EXPECT_FALSE(battle()->battleNightProwlerCrossesEnemy(adjacentMover, adjacentCommittedPath));
	activate(adjacentMover);
	ASSERT_TRUE(act(adjacentMover, BattleAction::makeMove(adjacentMover, adjacentDestination)));
	const auto adjacentBonuses = nightProwlerBonuses(adjacentMover);
	EXPECT_TRUE(!adjacentBonuses || adjacentBonuses->empty())
		<< "Ending adjacent to an enemy without crossing its footprint is not sufficient";

	BattleHexArray flyingCrossingPath;
	flyingCrossingPath.insert(flyingMover->getPosition());
	flyingCrossingPath.insert(secondEnemyBlocker->getPosition());
	flyingCrossingPath.insert(BattleHex(13, 8));
	EXPECT_FALSE(battle()->battleNightProwlerCrossesEnemy(flyingMover, flyingCrossingPath))
		<< "Flying movement is not Ghost Walk transit for Night Prowler";
}

TEST_F(NewHorizonsNightProwlerTest, BonusesAreTypedAndRoundTripWithoutWholeBattleSaveClaims)
{
	const auto bonuses = newHorizonsShroud::nightProwlerDamageBonuses();
	ASSERT_EQ(bonuses.size(), 2u);
	for(size_t index = 0; index < bonuses.size(); ++index)
	{
		const auto & original = bonuses[index];
		EXPECT_TRUE(newHorizonsShroud::isNightProwlerBonus(&original));
		EXPECT_EQ(original.duration, BonusDuration::STACK_ACTIVATION | BonusDuration::UNTIL_ATTACK);
		EXPECT_EQ(original.type, BonusType::PERCENTAGE_DAMAGE_BOOST);
		EXPECT_EQ(original.val, newHorizonsShroud::NIGHT_PROWLER_DAMAGE_PERCENT);
		EXPECT_EQ(original.source, BonusSource::SECONDARY_SKILL);
		EXPECT_FALSE(original.stacking.empty());
		EXPECT_EQ(original.subtype, index == 0
			? BonusCustomSubtype::damageTypeMelee : BonusCustomSubtype::damageTypeRanged);

		CMemorySerializer current;
		current.oser & original;
		Bonus restored;
		current.iser & restored;
		EXPECT_TRUE(newHorizonsShroud::isNightProwlerBonus(&restored));
		EXPECT_EQ(restored.duration, original.duration);
		EXPECT_EQ(restored.type, original.type);
		EXPECT_EQ(restored.subtype, original.subtype);
		EXPECT_EQ(restored.val, original.val);
		EXPECT_EQ(restored.source, original.source);
		EXPECT_EQ(restored.sid, original.sid);
		EXPECT_EQ(restored.stacking, original.stacking);
		EXPECT_EQ(restored.description.toString(LIBRARY->staticTexts()),
			original.description.toString(LIBRARY->staticTexts()));

	}
	EXPECT_NE(bonuses[0].stacking, bonuses[1].stacking);
}
