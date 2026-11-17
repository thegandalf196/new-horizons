/*
 * NativeRosterDecisionProfileTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../game/GameStateTest.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CBonusTypeHandler.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/logging/CLogger.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/BattleLayout.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/AutocombatPreferences.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/mapObjects/army/CArmedInstance.h"
#include "../../lib/mapObjects/ObjectTemplate.h"
#include "../../lib/mapping/MapDifficulty.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <cstdlib>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>

namespace
{
class InfoLogScope
{
	CLogger * global = CLogger::getGlobalLogger();
	CLogger * ai = CLogger::getLogger(CLoggerDomain("ai"));
	ELogLevel::ELogLevel oldGlobal = global->getLevel(), oldAi = ai->getLevel();
public:
	InfoLogScope()
	{
		const char * mode = std::getenv("NH_ORDINARY_DIAGNOSTICS");
		const bool diagnostics = mode && std::string_view(mode) == "1";
		global->setLevel(diagnostics ? ELogLevel::DEBUG : ELogLevel::INFO);
		ai->setLevel(diagnostics ? ELogLevel::DEBUG : ELogLevel::INFO);
	}
	~InfoLogScope() { ai->setLevel(oldAi); global->setLevel(oldGlobal); }
};
class RosterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RosterEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class RosterCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> actions;
	size_t heroActions = 0;
	RosterCallback() : CBattleCallback(PlayerColor(0), nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { actions.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction &) override { ++heroActions; }
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
};
}

class NativeRosterDecisionProfileTest : public GameStateTest
{
protected:
	static constexpr std::array<int, 7> rows{0, 2, 4, 5, 6, 8, 10};
	static constexpr std::array<int, 7> counts{14, 13, 16, 13, 9, 13, 7};
	static constexpr std::array<const char *, 7> creatures{"core:halberdier", "core:marksman", "core:gremlin",
		"core:marksman", "core:halberdier", "core:marksman", "core:halberdier"};
	std::shared_ptr<RosterEnvironment> environment;
	std::shared_ptr<RosterCallback> callback;
	std::unique_ptr<CBattleAI> ai;
	BattleInfo * battle = nullptr;
	const CStack * actor = nullptr;

	std::shared_ptr<CArmedInstance> army(BattleSide side)
	{
		auto result = std::make_shared<CArmedInstance>(gameState.get());
		result->tempOwner = PlayerColor(side == BattleSide::ATTACKER ? 0 : 1);
		result->pos = int3(4, 4, 0);
		result->instanceName = side == BattleSide::ATTACKER ? "profileAttacker" : "profileDefender";
		result->appearance = std::make_shared<ObjectTemplate>();
		for(size_t slot = 0; slot < rows.size(); ++slot)
		{
			const bool left = side == BattleSide::ATTACKER;
			const auto id = CreatureID(CreatureID::decode(left ? creatures[slot] : "core:dreadKnight"));
			result->setCreature(SlotID(slot), id, left ? counts[slot] : (slot == 6 ? 1 : 2));
		}
		map->addNewObject(result);
		result->attachToBonusSystem(*gameState);
		return result;
	}
	void prepareRoster()
	{
		randomGenerator.setSeed(1337);
		startTestGame();
		gameState->getStartInfo()->difficulty = static_cast<ui8>(EMapDifficulty::NORMAL);
		const auto left = army(BattleSide::ATTACKER), right = army(BattleSide::DEFENDER);
		BattleSideArray<const CArmedInstance *> armies{left.get(), right.get()};
		BattleSideArray<const CGHeroInstance *> heroes{nullptr, nullptr};
		BattleLayout layout;
		for(size_t slot = 0; slot < rows.size(); ++slot)
		{
			layout.units[BattleSide::ATTACKER][slot] = BattleHex(2, rows[slot]);
			layout.units[BattleSide::DEFENDER][slot] = BattleHex(13, rows[slot]);
		}
		layout.obstaclesAllowed = false;
		layout.tacticsAllowed = false;
		BattleStart start;
		start.battleID = BattleID(0);
		start.info = BattleInfo::setupBattle(gameState.get(), int3(4, 4, 0), TerrainId(TerrainId::GRASS),
			BattleField(0), armies, heroes, layout, nullptr);
		gameState->apply(start);
		battle = gameState->getBattle(BattleID(0));
		battle->nextRound();
		for(const auto * unit : battle->battleGetAllUnits(false))
		{
			auto * stack = battle->getStack(unit->unitId());
			if(unit->unitSide() == BattleSide::DEFENDER)
			{
				// Explicit shared initial WAIT state, not an authoritative action replay.
				stack->waiting = true;
				stack->waitedThisTurn = true;
			}
			else if(unit->unitSlot() == SlotID(1)) actor = stack;
			(void)stack->shots.total();
			(void)stack->counterAttacks.total();
		}
		ASSERT_NE(actor, nullptr);
		battle->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
		callback = std::make_shared<RosterCallback>();
		callback->onBattleStarted(battle);
		environment = std::make_shared<RosterEnvironment>(gameState);
		ai = std::make_unique<CBattleAI>();
		AutocombatPreferences preferences;
		preferences.enableSpellsUsage = false;
		ai->initBattleInterface(environment, callback, preferences);
		ai->battleStart(BattleID(0), left.get(), right.get(), int3(4, 4, 0), nullptr, nullptr, BattleSide::ATTACKER, false);
		randomGenerator.setSeed(1337);
		CRandomGenerator::getDefault().setSeed(1337);
	}
	JsonNode inputs() const
	{
		JsonNode result;
		result["terrain"].Integer() = battle->getTerrainType().getNum();
		result["battlefield"].Integer() = battle->getBattlefieldType().getNum();
		result["obstacles"].Integer() = battle->obstacles.size();
		result["round"].Integer() = battle->getRound();
		result["active"].Integer() = actor->unitId();
		result["difficulty"].Integer() = gameState->getStartInfo()->difficulty;
		for(const auto * unit : battle->battleGetAllUnits(false))
		{
			JsonNode row;
			row["id"].Integer() = unit->unitId();
			row["creature"].String() = unit->unitType()->getJsonKey();
			row["creatureAIValue"].Integer() = unit->unitType()->getAIValue();
			row["doubleWide"].Bool() = unit->doubleWide();
			row["side"].Integer() = static_cast<int>(unit->unitSide());
			row["owner"].Integer() = unit->unitOwner().getNum();
			row["controller"].Integer() = battle->battleGetActionController(unit).getNum();
			row["slot"].Integer() = unit->unitSlot().getNum();
			row["position"].Integer() = unit->getPosition().toInt();
			row["count"].Integer() = unit->getCount();
			row["health"].Integer() = unit->getAvailableHealth();
			row["maxHealth"].Integer() = unit->getMaxHealth();
			row["attack"].Integer() = unit->getAttack(true);
			row["defense"].Integer() = unit->getDefense(true);
			row["damageMin"].Integer() = unit->getMinDamage(true);
			row["damageMax"].Integer() = unit->getMaxDamage(true);
			row["attackMelee"].Integer() = unit->getAttack(false);
			row["defenseMelee"].Integer() = unit->getDefense(false);
			row["damageMinMelee"].Integer() = unit->getMinDamage(false);
			row["damageMaxMelee"].Integer() = unit->getMaxDamage(false);
			row["speed"].Integer() = unit->getMovementRange();
			row["initiative"].Integer() = unit->getInitiative();
			row["rangedAttacks"].Integer() = unit->getTotalAttacks(true);
			row["luckBonus"].Integer() = unit->valOfBonuses(BonusType::LUCK);
			row["moraleBonus"].Integer() = unit->valOfBonuses(BonusType::MORALE);
			const auto owned = unit->acquireState();
			row["waiting"].Bool() = owned->waiting;
			row["waitedThisTurn"].Bool() = owned->waitedThisTurn;
			row["shots"].Integer() = owned->shots.available();
			row["retaliations"].Integer() = owned->counterAttacks.available();
			result["units"].Vector().push_back(std::move(row));
		}
		return result;
	}
	JsonNode nativeStates() const
	{
		JsonNode result;
		for(const auto * unit : battle->battleGetAllUnits(false))
		{
			JsonNode row, state;
			const auto owned = unit->acquireState();
			state = owned->save();
			row["id"].Integer() = unit->unitId();
			row["state"] = state;
			const auto bonuses = unit->getAllBonuses(Selector::all);
			for(const auto & bonus : *bonuses)
			{
				JsonNode entry;
				// Keep the real limiter/payload graph in native bytes: upstream's
				// TerrainLimiter JSON exporter cannot represent its valid native sentinel.
				entry["nativeBytes"].String() = hex(bytes(*bonus));
				entry["type"].String() = LIBRARY->bth->bonusToString(bonus->type);
				entry["val"].Integer() = bonus->val;
				entry["valueType"].Integer() = static_cast<int>(bonus->valType);
				entry["sourceType"].Integer() = static_cast<int>(bonus->source);
				entry["effectRange"].Integer() = static_cast<int>(bonus->effectRange);
				entry["duration"].Integer() = static_cast<int>(bonus->duration);
				entry["turnsRemain"].Integer() = bonus->turnsRemain;
				entry["stacking"].String() = bonus->stacking;
				if(bonus->subtype != BonusSubtypeID()) entry["subtype"].String() = bonus->subtype.toString();
				if(bonus->sid != BonusSourceID()) entry["sourceID"].String() = bonus->sid.toString();
				if(bonus->parameters) entry["parameters"] = bonus->parameters->toJsonNode();
				row["bonuses"].Vector().push_back(std::move(entry));
			}
			result.Vector().push_back(std::move(row));
		}
		return result;
	}
	static std::string hex(const std::vector<std::byte> & data)
	{
		constexpr char digits[] = "0123456789abcdef";
		std::string result;
		result.reserve(data.size() * 2);
		for(const auto byte : data)
		{
			const auto value = std::to_integer<unsigned char>(byte);
			result += digits[value >> 4];
			result += digits[value & 15];
		}
		return result;
	}
	template<class T> static std::vector<std::byte> bytes(const T & value)
	{
		CMemorySerializer memory;
		memory.oser & value;
		return memory.extractBuffer();
	}
};

TEST_F(NativeRosterDecisionProfileTest, FourteenStackOrdinaryFirstDecision)
{
	InfoLogScope infoLogs;
	ASSERT_NO_FATAL_FAILURE(prepareRoster());
	ASSERT_EQ(battle->battleGetAllUnits(false).size(), 14u);
	ASSERT_TRUE(battle->obstacles.empty());
	ASSERT_EQ(actor->creatureId(), CreatureID(CreatureID::decode("core:marksman")));
	ASSERT_EQ(actor->getTotalAttacks(true), 2);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		ASSERT_EQ(battle->battleGetFightingHero(side), nullptr);
	const auto before = inputs(), statesBefore = nativeStates();
	const auto gameRng = bytes(randomGenerator), aiRng = bytes(CRandomGenerator::getDefault());
	RecordProperty("effectiveInputs", before.toString());
	RecordProperty("nativeStateBefore", statesBefore.toString());
	const bool diagnostics = std::getenv("NH_ORDINARY_DIAGNOSTICS")
		&& std::string_view(std::getenv("NH_ORDINARY_DIAGNOSTICS")) == "1";
	RecordProperty("logLevel", diagnostics ? "DEBUG" : "INFO");
	RecordProperty("diagnostics", diagnostics ? "1" : "0");
	RecordProperty("searchPolicy", "Full");
	RecordProperty("unitCount", "14");
	int creatureCount = 0;
	for(const auto * unit : battle->battleGetAllUnits(false))
		creatureCount += unit->getCount();
	ASSERT_EQ(creatureCount, 98);
	RecordProperty("creatureCount", std::to_string(creatureCount));
	RecordProperty("initialRound", std::to_string(battle->getRound()));
	RecordProperty("seed", "1337");
	RecordProperty("gameRngBefore", hex(gameRng));
	RecordProperty("aiRngBefore", hex(aiRng));
	const auto started = std::chrono::steady_clock::now();
	ai->activeStack(BattleID(0), actor);
	const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
	ASSERT_EQ(callback->heroActions, 0u);
	ASSERT_EQ(callback->actions.size(), 1u);
	const auto & action = callback->actions.front();
	EXPECT_EQ(action.stackNumber, actor->unitId());
	if(action.actionType == EActionType::SHOOT)
	{
		const auto target = action.getTarget(callback->getBattle(BattleID(0)).get());
		ASSERT_EQ(target.size(), 1u);
		ASSERT_NE(target.front().unitValue, nullptr);
		EXPECT_TRUE(battle->battleCanShootAction(actor, target.front().unitValue->getPosition()));
	}
	else if(action.actionType == EActionType::WAIT)
		EXPECT_FALSE(actor->waitedThisTurn);
	else if(action.actionType == EActionType::WALK)
	{
		const auto target = action.getTarget(callback->getBattle(BattleID(0)).get());
		ASSERT_EQ(target.size(), 1u);
		const auto available = battle->battleGetAvailableHexes(actor, false);
		EXPECT_TRUE(available.contains(target.front().hexValue));
	}
	else
		EXPECT_EQ(action.actionType, EActionType::DEFEND);
	EXPECT_EQ(inputs(), before);
	EXPECT_EQ(nativeStates(), statesBefore);
	EXPECT_EQ(bytes(randomGenerator), gameRng);
	EXPECT_EQ(bytes(CRandomGenerator::getDefault()), aiRng);
	RecordProperty("gameRngAfter", hex(bytes(randomGenerator)));
	RecordProperty("aiRngAfter", hex(bytes(CRandomGenerator::getDefault())));
	RecordProperty("firstCallbackUs", std::to_string(elapsed));
	RecordProperty("selectedAction", action.toString());
	RecordProperty("horizon", "2");
	RecordProperty("nativeStateAfter", nativeStates().toString());
	RecordProperty("selectedActionHex", hex(bytes(action)));
	RecordProperty("heroActionCount", std::to_string(callback->heroActions));
	RecordProperty("unitActionCount", std::to_string(callback->actions.size()));
	RecordProperty("finalRound", std::to_string(battle->getRound()));
	const auto timing = ai->getTimingSummary();
	RecordProperty("workCountersAvailable", timing ? "1" : "0");
	if(timing)
	{
		RecordProperty("callbackCount", std::to_string(timing->activeStackCalls));
		RecordProperty("roundsObserved", std::to_string(timing->roundsObserved));
		RecordProperty("movementExchangeCount", std::to_string(timing->movementExchangeBody.calls));
		RecordProperty("normalExchangeCount", std::to_string(timing->bestTargetNormalBody.calls));
		RecordProperty("waitedExchangeCount", std::to_string(timing->bestTargetWaitedBody.calls));
		RecordProperty("movementTargetProbes", std::to_string(timing->movementExchangeTargetProbes.calls));
		RecordProperty("normalTargetProbes", std::to_string(timing->bestTargetNormalTargetProbes.calls));
		RecordProperty("waitedTargetProbes", std::to_string(timing->bestTargetWaitedTargetProbes.calls));
	}
	if(diagnostics)
		ASSERT_TRUE(timing.has_value()) << "Diagnostic pass requires admitted BattleAI timing";
	RecordProperty("scope", "One ordinary no-Hero callback; captured submission/native legality query, not authoritative replay or whole combat");
}
