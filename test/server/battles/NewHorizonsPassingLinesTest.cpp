/*
 * NewHorizonsPassingLinesTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <vcmi/Environment.h>
#include <vstd/ContainerUtils.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view battlecraftSkill = "new-horizons:battlecraft";
constexpr std::string_view entrenchPerk = "new-horizons:battlecraft.entrench";
constexpr std::string_view passingLinesPerk = "new-horizons:battlecraft.passingLines";

const BattleHex corridorStart(8, 2);
const BattleHex corridorBlockerPosition(8, 5);
const BattleHex corridorDestination(8, 8);

class PassingLinesEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit PassingLinesEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsPassingLinesTest : public BattleTestFixture
{
protected:
	CStack * mover = nullptr;
	CStack * blocker = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the separate native New Horizons preset";
	}

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	bool offerContains(CGHeroInstance * hero, std::string_view perkId) const
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			if(std::ranges::any_of(offers, [perkId](const auto & offer)
				{
					return offer.selection.skillId == battlecraftSkill
						&& offer.selection.perkId == perkId;
				}))
				return true;
		}
		return false;
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [perkId](const auto & offer)
			{
				return offer.selection.skillId == battlecraftSkill
					&& offer.selection.perkId == perkId;
			});
			if(selected == offers.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offers.begin(), selected));
			gameHandler->levelUpHero(hero, offers, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(battlecraftSkill), std::string(perkId)));
			return;
		}
		FAIL() << "Perk never appeared in a legal Battlecraft offer: " << perkId;
	}

	void selectPassingLines(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(std::string(battlecraftSkill));
		ASSERT_GE(decoded, 0);
		const SecondarySkill skill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, entrenchPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, passingLinesPerk);
		ASSERT_TRUE(hero->hasActivePerk(std::string(battlecraftSkill), std::string(passingLinesPerk)));
	}

	void removeStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!changes.changedStacks.empty())
			gameHandler->sendAndApply(changes);
	}

	void addObstacle(int32_t uniqueId, SpellID id, SpellID trigger, BattleSide casterSide,
		bool passable, bool trap, bool hidden, const BattleHexArray & tiles, int damage = 0)
	{
		ASSERT_FALSE(tiles.empty());
		SpellCreatedObstacle obstacle;
		obstacle.uniqueID = uniqueId;
		obstacle.ID = id;
		obstacle.trigger = trigger;
		obstacle.pos = tiles.front();
		obstacle.casterSide = casterSide;
		obstacle.turnsRemaining = 3;
		obstacle.passable = passable;
		obstacle.trap = trap;
		obstacle.hidden = hidden;
		obstacle.nativeVisible = false;
		obstacle.damageSnapshot = damage > 0;
		obstacle.minimalDamage = damage;
		obstacle.customSize.insert(tiles);

		BattleObstaclesChanged packet;
		packet.battleID = BattleID(0);
		packet.change = ObstacleChanges(uniqueId, BattleChanges::EOperation::ADD);
		obstacle.toInfo(packet.change);
		gameHandler->sendAndApply(packet);
	}

	void addCorridorWall(int32_t uniqueId, const BattleHexArray & opening)
	{
		BattleHexArray tiles;
		for(int x = 1; x < GameConstants::BFIELD_WIDTH - 1; ++x)
		{
			const BattleHex hex(x, corridorBlockerPosition.getY());
			if(!opening.contains(hex))
				tiles.insert(hex);
		}
		addObstacle(uniqueId, SpellID::FORCE_FIELD, SpellID::NONE,
			BattleSide::NONE, false, false, false, tiles);
	}

	void prepareCorridor(bool doubleWideBlocker = false, BattleSide blockerSide = BattleSide::ATTACKER,
		bool enablePassingLines = true)
	{
		startGame();
		if(enablePassingLines)
			selectPassingLines(attackerSideHero);
		startBattle();
		removeStartingUnits();

		mover = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), corridorStart, 10);
		blocker = addStack(blockerSide,
			creatureByName(doubleWideBlocker ? "core:hydra" : "core:pikeman"),
			corridorBlockerPosition, 1);
		addStack(BattleSide::DEFENDER, creatureByName("core:ogre"), BattleHex(13, 9), 10);
		ASSERT_NE(mover, nullptr);
		ASSERT_NE(blocker, nullptr);
		mover->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::STACKS_SPEED, BonusSource::OTHER, 20, BonusSourceID()));
		addCorridorWall(301, blocker->getHexes());
		beginCombat();
	}

	void activateMover()
	{
		ASSERT_NE(mover, nullptr);
		BattleSetActiveStack activation;
		activation.battleID = BattleID(0);
		activation.stack = mover->unitId();
		activation.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(activation);
		ASSERT_EQ(battle()->battleActiveUnit()->unitId(), mover->unitId());
	}

	bool moveTo(const CStack * stack, const BattleHex & destination)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->battleGetOwner(stack), BattleAction::makeMove(stack, destination));
	}
};
}

TEST_F(NewHorizonsPassingLinesTest, AdvancedBattlecraftAndARealPerkOfferAreRequired)
{
	startGame();
	const int decoded = SecondarySkill::decode(std::string(battlecraftSkill));
	ASSERT_GE(decoded, 0);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	EXPECT_FALSE(offerContains(attackerSideHero, passingLinesPerk));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(std::string(battlecraftSkill), std::string(passingLinesPerk)));
	acceptPerkThroughOffer(attackerSideHero, entrenchPerk);
	attackerSideHero->setSecSkillLevel(SecondarySkill(decoded), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	acceptPerkThroughOffer(attackerSideHero, passingLinesPerk);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(std::string(battlecraftSkill), std::string(passingLinesPerk)));
}

TEST_F(NewHorizonsPassingLinesTest, WithoutThePerkAFriendlyStackStillSealsTheOnlyCorridor)
{
	prepareCorridor(false, BattleSide::ATTACKER, false);
	EXPECT_FALSE(battle()->getReachability(mover).isReachable(corridorDestination));
}

TEST_F(NewHorizonsPassingLinesTest, FriendlyOccupiedHexIsTransitOnlyAndTheServerAcceptsTheEmptyEndpoint)
{
	prepareCorridor();
	ASSERT_FALSE(mover->hasBonusOfType(BonusType::FLYING));
	const auto reachability = battle()->getReachability(mover);
	ASSERT_LT(reachability.distances[blocker->getPosition().toInt()], ReachabilityInfo::INFINITE_DIST);
	EXPECT_FALSE(reachability.isReachable(blocker->getPosition()));
	EXPECT_TRUE(reachability.isReachable(corridorDestination));
	EXPECT_FALSE(battle()->battleGetAvailableHexes(mover, false).contains(blocker->getPosition()));
	ASSERT_GE(mover->getMovementRange(0), static_cast<int>(reachability.distances[corridorDestination.toInt()]));

	PassingLinesEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	const auto projectedMover = projected.getForUpdate(mover->unitId());
	ASSERT_NE(projectedMover, nullptr);
	const auto projectedReachability = projected.getReachability(projectedMover.get());
	EXPECT_TRUE(projectedReachability.isReachable(corridorDestination));
	EXPECT_FALSE(projectedReachability.isReachable(blocker->getPosition()));
	EXPECT_EQ(projectedMover->getPosition(), corridorStart);

	activateMover();
	const auto originalPosition = mover->getPosition();
	EXPECT_FALSE(moveTo(mover, blocker->getPosition()))
		<< "A crossed friendly unit is never a legal movement endpoint";
	EXPECT_EQ(mover->getPosition(), originalPosition);
	ASSERT_TRUE(moveTo(mover, corridorDestination));
	EXPECT_EQ(mover->getPosition(), corridorDestination);
	EXPECT_EQ(projectedMover->getPosition(), corridorStart)
		<< "The detached AI view remains isolated from the authoritative move";
}

TEST_F(NewHorizonsPassingLinesTest, HypnotizedDoubleWideFootprintUsesCurrentControllerForTransit)
{
	prepareCorridor(true, BattleSide::DEFENDER);
	ASSERT_TRUE(blocker->doubleWide());
	const auto blockedReachability = battle()->getReachability(mover);
	EXPECT_FALSE(blockedReachability.isReachable(corridorDestination))
		<< "A hostile double-wide stack still seals the only gap in the force-field line";

	auto hypnosis = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	blocker->addNewBonus(hypnosis);
	ASSERT_EQ(blocker->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(battle()->playerToSide(battle()->battleGetOwner(blocker)), BattleSide::ATTACKER);

	ReachabilityInfo::Parameters parameters(mover, corridorStart);
	battle()->configurePassingLines(mover, parameters);
	ASSERT_EQ(blocker->getHexes().size(), 2u);
	for(const auto & hex : blocker->getHexes())
		EXPECT_TRUE(parameters.friendlyTransit.test(static_cast<size_t>(hex.toInt())))
			<< "Both occupied hexes of a wide controlled stack belong to the transit mask";

	const auto reachability = battle()->getReachability(mover);
	EXPECT_TRUE(reachability.isReachable(corridorDestination));
	for(const auto & hex : blocker->getHexes())
		EXPECT_FALSE(reachability.isReachable(hex))
			<< "Neither half of the double-wide footprint is an endpoint";

	activateMover();
	EXPECT_FALSE(moveTo(mover, blocker->occupiedHex()));
	EXPECT_EQ(mover->getPosition(), corridorStart);
	ASSERT_TRUE(moveTo(mover, corridorDestination));
	EXPECT_EQ(mover->getPosition(), corridorDestination);
}

TEST_F(NewHorizonsPassingLinesTest, OrdinaryMovementThroughTheFormationStillTriggersFireWallAndAiPreview)
{
	prepareCorridor();
	BattleHexArray wallTiles{blocker->getPosition()};
	const SpellID fireWallTrigger(SpellID::decode("core:fireWallTrigger"));
	ASSERT_TRUE(fireWallTrigger.hasValue());
	addObstacle(302, SpellID::FIRE_WALL, fireWallTrigger, BattleSide::DEFENDER,
		true, false, false, wallTiles, 5);

	const auto realBefore = battle()->getReachability(mover);
	ASSERT_TRUE(realBefore.isReachable(corridorDestination));
	PassingLinesEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle projected(&environment, callback);
	const auto projectedMover = projected.getForUpdate(mover->unitId());
	ASSERT_NE(projectedMover, nullptr);
	EXPECT_TRUE(projected.getReachability(projectedMover.get()).isReachable(corridorDestination));

	activateMover();
	const auto movementActivation = battle()->getActivationSerial();
	const auto healthBefore = mover->getAvailableHealth();
	ASSERT_TRUE(moveTo(mover, corridorDestination));
	EXPECT_EQ(mover->getPosition(), corridorDestination);
	EXPECT_EQ(healthBefore - mover->getAvailableHealth(), 5)
		<< "The Fire Wall on the crossed occupied footprint triggers exactly once";
	const auto obstacles = battle()->getAllObstacles();
	const auto fireWall = std::ranges::find_if(obstacles, [](const auto & obstacle)
	{
		return obstacle->ID == SpellID::FIRE_WALL;
	});
	ASSERT_NE(fireWall, obstacles.end());
	const auto * triggered = dynamic_cast<const SpellCreatedObstacle *>(fireWall->get());
	ASSERT_NE(triggered, nullptr);
	EXPECT_EQ(triggered->lastTriggerUnit, static_cast<si32>(mover->unitId()));
	EXPECT_EQ(triggered->lastTriggerActivation, movementActivation);
	EXPECT_EQ(projectedMover->getPosition(), corridorStart)
		<< "Movement preview does not mutate the authoritative or detached stack position";
}

TEST_F(NewHorizonsPassingLinesTest, HiddenQuicksandAtAFriendlyFootprintStopsBeforeTheOccupiedRun)
{
	prepareCorridor();
	BattleHexArray quicksandTiles{blocker->getPosition()};
	addObstacle(303, SpellID::QUICKSAND, SpellID::NONE, BattleSide::DEFENDER,
		true, true, true, quicksandTiles);

	CPlayerBattleCallback attackerView(battle(), PlayerColor(0));
	const auto visibleObstacles = attackerView.battleGetAllObstacles(BattleSide::ATTACKER);
	EXPECT_FALSE(std::ranges::any_of(visibleObstacles, [](const auto & obstacle)
	{
		return obstacle->ID == SpellID::QUICKSAND;
	})) << "The hidden hostile Quicksand remains absent from the attacker's preview";

	const auto preview = attackerView.getReachability(mover);
	ASSERT_TRUE(preview.isReachable(corridorDestination));
	ASSERT_LT(preview.distances[blocker->getPosition().toInt()], ReachabilityInfo::INFINITE_DIST);
	const auto lastLegalHex = preview.predecessors[blocker->getPosition().toInt()];
	ASSERT_TRUE(lastLegalHex.isAvailable());
	ASSERT_NE(lastLegalHex, blocker->getPosition());
	EXPECT_FALSE(preview.isReachable(blocker->getPosition()));

	activateMover();
	ASSERT_TRUE(moveTo(mover, corridorDestination));
	EXPECT_EQ(mover->getPosition(), lastLegalHex)
		<< "The trap stops at the last empty legal hex before crossing the friendly footprint";
	EXPECT_FALSE(blocker->coversPos(mover->getPosition()));
	EXPECT_NE(mover->getPosition(), corridorDestination)
		<< "Movement cannot emerge beyond an obstacle that stopped the crossing";
	const auto obstacles = battle()->getAllObstacles();
	const auto revealed = std::ranges::find_if(obstacles, [](const auto & obstacle)
	{
		const auto * spellObstacle = dynamic_cast<const SpellCreatedObstacle *>(obstacle.get());
		return spellObstacle && spellObstacle->ID == SpellID::QUICKSAND;
	});
	ASSERT_NE(revealed, obstacles.end());
	EXPECT_TRUE(dynamic_cast<const SpellCreatedObstacle *>(revealed->get())->revealed);
}
