/*
 * NewHorizonsDemonicReserveAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "AI/BattleAI/BattleAI.h"
#include "AI/Nullkiller2/AIGateway.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "lib/CPlayerState.h"
#include "lib/callback/CBattleCallback.h"
#include "lib/callback/CCallback.h"
#include "lib/callback/IClient.h"
#include "lib/entities/hero/NewHorizonsLeadership.h"
#include "lib/mapObjects/CGTownInstance.h"
#include "lib/networkPacks/PacksForServer.h"
#include "lib/serializer/CMemorySerializer.h"

#include <array>
#include <limits>
#include <set>

namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto GATING = "new-horizons:demonicGating";
constexpr std::array<const char *, 7> INFERNO = {
	"core:imp", "core:magog", "core:cerberus", "core:hornedDemon", "core:pitLord", "core:efreetSultan", "core:archDevil"
};

class ReserveLoopback final : public IClient
{
	CGameHandler & handler;
	int sequence = 0;
public:
	std::vector<ArrangeDemonicReserve> reserves;
	std::vector<ArrangeStacks> transfers;
	bool rejectTransfers = false;
	explicit ReserveLoopback(CGameHandler & handler) : handler(handler) {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(PlayerColor,
		const BattleID &, const BattleStateInfoForRetreat &) override { return std::nullopt; }
	int sendRequest(const CPackForServer & request, PlayerColor player, bool) override
	{
		auto incoming = CMemorySerializer::deepCopy(request);
		incoming->player = player;
		incoming->requestID = ++sequence;
		if(const auto * reserve = dynamic_cast<const ArrangeDemonicReserve *>(incoming.get()))
			reserves.push_back(*reserve);
		if(const auto * transfer = dynamic_cast<const ArrangeStacks *>(incoming.get()))
		{
			transfers.push_back(*transfer);
			if(rejectTransfers)
				return sequence;
		}
		handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, *incoming);
		return sequence;
	}
};

class ReserveEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ReserveEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class ReserveBattleCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	ReserveBattleCallback() : CBattleCallback(PLAYER, nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { actions.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override {}
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &,
		const BattleStateInfoForRetreat &) override { return std::nullopt; }
};

class NewHorizonsDemonicReserveAITest : public HeroCommandFixture
{
protected:
	CGTownInstance * town = nullptr;
	std::unique_ptr<ReserveLoopback> client;
	std::shared_ptr<CCallback> callback;
	std::unique_ptr<NK2AI::AIGateway> gateway;

	void TearDown() override
	{
		gateway.reset();
		callback.reset();
		client.reset();
		HeroCommandFixture::TearDown();
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
		// Deterministic capacity boundary, not AI/balance tuning: each distinct
		// ordinary Inferno row fits one creature, and surplus cannot merge into it.
		capabilities["classProfiles"]["core:demoniac"]["base"] = JsonNode(1000);
		for(const auto * key : INFERNO)
			capabilities["leadership"]["creatureRequirements"][key] = JsonNode(1000);
		capabilities["leadership"]["creatureRequirements"]["core:familiar"] = JsonNode(1000);
		capabilities["leadership"]["creatureRequirements"]["core:gog"] = JsonNode(1000);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, capabilities);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::CREATURES_NEW_HORIZONS_CATEGORIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCreatureCategories")));
	}

	void prepare(bool full = true)
	{
		std::vector<std::pair<CreatureID, uint16_t>> army;
		for(const auto * key : INFERNO)
			army.emplace_back(creatureByName(key), 1);
		if(!full)
			army.resize(1);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER).playerActive(PlayerColor(1))
			.town({12, 12, 0}, FactionID::INFERNO, PLAYER).townGarrison({})
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:fiona")), PLAYER).heroGarrison(army)
			.hero({24, 24, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), PlayerColor(1))
			.heroGarrison({{creatureByName("core:pikeman"), 100}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PLAYER);
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		town = findFirst<CGTownInstance>();
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(town, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		town->setVisitingHero(attackerSideHero);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(GATING)),
			MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->getDemonicReserve().empty());
		for(const auto & [slot, stack] : attackerSideHero->Slots())
		{
			const auto capacity = attackerSideHero->getLeadershipSlotCapacity(stack->getCreatureID());
			ASSERT_TRUE(capacity);
			ASSERT_EQ(capacity->maximum, 1);
		}
		gameState()->actingPlayers.insert(PLAYER);
		revealMap(PLAYER);
		client = std::make_unique<ReserveLoopback>(*gameHandler);
		callback = makeCallback(PLAYER, client.get());
		gateway = std::make_unique<NK2AI::AIGateway>();
		gateway->initGameInterface(std::shared_ptr<Environment>(), callback);
	}

	using Army = std::map<SlotID, std::pair<CreatureID, TQuantity>>;
	static Army snapshot(const CArmedInstance * army)
	{
		Army result;
		for(const auto & [slot, stack] : army->Slots())
			result[slot] = {stack->getCreatureID(), stack->getCount()};
		return result;
	}
	std::map<CreatureID, int64_t> totals() const
	{
		std::map<CreatureID, int64_t> result;
		for(const auto * army : {static_cast<const CArmedInstance *>(town), static_cast<const CArmedInstance *>(attackerSideHero)})
			for(const auto & [slot, stack] : army->Slots())
				result[stack->getCreatureID()] += stack->getCount();
		for(const auto & [creature, count] : attackerSideHero->getDemonicReserve())
			result[creature] += count;
		return result;
	}
	void stock(CreatureID creature, int count)
	{
		ASSERT_TRUE(town->setCreature(SlotID(0), creature, count));
	}
};
}

TEST_F(NewHorizonsDemonicReserveAITest, OrdinaryRecruitedNonduplicateArmyProducesReserveThenActualAIGateArrives)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto imp = creatureByName("core:imp");
	// Normal recruitment places purchased creatures in the town upper army.
	gameState()->getPlayerState(PLAYER)->resources[EGameResID::GOLD] = 10000;
	town->addBuilding(BuildingID::DWELL_LVL_1);
	town->creatures.at(0) = {1, {imp}};
	callback->recruitCreatures(town, town, imp, 1, 0);
	ASSERT_EQ(town->getStackCount(SlotID(0)), 1);
	const auto conserved = totals();
	const auto original = snapshot(attackerSideHero);
	const auto resources = gameState()->getPlayerState(PLAYER)->resources;
	gateway->moveCreaturesToHero(town);
	ASSERT_EQ(client->reserves.size(), 1u);
	EXPECT_TRUE(client->reserves.front().toReserve);
	EXPECT_EQ(client->reserves.front().amount, 1);
	EXPECT_EQ(attackerSideHero->getDemonicReserveCount(imp), 1);
	EXPECT_EQ(snapshot(attackerSideHero), original);
	EXPECT_EQ(totals(), conserved);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources, resources);
	std::set<CreatureID> activeTypes;
	for(const auto & [slot, stack] : attackerSideHero->Slots())
		activeTypes.insert(stack->getCreatureID());
	EXPECT_EQ(activeTypes.size(), GameConstants::ARMY_SIZE);
	gateway->moveCreaturesToHero(town);
	gateway->prepareDemonicReserve(town);
	EXPECT_EQ(client->reserves.size(), 1u) << "An already usable reserve must not oscillate";
	EXPECT_EQ(totals(), conserved);

	startBattle();
	beginCombat();
	auto * active = const_cast<CStack *>(dynamic_cast<const CStack *>(battle()->battleActiveUnit()));
	ASSERT_NE(active, nullptr);
	ASSERT_EQ(active->unitSide(), BattleSide::ATTACKER);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	auto environment = std::make_shared<ReserveEnvironment>(gameState());
	auto combatCallback = std::make_shared<ReserveBattleCallback>();
	combatCallback->onBattleStarted(battle());
	CBattleAI ai;
	ai.initBattleInterface(environment, combatCallback);
	ai.battleStart(BattleID(0), attackerSideHero, defenderSideHero, int3(4, 4, 0),
		attackerSideHero, defenderSideHero, BattleSide::ATTACKER, false);
	ai.activeStack(BattleID(0), active);
	ASSERT_EQ(combatCallback->actions.size(), 1u);
	const auto action = combatCallback->actions.front();
	ASSERT_EQ(action.actionType, EActionType::DEMONIC_GATING);
	EXPECT_EQ(action.gatingCreature, imp);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PLAYER, action));
	ASSERT_EQ(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.size(), 1u);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.front().count, 1);
	EXPECT_TRUE(battle()->getDemonicReserve(BattleSide::ATTACKER).empty());
	endRound();
	const auto arrivals = battle()->battleGetStacksIf([&](const CStack * stack)
	{
		return stack->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER && stack->creatureId() == imp;
	});
	ASSERT_EQ(arrivals.size(), 1u);
	EXPECT_EQ(arrivals.front()->getCount(), 1);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).pendingDemonicGates.empty());
	EXPECT_EQ(totals(), conserved) << "Battle commitment does not conjure adventure troops";
}

TEST_F(NewHorizonsDemonicReserveAITest, FreeStagingSlotCapsSurplusAndLeavesActiveArmyUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:gog"), 5));
	const auto original = snapshot(attackerSideHero);
	const auto conserved = totals();
	gateway->prepareDemonicReserve(town);
	ASSERT_EQ(client->reserves.size(), 1u);
	EXPECT_EQ(attackerSideHero->getDemonicReserveCount(creatureByName("core:gog")), 1);
	EXPECT_EQ(town->getStackCount(SlotID(0)), 4);
	EXPECT_EQ(snapshot(attackerSideHero), original);
	EXPECT_EQ(totals(), conserved);
}

TEST_F(NewHorizonsDemonicReserveAITest, FullArmyCanUseNonduplicateStrongerRefill)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto familiar = creatureByName("core:familiar");
	ASSERT_NO_FATAL_FAILURE(stock(familiar, 1));
	const auto before = attackerSideHero->getArmyStrength();
	const auto conserved = totals();
	gateway->prepareDemonicReserve(town);
	ASSERT_EQ(client->reserves.size(), 1u);
	EXPECT_EQ(client->reserves.front().creatureId, creatureByName("core:imp"));
	EXPECT_GE(attackerSideHero->getArmyStrength(), before);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 1);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), familiar);
	EXPECT_EQ(totals(), conserved);
}

TEST_F(NewHorizonsDemonicReserveAITest, RejectedReplacementWithdrawsExactWholeDepositAndStops)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:imp"), 2));
	const auto original = snapshot(attackerSideHero);
	const auto source = snapshot(town);
	const auto conserved = totals();
	client->rejectTransfers = true;
	gateway->prepareDemonicReserve(town);
	ASSERT_EQ(client->reserves.size(), 2u);
	EXPECT_TRUE(client->reserves.front().toReserve);
	EXPECT_FALSE(client->reserves.back().toReserve);
	EXPECT_EQ(client->reserves.front().amount, client->reserves.back().amount);
	EXPECT_EQ(client->reserves.front().creatureId, client->reserves.back().creatureId);
	EXPECT_EQ(snapshot(attackerSideHero), original);
	EXPECT_EQ(snapshot(town), source);
	EXPECT_TRUE(attackerSideHero->getDemonicReserve().empty());
	EXPECT_EQ(totals(), conserved);
	EXPECT_EQ(client->transfers.size(), 1u);
}

TEST_F(NewHorizonsDemonicReserveAITest, NoStockNoRankWrongCategoryAndNoOpenerDoNotStripStartingArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	const auto original = snapshot(attackerSideHero);
	gateway->prepareDemonicReserve(town);
	EXPECT_EQ(snapshot(attackerSideHero), original);
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:imp"), 2));
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(GATING)), 0, ChangeValueMode::ABSOLUTE);
	gateway->prepareDemonicReserve(town);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(GATING)), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:devil"), 2));
	gateway->prepareDemonicReserve(town);
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:imp"), 2));
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:pikeman"), 1));
	gateway->prepareDemonicReserve(town);
	EXPECT_TRUE(client->reserves.empty());
	EXPECT_TRUE(client->transfers.empty());
	EXPECT_TRUE(attackerSideHero->getDemonicReserve().empty());
}

TEST_F(NewHorizonsDemonicReserveAITest, OtherOwnerAndHeroGarrisonAreNotTownSurplus)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:imp"), 2));
	town->tempOwner = PlayerColor(1);
	gateway->prepareDemonicReserve(town);
	town->tempOwner = PLAYER;
	town->setGarrisonedHero(defenderSideHero);
	gateway->prepareDemonicReserve(town);
	EXPECT_TRUE(client->reserves.empty());
	EXPECT_TRUE(client->transfers.empty());
}

TEST_F(NewHorizonsDemonicReserveAITest, WeakerReplacementAndUnsafeValuationRejectWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = snapshot(attackerSideHero);
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:peasant"), 1));
	gateway->prepareDemonicReserve(town);
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:imp"), 2));
	// Admission checks the existing manager's int multiplication before calling
	// it. Deliberately invalid loaded counts must never overflow that valuation.
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creatureByName("core:imp"), std::numeric_limits<TQuantity>::max()));
	const auto unsafe = snapshot(attackerSideHero);
	gateway->prepareDemonicReserve(town);
	EXPECT_EQ(snapshot(attackerSideHero), unsafe);
	EXPECT_TRUE(client->reserves.empty());
	EXPECT_TRUE(client->transfers.empty());
	EXPECT_NE(unsafe, original);
}

TEST_F(NewHorizonsDemonicReserveAITest, DefenceLockAndRejectedStagingNeverDepositOrRemoveLastActiveStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(stock(creatureByName("core:gog"), 5));
	const auto original = snapshot(attackerSideHero);
	const auto source = snapshot(town);
	const auto conserved = totals();
	gateway->nullkiller->lockHero(attackerSideHero, NK2AI::HeroLockedReason::DEFENCE);
	gateway->prepareDemonicReserve(town);
	EXPECT_TRUE(client->transfers.empty());
	gateway->nullkiller->unlockHero(attackerSideHero);
	client->rejectTransfers = true;
	gateway->prepareDemonicReserve(town);
	EXPECT_EQ(client->transfers.size(), 1u);
	EXPECT_TRUE(client->reserves.empty());
	EXPECT_EQ(snapshot(attackerSideHero), original);
	EXPECT_EQ(snapshot(town), source);
	EXPECT_EQ(totals(), conserved);
	EXPECT_EQ(attackerSideHero->stacksCount(), 1);
}
