/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../luascript/StdInc.h"
#include "../../luascript/LuaContext.h"
#include "../../luascript/LuaModule.h"
#include "../../luascript/LuaScriptInstance.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/modding/IdentifierStorage.h"
#include "../../lib/modding/ModScope.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleActionProcessor.h"
#include "../../server/battles/BattleProcessor.h"
#include <array>
#include <optional>
#include <tuple>

namespace
{
template<typename T> std::vector<std::byte> bytes(const T & value)
{
	CMemorySerializer memory;
	memory.oser & value;
	return memory.extractBuffer();
}

// Observe the actual authoritative packet order, including packets emitted from
// inside ServerSpellCastEnvironment's adverse-roll path, then apply normally.
class AuthorityTraceServer final : public RecordingGameServer
{
public:
	std::vector<std::pair<std::string, std::vector<std::byte>>> packets;
	void applyPack(CPackForClient & pack) override
	{
		CPackForClient * pointer = &pack;
		packets.emplace_back(typeid(pack).name(), bytes(pointer));
		RecordingGameServer::applyPack(pack);
	}
};

class AuthorityEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AuthorityEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NativeCombatEventAuthorityParityTest : public BattleTestFixture
{
	AuthorityTraceServer trace;
	CStack * actor = nullptr;
	CStack * victim = nullptr;
	JsonNode initialVictim;
	using Result = std::tuple<decltype(trace.packets), std::vector<JsonNode>,
		std::vector<std::byte>, std::array<AdverseCombatRerollState, 2>>;

	void restoreVictim()
	{
		BattleUnitsChanged change;
		change.battleID = BattleID(0);
		change.changedStacks.emplace_back(victim->unitId(), UnitChanges::EOperation::UPDATE);
		change.changedStacks.back().data = initialVictim;
		gameHandler->sendAndApply(change);
	}

	void addTrigger(int chance, int kills)
	{
		const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), "script", std::string("core:destruction"));
		ASSERT_TRUE(id.has_value());
		JsonNode parameters;
		parameters["killBy"].String() = "count";
		parameters["amount"].Integer() = kills;
		auto trigger = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::COMBAT_EVENT_TRIGGER,
			BonusSource::OTHER, chance, BonusSourceID(), BonusSubtypeID(ScriptID(*id)));
		trigger->parameters = std::make_shared<BonusParameters>(parameters);
		actor->addNewBonus(trigger);
	}

protected:
	void TearDown() override
	{
		// CGameHandler borrows trace. Release it before the derived member dies.
		BattleTestFixture::TearDown();
		trace.gameState.reset();
	}

	void compare(bool inert, bool reroll, bool orderedPair)
	{
		ASSERT_NO_FATAL_FAILURE(startGame());
		gameHandler.reset();
		trace.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(trace, gameState());
		gameHandler->randomizer->setSeed(seed);
		ASSERT_NO_FATAL_FAILURE(startBattle());
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 10);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 100);
		ASSERT_NE(actor, nullptr);
		ASSERT_NE(victim, nullptr);
		ASSERT_TRUE(actor->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER)->empty());
		if(orderedPair) ASSERT_NO_FATAL_FAILURE(addTrigger(31, 0));
		ASSERT_NO_FATAL_FAILURE(addTrigger(47, inert ? 0 : 1));
		const auto triggers = actor->getBonusesOfType(BonusType::COMBAT_EVENT_TRIGGER);
		ASSERT_EQ(triggers->size(), orderedPair ? 2u : 1u);
		ASSERT_EQ((*triggers)[0]->val, 47);
		if(orderedPair) ASSERT_EQ((*triggers)[1]->val, 31);
		initialVictim = victim->acquireState()->save();
		const auto world = gameState()->saveToMemory();
		const auto initialRng = bytes(*gameHandler->randomizer);
		const auto defaultRng = bytes(CRandomGenerator::getDefault());
		AuthorityEnvironment environment(gameState());
		scripting::LuaModule host;
		scripting::LuaScriptInstance script(host, "core", ScriptPath::builtinTODO("combat/destruction").addPrefix("SCRIPTS/"), {});
		bool observedInjury = false, observedNoInjury = false, observedReroll = false;
		for(int drawSeed = 1; drawSeed <= 32; ++drawSeed)
		{
			SCOPED_TRACE(drawSeed);
			std::optional<Result> expected;
			for(bool native : {false, true})
			{
				ASSERT_NO_FATAL_FAILURE(restoreVictim());
				CMemorySerializer randomMemory(initialRng);
				randomMemory.iser & *gameHandler->randomizer;
				gameHandler->randomizer->setSeed(drawSeed);
				const auto rngBefore = bytes(*gameHandler->randomizer);
				for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
					battle()->setAdverseCombatRerollState(side, {reroll, false});
				trace.packets.clear();
				if(native)
				{
					BattleActionProcessor dispatcher(gameHandler->battles.get(), gameHandler.get());
					dispatcher.processBattleEventTriggers(*battle(), CombatEventType::AFTER_ATTACK, actor, victim);
				}
				else
				{
					scripting::LuaContext context(&script, &environment);
					context.initialize();
					const auto invoke = [&](int chance, int kills)
					{
						JsonNode parameters;
						parameters["val"].Integer() = chance;
						parameters["killBy"].String() = "count";
						parameters["amount"].Integer() = kills;
						context.callMethod<void>("onAfterAttack", parameters,
							static_cast<ServerCallback *>(gameHandler->spellcastEnvironment()),
							static_cast<const CBattleInfoCallback *>(battle()),
							static_cast<const battle::Unit *>(actor), static_cast<const battle::Unit *>(victim), CombatEventPayload());
					};
					// The unchanged bonus collector sorts equal identity/stacking by
					// descending magnitude before the priority-stable event dispatch.
					invoke(47, inert ? 0 : 1);
					if(orderedPair) invoke(31, 0);
				}
				std::vector<JsonNode> units;
				for(const auto * unit : battle()->battleGetAllUnits(false)) units.push_back(unit->acquire()->save());
				const std::array<AdverseCombatRerollState, 2> rerolls{
					battle()->getAdverseCombatRerollState(BattleSide::ATTACKER),
					battle()->getAdverseCombatRerollState(BattleSide::DEFENDER)};
				const Result result{trace.packets, units, bytes(*gameHandler->randomizer), rerolls};
				EXPECT_NE(bytes(*gameHandler->randomizer), rngBefore) << "Must execute a real authoritative chance draw";
				if(!expected) expected = result;
				else EXPECT_EQ(result, *expected);
				observedInjury |= victim->getCount() < 100;
				observedNoInjury |= victim->getCount() == 100;
				observedReroll |= rerolls[1].used;
				EXPECT_FALSE(rerolls[0].used);
				EXPECT_EQ(gameState()->saveToMemory(), world);
				EXPECT_EQ(bytes(CRandomGenerator::getDefault()), defaultRng);
			}
		}
		EXPECT_TRUE(observedNoInjury);
		EXPECT_EQ(observedInjury, !inert);
		EXPECT_EQ(observedReroll, reroll && !inert);
	}
};
}

TEST_F(NativeCombatEventAuthorityParityTest, OrdinaryChanceMatchesServerRngWithoutInjuryOrReroll)
{
	ASSERT_NO_FATAL_FAILURE(compare(true, true, false));
}

TEST_F(NativeCombatEventAuthorityParityTest, HostileChanceMatchesServerPacketsStateAndRng)
{
	ASSERT_NO_FATAL_FAILURE(compare(false, false, false));
}

TEST_F(NativeCombatEventAuthorityParityTest, OrderedHostileThenOrdinaryChanceMatchesRerollPacketOrder)
{
	ASSERT_NO_FATAL_FAILURE(compare(false, true, true));
}
