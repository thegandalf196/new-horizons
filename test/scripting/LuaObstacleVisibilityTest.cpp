/*
 * LuaObstacleVisibilityTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../luascript/StdInc.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/logging/CLogger.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include <atomic>

namespace
{
class ObstacleEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit ObstacleEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class AuthorityObstacleCallback final : public CPlayerBattleCallback
{
public:
	explicit AuthorityObstacleCallback(const IBattleInfo * battle)
		: CPlayerBattleCallback(battle, PlayerColor(0)) {}
	std::optional<PlayerColor> getPlayerID() const override { return std::nullopt; }
};

class ObstacleWarningTarget final : public ILogTarget
{
	std::weak_ptr<std::atomic<size_t>> messages;
public:
	explicit ObstacleWarningTarget(std::weak_ptr<std::atomic<size_t>> messages)
		: messages(std::move(messages)) {}
	void write(const LogRecord & record) override
	{
		if(record.message == "Unauthorized obstacles access attempt, assuming massive spell")
			if(const auto output = messages.lock())
				++*output;
	}
};

class ObstacleWarnings final
{
	CLogger * logger = CLogger::getGlobalLogger();
	ELogLevel::ELogLevel previousLevel = logger->getLevel();
public:
	std::shared_ptr<std::atomic<size_t>> messages = std::make_shared<std::atomic<size_t>>(0);
	ObstacleWarnings()
	{
		logger->setLevel(ELogLevel::WARN);
		logger->addTarget(std::make_unique<ObstacleWarningTarget>(messages));
	}
	~ObstacleWarnings() { logger->setLevel(previousLevel); }
};

// Exercise the production Lua binding and real Remove Obstacle helper. This
// adapter supplies only the two read-only mechanics methods used by that helper.
constexpr char probeScript[] = R"lua(
local Remove = require("spells/removeObstacle")
return { inspect = function(battle, hiddenHex)
	local mechanics = {
		getBattle = function() return battle end,
		isMassive = function() return false end
	}
	local problem = { count = 0, addStandard = function(self) self.count = self.count + 1 end }
	local effect = setmetatable({removeAllSpells = true}, {__index = Remove})
	local general = effect:applicableGeneral(mechanics, problem)
	return {
		all = #battle:getAllObstacles(),
		hidden = #battle:getObstaclesOnPos(hiddenHex, false),
		removable = #effect:getTargets(mechanics, {}, true),
		target = effect:applicableTarget(mechanics, problem, {{hex = hiddenHex}}),
		general = general, problems = problem.count
	}
end }
)lua";
}

class LuaObstacleVisibilityTest : public HeroCommandFixture
{
protected:
	std::shared_ptr<ObstacleEnvironment> environment;
	const BattleHex hiddenHex{8, 3};

	void addMine(int id, BattleSide casterSide, bool hidden, BattleHex position, bool revealed = false)
	{
		SpellCreatedObstacle mine;
		mine.uniqueID = id;
		mine.ID = SpellID::LAND_MINE;
		mine.pos = position;
		mine.customSize.insert(position);
		mine.casterSide = casterSide;
		mine.hidden = hidden;
		mine.revealed = revealed;
		mine.nativeVisible = false;
		mine.passable = true;
		BattleObstaclesChanged pack;
		pack.battleID = BattleID(0);
		mine.toInfo(pack.change);
		gameHandler->sendAndApply(pack);
	}

	void prepare(BattleSide observer, bool hiddenOnly = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		ASSERT_TRUE(battle()->getAllObstacles().empty());
		const auto enemy = CBattleInfoEssentials::otherSide(observer);
		addMine(702, enemy, true, hiddenHex);
		if(!hiddenOnly)
		{
			addMine(701, observer, true, BattleHex(7, 3));
			addMine(703, enemy, true, BattleHex(9, 3), true);
		}
		environment = std::make_shared<ObstacleEnvironment>(gameState());
	}

	std::shared_ptr<CPlayerBattleCallback> player(BattleSide side)
	{
		return std::make_shared<CPlayerBattleCallback>(battle(), battle()->getSidePlayer(side));
	}

	void check(const IBattleInfoCallback & callback, int expected, bool knowsHidden)
	{
		const auto before = battle()->getAllObstacles();
		ObstacleWarnings warnings;
		scripting::LuaModule host;
		scripting::LuaScriptInstance script(host, "core", std::string(probeScript));
		scripting::LuaContext context(&script, environment.get());
		context.initialize();
		ASSERT_TRUE(context.hasFunction("inspect"));
		const auto result = context.callFunction<JsonNode>("inspect", &callback, hiddenHex);
		ASSERT_TRUE(result.isStruct()) << "The real Lua helper must finish successfully";
		EXPECT_EQ(result["all"].Float(), expected);
		EXPECT_EQ(result["removable"].Float(), expected);
		EXPECT_EQ(result["hidden"].Float(), knowsHidden ? 1 : 0);
		EXPECT_EQ(result["target"].Bool(), knowsHidden);
		EXPECT_EQ(result["general"].Bool(), expected > 0);
		EXPECT_EQ(result["problems"].Float(), expected > 0 ? 0 : 1);
		EXPECT_TRUE(warnings.messages->load() == 0);
		EXPECT_EQ(battle()->getAllObstacles(), before);
	}
};

class LuaObstacleObserverTest : public LuaObstacleVisibilityTest,
	public ::testing::WithParamInterface<BattleSide>
{
};

TEST_P(LuaObstacleObserverTest, DirectPlayerSeesOwnHiddenAndRevealedEnemyButNotUnrevealedEnemy)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	ASSERT_EQ(battle()->getAllObstacles().size(), 3u);
	ASSERT_NO_FATAL_FAILURE(check(*player(GetParam()), 2, false));
}

TEST_P(LuaObstacleObserverTest, HypotheticViewCannotRecoverUnrevealedEnemyTraps)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	HypotheticBattle model(environment.get(), player(GetParam()));
	ASSERT_EQ(model.getAllObstacles().size(), 2u);
	ASSERT_NO_FATAL_FAILURE(check(model, 2, false));
}

INSTANTIATE_TEST_SUITE_P(BothObservers, LuaObstacleObserverTest,
	::testing::Values(BattleSide::ATTACKER, BattleSide::DEFENDER));

TEST_F(LuaObstacleVisibilityTest, ObserverFreeAuthorityRetainsAllObstacles)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER));
	AuthorityObstacleCallback authority(battle());
	ASSERT_EQ(authority.battleGetMySide(), BattleSide::ALL_KNOWING);
	ASSERT_NO_FATAL_FAILURE(check(authority, 3, true));
}

TEST_F(LuaObstacleVisibilityTest, HiddenEnemyTrapAloneDoesNotMakePlayerRemovalApplicable)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER, true));
	const auto callback = player(BattleSide::ATTACKER);
	ASSERT_NO_FATAL_FAILURE(check(*callback, 0, false));
	HypotheticBattle model(environment.get(), callback);
	ASSERT_NO_FATAL_FAILURE(check(model, 0, false));
	AuthorityObstacleCallback authority(battle());
	ASSERT_NO_FATAL_FAILURE(check(authority, 1, true));
}

TEST_F(LuaObstacleVisibilityTest, ExplicitOmniscientPlayerOverrideStillEmitsItsDiagnostic)
{
	ASSERT_NO_FATAL_FAILURE(prepare(BattleSide::ATTACKER));
	ObstacleWarnings warnings;
	EXPECT_EQ(player(BattleSide::ATTACKER)->battleGetAllObstacles(BattleSide::ALL_KNOWING).size(), 3u);
	ASSERT_EQ(warnings.messages->load(), 1u);
}
